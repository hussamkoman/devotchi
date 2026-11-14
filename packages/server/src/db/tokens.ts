// API tokens (protocol 7.1): UUID v4 values stored as SHA-256 hashes.

import { createHash, randomBytes, randomUUID } from "node:crypto";
import type { TokenInfo, TokenKind } from "@devotchi/shared";
import type { Db } from "./index.js";

export interface TokenRow {
  id: string;
  room_id: string;
  label: string | null;
  kind: TokenKind;
  user_id: string | null;
  created_at: string;
  last_used: string | null;
}

export interface MintedToken {
  token: string;
  id: string;
  roomId: string;
}

export function hashToken(raw: string): string {
  return createHash("sha256").update(raw).digest("hex");
}

/** A room id no token or user uses yet. */
export function allocateRoomId(db: Db): string {
  for (;;) {
    const id = randomBytes(6).toString("hex");
    const used =
      db.get("SELECT 1 FROM tokens WHERE room_id = ?", id) ?? db.get("SELECT 1 FROM users WHERE room_id = ?", id);
    if (!used) return id;
  }
}

export function findTokenByValue(db: Db, raw: string): TokenRow | undefined {
  return db.get<TokenRow>(
    "SELECT id, room_id, label, kind, user_id, created_at, last_used FROM tokens WHERE token_hash = ?",
    hashToken(raw),
  );
}

export function insertToken(
  db: Db,
  input: { roomId: string; kind: TokenKind; label: string | null; userId?: string | null },
): MintedToken {
  const token = randomUUID();
  const hash = hashToken(token);
  const now = new Date().toISOString();
  // 8 hex ids collide rarely but do collide; retry instead of failing the request.
  for (let attempt = 0; attempt < 16; attempt++) {
    const id = randomBytes(4).toString("hex");
    if (db.get("SELECT 1 FROM tokens WHERE id = ?", id)) continue;
    db.run(
      "INSERT INTO tokens (id, token_hash, room_id, label, kind, user_id, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)",
      id,
      hash,
      input.roomId,
      input.label,
      input.kind,
      input.userId ?? null,
      now,
    );
    return { token, id, roomId: input.roomId };
  }
  throw new Error("could not allocate a token id");
}

/** Writes last_used when the stored value is older than `minIntervalMs`. */
export function touchToken(db: Db, row: TokenRow, now: number, minIntervalMs: number): void {
  const last = row.last_used === null ? NaN : Date.parse(row.last_used);
  if (!Number.isNaN(last) && now - last < minIntervalMs) return;
  const iso = new Date(now).toISOString();
  db.run("UPDATE tokens SET last_used = ? WHERE id = ?", iso, row.id);
  row.last_used = iso;
}

export function listTokens(db: Db, roomId: string): TokenInfo[] {
  return db
    .all<TokenRow>(
      "SELECT id, room_id, label, kind, user_id, created_at, last_used FROM tokens WHERE room_id = ? ORDER BY created_at, id",
      roomId,
    )
    .map((row) => ({ id: row.id, label: row.label, kind: row.kind, createdAt: row.created_at, lastUsed: row.last_used }));
}

/** Deletes a token of the room (its device row cascades). Returns false if none. */
export function deleteToken(db: Db, roomId: string, id: string): boolean {
  return db.run("DELETE FROM tokens WHERE id = ? AND room_id = ?", id, roomId).changes === 1;
}
