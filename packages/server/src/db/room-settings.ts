// Persisted per-room settings (protocol section 6). A room without a row
// uses the defaults.

import type { RoomMode } from "@devotchi/shared";
import type { Db } from "./index.js";

export const DEFAULT_ROOM_MODE: RoomMode = "ask";

export function getRoomMode(db: Db, roomId: string): RoomMode {
  const row = db.get<{ mode: RoomMode }>("SELECT mode FROM room_settings WHERE room_id = ?", roomId);
  return row?.mode ?? DEFAULT_ROOM_MODE;
}
