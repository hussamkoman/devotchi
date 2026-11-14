// Pairing (protocol 7.4 and 9): flow A (the device shows a code, the PC
// confirms) and flow B (the PC creates a code, the device claims it). Both
// end in a device token plus a registry row in the room that confirmed or
// created the code.

import { createHash, randomBytes } from "node:crypto";
import {
  PAIR_POLL_INTERVAL_MS,
  generatePairingCode,
  type CreatePairCodeResponse,
  type DevicePairPollResponse,
  type DevicePairStartResponse,
  type PairConfirmResponse,
  type PairedDeviceCredentials,
} from "@devotchi/shared";
import type { Db } from "../db/index.js";
import { allocateDeviceId, deleteDevice, insertDevice } from "../db/devices.js";
import { insertToken } from "../db/tokens.js";
import { HttpError, notFound } from "../http/errors.js";
import type { Logger } from "../log.js";
import type { WsServer } from "../ws/server.js";

export const DEFAULT_DEVICE_NAME = "device";
const DEVICE_TOKEN_LABEL = "device";

export interface PairingTiming {
  pairCodeTtlMs: number;
  pairDeliveryMs: number;
}

export interface PairingDeps {
  db: Db;
  ws: WsServer;
  log: Logger;
  timing: PairingTiming;
}

/** A confirmed flow A pairing. The raw token exists only here, never in the database. */
interface Delivery {
  credentials: PairedDeviceCredentials;
  confirmedAt: number;
  deliveredAt: number | null;
}

interface DevicePairingRow {
  code: string;
  device_id: string;
  model: string | null;
  fw: string | null;
  expires_at: string;
  state: "pending" | "confirmed" | "delivered";
  token_id: string | null;
}

export interface DeviceMeta {
  deviceId: string;
  model?: string;
  fw?: string;
}

const expired = () => new HttpError(410, "expired", "Pairing code expired");
const alreadyUsed = () => new HttpError(410, "already_used", "Pairing code already used");
const secretHash = (secret: string) => createHash("sha256").update(secret).digest("hex");
const seconds = (ms: number) => Math.max(0, Math.ceil(ms / 1000));

export class PairingService {
  private readonly deliveries = new Map<string, Delivery>();

  constructor(private readonly deps: PairingDeps) {}

  /** Flow B: POST /api/pair. */
  createCode(roomId: string, now = Date.now()): CreatePairCodeResponse {
    const { db, timing } = this.deps;
    const expiresAt = new Date(now + timing.pairCodeTtlMs).toISOString();
    const code = this.freshCode();
    db.run(
      "INSERT INTO pairings (code, room_id, expires_at, created_at) VALUES (?, ?, ?, ?)",
      code,
      roomId,
      expiresAt,
      new Date(now).toISOString(),
    );
    return { code, expiresAt, expiresInS: seconds(timing.pairCodeTtlMs) };
  }

  /** Flow B: POST /api/pair/claim. Throws 404/410 HttpErrors. */
  claim(code: string, meta: Partial<DeviceMeta>, now = Date.now()): PairedDeviceCredentials {
    const { db } = this.deps;
    const { credentials, replaced } = db.transaction(() => {
      const row = db.get<{ room_id: string; expires_at: string; claimed: number }>(
        "SELECT room_id, expires_at, claimed FROM pairings WHERE code = ?",
        code,
      );
      if (!row) throw notFound("Unknown pairing code");
      if (row.claimed) throw alreadyUsed();
      if (Date.parse(row.expires_at) <= now) throw expired();
      db.run("UPDATE pairings SET claimed = 1 WHERE code = ?", code);
      const deviceId = meta.deviceId ?? allocateDeviceId(db, row.room_id);
      return this.installDevice(row.room_id, { ...meta, deviceId }, DEFAULT_DEVICE_NAME);
    });
    this.closeReplaced(replaced);
    return credentials;
  }

  /** Flow A: POST /api/device/pair/start. */
  start(meta: DeviceMeta, now = Date.now()): DevicePairStartResponse {
    const { db, timing } = this.deps;
    const code = this.freshCode();
    const deviceSecret = randomBytes(16).toString("hex");
    db.run(
      `INSERT INTO device_pairings (code, secret_hash, device_id, model, fw, expires_at, state, created_at)
       VALUES (?, ?, ?, ?, ?, ?, 'pending', ?)`,
      code,
      secretHash(deviceSecret),
      meta.deviceId,
      meta.model ?? null,
      meta.fw ?? null,
      new Date(now + timing.pairCodeTtlMs).toISOString(),
      new Date(now).toISOString(),
    );
    return {
      code,
      deviceSecret,
      expiresInS: seconds(timing.pairCodeTtlMs),
      pollIntervalMs: PAIR_POLL_INTERVAL_MS,
    };
  }

  /** Flow A: POST /api/device/pair/poll. Throws 404/410 HttpErrors. */
  poll(deviceSecret: string, now = Date.now()): DevicePairPollResponse {
    const { db, timing } = this.deps;
    const row = db.get<DevicePairingRow>(
      "SELECT code, device_id, model, fw, expires_at, state, token_id FROM device_pairings WHERE secret_hash = ?",
      secretHash(deviceSecret),
    );
    if (!row) throw notFound("Unknown device secret");
    if (row.state === "pending") {
      const left = Date.parse(row.expires_at) - now;
      if (left <= 0) throw expired();
      return { status: "pending", expiresInS: seconds(left) };
    }
    const delivery = this.deliveries.get(row.code);
    if (row.state === "confirmed") {
      if (!delivery || row.token_id === null || now - delivery.confirmedAt > timing.pairDeliveryMs) {
        this.dropOrphan(row);
        throw expired();
      }
      delivery.deliveredAt = now;
      db.run("UPDATE device_pairings SET state = 'delivered' WHERE code = ?", row.code);
      return delivery.credentials;
    }
    // Delivered: the same body again while the response may have been lost.
    if (!delivery || delivery.deliveredAt === null || row.token_id === null || now - delivery.deliveredAt > timing.pairDeliveryMs) {
      this.deliveries.delete(row.code);
      throw expired();
    }
    return delivery.credentials;
  }

