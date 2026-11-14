// Device registry rows (architecture 2.2), keyed by (room_id, id).

import { randomBytes } from "node:crypto";
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
