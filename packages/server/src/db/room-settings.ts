// Persisted per-room settings (protocol section 6). A room without a row
// uses the defaults.

import type { RoomMode, RoomSettings } from "@devotchi/shared";
import type { Db } from "./index.js";

export const DEFAULT_ROOM_MODE: RoomMode = "ask";

interface SettingsRow {
  mode: RoomMode;
  hold_ms: number | null;
  share_text: number;
  time_zone: string;
}

const COLUMNS = "mode, hold_ms, share_text, time_zone";

export function getRoomTimeZone(db: Db, roomId: string): string {
  return db.get<{ time_zone: string }>("SELECT time_zone FROM room_settings WHERE room_id = ?", roomId)?.time_zone ?? "";
}

export function getRoomMode(db: Db, roomId: string): RoomMode {
  const row = db.get<{ mode: RoomMode }>("SELECT mode FROM room_settings WHERE room_id = ?", roomId);
  return row?.mode ?? DEFAULT_ROOM_MODE;
}

/** `defaultHoldMs` (DEVOTCHI_PERMISSION_HOLD_MS) applies until the room sets its own `holdMs`. */
export function getRoomSettings(db: Db, roomId: string, defaultHoldMs: number): RoomSettings {
  const row = db.get<SettingsRow>(`SELECT ${COLUMNS} FROM room_settings WHERE room_id = ?`, roomId);
  return {
    mode: row?.mode ?? DEFAULT_ROOM_MODE,
    holdMs: row?.hold_ms ?? defaultHoldMs,
    shareText: row ? row.share_text !== 0 : false,
    timeZone: row?.time_zone ?? "",
  };
}

/** Applies the given fields and returns the resulting settings. */
export function updateRoomSettings(
  db: Db,
  roomId: string,
  changes: Partial<RoomSettings>,
  defaultHoldMs: number,
): RoomSettings {
  return db.transaction(() => {
    const row = db.get<SettingsRow>(`SELECT ${COLUMNS} FROM room_settings WHERE room_id = ?`, roomId);
    const mode = changes.mode ?? row?.mode ?? DEFAULT_ROOM_MODE;
    // NULL keeps following the server default until the room chooses a value.
    const holdMs = changes.holdMs ?? row?.hold_ms ?? null;
    const shareText = changes.shareText ?? (row ? row.share_text !== 0 : false);
    const timeZone = changes.timeZone ?? row?.time_zone ?? "";
    db.run(
      `INSERT INTO room_settings (room_id, mode, hold_ms, share_text, time_zone, updated_at) VALUES (?, ?, ?, ?, ?, ?)
       ON CONFLICT(room_id) DO UPDATE SET mode = excluded.mode, hold_ms = excluded.hold_ms,
         share_text = excluded.share_text, time_zone = excluded.time_zone, updated_at = excluded.updated_at`,
      roomId,
      mode,
      holdMs,
      shareText ? 1 : 0,
      timeZone,
      new Date().toISOString(),
    );
    return { mode, holdMs: holdMs ?? defaultHoldMs, shareText, timeZone };
  });
}
