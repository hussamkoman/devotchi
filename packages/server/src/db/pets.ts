// Room uploads of protocol 14.8. Packs are validated before they are stored
// and again before they are served.

import type { Db } from "./index.js";

export interface PetRow {
  id: string;
  rev: number;
  bytes: Uint8Array;
}

export function listRoomPets(db: Db, roomId: string): PetRow[] {
  return db.all<PetRow>("SELECT id, rev, bytes FROM pets WHERE room_id = ? ORDER BY id", roomId);
}

export function findRoomPet(db: Db, roomId: string, id: string): PetRow | undefined {
  return db.get<PetRow>("SELECT id, rev, bytes FROM pets WHERE room_id = ? AND id = ?", roomId, id);
}

export function countRoomPets(db: Db, roomId: string): number {
  return db.get<{ n: number }>("SELECT COUNT(*) AS n FROM pets WHERE room_id = ?", roomId)?.n ?? 0;
}

/** Inserts or replaces the room's pack with this id. */
export function putRoomPet(db: Db, roomId: string, row: PetRow, now: number): void {
  db.run(
    `INSERT INTO pets (room_id, id, rev, bytes, created_at) VALUES (?, ?, ?, ?, ?)
     ON CONFLICT (room_id, id) DO UPDATE SET rev = excluded.rev, bytes = excluded.bytes, created_at = excluded.created_at`,
    roomId,
    row.id,
    row.rev,
    row.bytes,
    new Date(now).toISOString(),
  );
}

export function deleteRoomPet(db: Db, roomId: string, id: string): boolean {
  return db.run("DELETE FROM pets WHERE room_id = ? AND id = ?", roomId, id).changes > 0;
}