  /** Flow A: POST /api/pair/confirm by the room that pairs the device. Throws 404/410 HttpErrors. */
  confirm(roomId: string, code: string, name: string | undefined, now = Date.now()): PairConfirmResponse {
    const { db } = this.deps;
    const deviceName = name ?? DEFAULT_DEVICE_NAME;
    const { credentials, replaced } = db.transaction(() => {
      const row = db.get<DevicePairingRow>(
        "SELECT code, device_id, model, fw, expires_at, state, token_id FROM device_pairings WHERE code = ?",
        code,
      );
      if (!row) throw notFound("Unknown pairing code");
      if (row.state !== "pending") throw alreadyUsed();
      if (Date.parse(row.expires_at) <= now) throw expired();
      const meta: DeviceMeta = { deviceId: row.device_id };
      if (row.model !== null) meta.model = row.model;
      if (row.fw !== null) meta.fw = row.fw;
      const installed = this.installDevice(roomId, meta, deviceName);
      db.run(
        "UPDATE device_pairings SET state = 'confirmed', room_id = ?, confirmed_at = ?, name = ?, token_id = ? WHERE code = ?",
        roomId,
        new Date(now).toISOString(),
        deviceName,
        installed.tokenId,
        code,
      );
      return installed;
    });
    this.deliveries.set(code, { credentials, confirmedAt: now, deliveredAt: null });
    this.closeReplaced(replaced);
    this.deps.log.info("device pairing confirmed", { room: roomId, device: credentials.deviceId });
    return { deviceId: credentials.deviceId, name: deviceName };
  }

  /**
   * Removes expired and claimed codes and finished deliveries, and deletes
   * the device row and token of every flow A pairing that was confirmed but
   * not fetched in time. At startup the delivery map is empty, so that
   * applies to every confirmed-but-undelivered pairing.
   */
  prune(now = Date.now()): void {
    const { db, timing } = this.deps;
    const nowIso = new Date(now).toISOString();
    db.transaction(() => {
      const rows = db.all<DevicePairingRow>(
        "SELECT code, device_id, model, fw, expires_at, state, token_id FROM device_pairings WHERE state <> 'pending'",
      );
      for (const row of rows) {
        const delivery = this.deliveries.get(row.code);
        if (row.state === "confirmed") {
          if (!delivery || now - delivery.confirmedAt > timing.pairDeliveryMs) this.dropOrphan(row);
        } else if (!delivery || delivery.deliveredAt === null || now - delivery.deliveredAt > timing.pairDeliveryMs) {
          this.deliveries.delete(row.code);
        }
      }
      // A row past its expiry stays while its delivery window is open (the code may expire right after a confirm).
      for (const { code } of db.all<{ code: string }>("SELECT code FROM device_pairings WHERE expires_at <= ?", nowIso)) {
        if (!this.deliveries.has(code)) db.run("DELETE FROM device_pairings WHERE code = ?", code);
      }
      db.run("DELETE FROM pairings WHERE claimed = 1 OR expires_at <= ?", nowIso);
    });
  }

  /** A pairing confirmed but never fetched: its device row and token go; the row stays as a 410 until it expires. */
  private dropOrphan(row: DevicePairingRow): void {
    this.deliveries.delete(row.code);
    const { db } = this.deps;
    db.transaction(() => {
      // Deleting the token cascades to its device row and sets token_id to NULL.
      if (row.token_id !== null) db.run("DELETE FROM tokens WHERE id = ?", row.token_id);
      db.run("UPDATE device_pairings SET token_id = NULL WHERE code = ?", row.code);
    });
    if (row.token_id !== null) {
      this.deps.ws.closeToken(row.token_id, undefined, "device unpaired");
      this.deps.log.info("undelivered device pairing removed", { device: row.device_id });
    }
  }

  /**
   * Mints the device token and registry row. An existing device with the
   * same id in this room is the same board paired again and is replaced;
   * devices of other rooms are never touched (protocol 7.4).
   */
  private installDevice(
    roomId: string,
    meta: DeviceMeta,
    name: string,
  ): { credentials: PairedDeviceCredentials; replaced: string | null; tokenId: string } {
    const { db } = this.deps;
    const replaced = deleteDevice(db, roomId, meta.deviceId);
    const minted = insertToken(db, { roomId, kind: "device", label: DEVICE_TOKEN_LABEL });
    insertDevice(db, { roomId, id: meta.deviceId, tokenId: minted.id, name, model: meta.model, fw: meta.fw });
    return {
      credentials: { token: minted.token, roomId, deviceId: meta.deviceId, name },
      replaced,
      tokenId: minted.id,
    };
  }

  private closeReplaced(tokenId: string | null): void {
    if (tokenId !== null) this.deps.ws.closeToken(tokenId, undefined, "device paired again");
  }

  /** A code used by no live flow A or flow B row. */
  private freshCode(): string {
    const { db } = this.deps;
    for (;;) {
      const code = generatePairingCode();
      const used =
        db.get("SELECT 1 FROM pairings WHERE code = ?", code) ?? db.get("SELECT 1 FROM device_pairings WHERE code = ?", code);
      if (!used) return code;
    }
  }
}
