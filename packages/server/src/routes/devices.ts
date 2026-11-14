// Device registry listing and pairing flow B (protocol 7.4, 7.5).

import Router from "@koa/router";
import { generatePairingCode, normalizePairingCode, type Device, type ListDevicesResponse } from "@devotchi/shared";
import type { ResolvedConfig } from "../config.js";
import type { Db } from "../db/index.js";
import { allocateDeviceId, insertDevice, listDeviceRows, type DeviceRow } from "../db/devices.js";
import { insertToken } from "../db/tokens.js";
import { authOf, requireAuth, requireCli, type Authenticator } from "../http/auth.js";
import { HttpError, badRequest, notFound, rateLimited } from "../http/errors.js";
import { byIp, type RateLimits } from "../http/rate-limit.js";
import type { RoomRegistry } from "../rooms/registry.js";

export interface DeviceRouteDeps {
  db: Db;
  config: ResolvedConfig;
  auth: Authenticator;
  limits: RateLimits;
  rooms: RoomRegistry;
}

const DEVICE_NAME = "device";

function toDevice(row: DeviceRow, online: boolean): Device {
  return {
    id: row.id,
    name: row.name,
    model: row.model,
    fw: row.fw,
    createdAt: row.created_at,
    online,
    lastSeen: row.last_seen,
    lastHoldAt: row.last_hold_at,
    holding: 0,
    telemetry: row.telemetry === null ? null : (JSON.parse(row.telemetry) as Device["telemetry"]),
    telemetryAt: row.telemetry_at,
  };
}

export function deviceRoutes(deps: DeviceRouteDeps): Router {
  const { db, config, auth, limits, rooms } = deps;
  const router = new Router();
  const cli = [requireAuth(auth), requireCli()];
  const claimFailures = limits.create(20, 60);

  router.get("/api/devices", ...cli, (ctx) => {
    const roomId = authOf(ctx).roomId;
    const room = rooms.get(roomId);
    const onlineTokens = new Set([...(room?.consumers ?? [])].map((conn) => conn.tokenId));
    const body: ListDevicesResponse = {
      devices: listDeviceRows(db, roomId).map((row) => toDevice(row, onlineTokens.has(row.token_id))),
    };
    ctx.body = body;
  });

  router.post("/api/pair", ...cli, (ctx) => {
    const ttl = config.timing.pairCodeTtlMs;
    const expiresAt = new Date(Date.now() + ttl).toISOString();
    let code = generatePairingCode();
    while (db.get("SELECT 1 FROM pairings WHERE code = ?", code)) code = generatePairingCode();
    db.run(
      "INSERT INTO pairings (code, room_id, expires_at, created_at) VALUES (?, ?, ?, ?)",
      code,
      authOf(ctx).roomId,
      expiresAt,
      new Date().toISOString(),
    );
    ctx.status = 201;
    ctx.body = { pairingCode: code, expiresAt, expiresInS: Math.round(ttl / 1000) };
  });

  router.post("/api/pair/claim", limits.middleware(10, byIp), (ctx) => {
    const ipKey = byIp(ctx);
    const now = Date.now();
    const wait = claimFailures.retryAfter(ipKey, now);
    if (wait > 0) throw rateLimited(wait);
    const body: unknown = ctx.request.body;
    if (body === null || typeof body !== "object" || Array.isArray(body)) throw badRequest("Body must be a JSON object");
    const raw = (body as { code?: unknown }).code;
    const code = typeof raw === "string" ? normalizePairingCode(raw) : null;
    if (code === null) throw badRequest("code must be a pairing code");

    const row = db.get<{ room_id: string; expires_at: string; claimed: number }>(
      "SELECT room_id, expires_at, claimed FROM pairings WHERE code = ?",
      code,
    );
    if (!row) {
      claimFailures.record(ipKey, now);
      throw notFound("Unknown pairing code");
    }
    if (row.claimed) throw new HttpError(410, "already_used", "Pairing code already used");
    if (Date.parse(row.expires_at) <= now) throw new HttpError(410, "expired", "Pairing code expired");

    const credentials = db.transaction(() => {
      db.run("UPDATE pairings SET claimed = 1 WHERE code = ?", code);
      const minted = insertToken(db, { roomId: row.room_id, kind: "device", label: DEVICE_NAME });
      const deviceId = allocateDeviceId(db, row.room_id);
      insertDevice(db, { roomId: row.room_id, id: deviceId, tokenId: minted.id, name: DEVICE_NAME });
      return { token: minted.token, roomId: row.room_id, deviceId, name: DEVICE_NAME };
    });
    ctx.status = 201;
    ctx.body = credentials;
  });

  return router;
}
