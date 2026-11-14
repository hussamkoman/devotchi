// Device registry rows (architecture 2.2), keyed by (room_id, id).

import { randomBytes } from "node:crypto";
import { isValidDeviceId } from "@devotchi/shared";
import type { Db } from "./index.js";

export interface DeviceRow {
  room_id: string;
  id: string;
  token_id: string;
  name: string;
  model: string | null;
  fw: string | null;
  adopt_id: number;
  created_at: string;
  last_seen: string | null;
  last_hold_at: string | null;
  telemetry: string | null;
  telemetry_at: string | null;
}

const COLUMNS =
  "room_id, id, token_id, name, model, fw, adopt_id, created_at, last_seen, last_hold_at, telemetry, telemetry_at";

export function findDeviceByToken(db: Db, tokenId: string): DeviceRow | undefined {
  return db.get<DeviceRow>(`SELECT ${COLUMNS} FROM devices WHERE token_id = ?`, tokenId);
}

export function findDevice(db: Db, roomId: string, id: string): DeviceRow | undefined {
  return db.get<DeviceRow>(`SELECT ${COLUMNS} FROM devices WHERE room_id = ? AND id = ?`, roomId, id);
}

export function listDeviceRows(db: Db, roomId: string): DeviceRow[] {
  return db.all<DeviceRow>(`SELECT ${COLUMNS} FROM devices WHERE room_id = ? ORDER BY created_at, id`, roomId);
}

/** A `dv-` id with 6 random hex digits that is free in the room. */
export function allocateDeviceId(db: Db, roomId: string): string {
  for (;;) {
    const id = `dv-${randomBytes(3).toString("hex")}`;
    if (!db.get("SELECT 1 FROM devices WHERE room_id = ? AND id = ?", roomId, id)) return id;
  }
}

export function insertDevice(
  db: Db,
  input: { roomId: string; id: string; tokenId: string; name: string; model?: string | null; fw?: string | null },
): void {
  db.run(
    "INSERT INTO devices (room_id, id, token_id, name, model, fw, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)",
    input.roomId,
    input.id,
    input.tokenId,
    input.name,
    input.model ?? null,
    input.fw ?? null,
    new Date().toISOString(),
  );
}

/** Records the time of a device's last `holding` frame (protocol 7.5 `lastHoldAt`). */
export function touchDeviceHold(db: Db, tokenId: string, at: number): void {
  db.run("UPDATE devices SET last_hold_at = ? WHERE token_id = ?", new Date(at).toISOString(), tokenId);
}

export interface RegisterInfo {
  /** `client.deviceId` as sent. */
  claimedId?: string;
  model?: string;
  /** `client.version`, stored as `fw`. */
  version?: string;
}

/**
 * The registry row of a device token at register time (protocol 3.2). A row
 * from the v1 migration (adopt_id = 1) takes the valid id the device claims,
 * unless another device of the room already has it; either way adopt_id is
 * cleared, so a row adopts at most once. model and fw are updated when sent.
 */
export function bindDeviceOnRegister(db: Db, tokenId: string, info: RegisterInfo): DeviceRow | undefined {
  return db.transaction(() => {
    const row = findDeviceByToken(db, tokenId);
    if (!row) return undefined;
    if (row.adopt_id === 1) {
      const claimed = info.claimedId;
      const free =
        claimed !== undefined &&
        isValidDeviceId(claimed) &&
        !db.get("SELECT 1 FROM devices WHERE room_id = ? AND id = ? AND token_id <> ?", row.room_id, claimed, tokenId);
      if (free) row.id = claimed;
      row.adopt_id = 0;
      db.run("UPDATE devices SET id = ?, adopt_id = 0 WHERE token_id = ?", row.id, tokenId);
    }
    if (info.model !== undefined) row.model = info.model;
    if (info.version !== undefined) row.fw = info.version;
    if (info.model !== undefined || info.version !== undefined) {
      db.run("UPDATE devices SET model = ?, fw = ? WHERE token_id = ?", row.model, row.fw, tokenId);
    }
    return row;
  });
}

/** Deletes a device and its token. Returns the token id, or null when the room has no such device. */
export function deleteDevice(db: Db, roomId: string, id: string): string | null {
  return db.transaction(() => {
    const row = db.get<{ token_id: string }>("SELECT token_id FROM devices WHERE room_id = ? AND id = ?", roomId, id);
    if (!row) return null;
    db.run("DELETE FROM devices WHERE room_id = ? AND id = ?", roomId, id);
    db.run("DELETE FROM tokens WHERE id = ?", row.token_id);
    return row.token_id;
  });
}

export function renameDevice(db: Db, roomId: string, id: string, name: string): boolean {
  return db.run("UPDATE devices SET name = ? WHERE room_id = ? AND id = ?", name, roomId, id).changes === 1;
}

/** Never moves last_seen backwards (several sockets of one token may flush out of order). */
export function writeDeviceSeen(db: Db, tokenId: string, at: number): void {
  const iso = new Date(at).toISOString();
  db.run("UPDATE devices SET last_seen = ? WHERE token_id = ? AND (last_seen IS NULL OR last_seen < ?)", iso, tokenId, iso);
}

export function writeDeviceTelemetry(db: Db, tokenId: string, telemetry: unknown, at: number): void {
  db.run(
    "UPDATE devices SET telemetry = ?, telemetry_at = ? WHERE token_id = ?",
    JSON.stringify(telemetry),
    new Date(at).toISOString(),
    tokenId,
  );
}
