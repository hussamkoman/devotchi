// Schema migrations keyed by PRAGMA user_version.
//   0: the sql.js era schema (tokens, users, user_tokens, pairings), or an
//      empty file.
//   1: architecture 2.2.
//   2: device_pairings.token_id, so an undelivered flow A pairing removes
//      exactly the token it minted even if the room re-paired that device id.

import { randomBytes } from "node:crypto";
import type { Logger } from "../log.js";
import type { Db } from "./index.js";

export const SCHEMA_VERSION = 2;

/** The v0 schema exactly as the sql.js server created it. */
export const V0_SCHEMA = `
CREATE TABLE IF NOT EXISTS tokens (
  id TEXT PRIMARY KEY,
  token_hash TEXT NOT NULL,
  room_id TEXT NOT NULL,
  label TEXT,
  created_at TEXT DEFAULT (datetime('now')),
  last_used TEXT DEFAULT (datetime('now'))
);
CREATE TABLE IF NOT EXISTS users (
  id TEXT PRIMARY KEY,
  email TEXT UNIQUE NOT NULL,
  password_hash TEXT NOT NULL,
  created_at TEXT DEFAULT (datetime('now'))
);
CREATE TABLE IF NOT EXISTS user_tokens (
  user_id TEXT REFERENCES users(id) ON DELETE CASCADE,
  token_id TEXT REFERENCES tokens(id) ON DELETE CASCADE,
  PRIMARY KEY (user_id, token_id)
);
CREATE TABLE IF NOT EXISTS pairings (
  id TEXT PRIMARY KEY,
  room_id TEXT NOT NULL,
  pairing_code TEXT UNIQUE NOT NULL,
  expires_at TEXT NOT NULL,
  claimed INTEGER DEFAULT 0,
  created_at TEXT DEFAULT (datetime('now'))
);
`;

// Every timestamp column holds ISO 8601 UTC with milliseconds and Z, the
// format of Date.prototype.toISOString().
const ISO = (column: string) => `strftime('%Y-%m-%dT%H:%M:%fZ', ${column})`;
const NOW_ISO = ISO("'now'");

const V1_TABLES = `
CREATE TABLE users_v1 (
  id TEXT PRIMARY KEY,
  email TEXT NOT NULL UNIQUE,
  password_hash TEXT NOT NULL,
  room_id TEXT NOT NULL,
  created_at TEXT NOT NULL
);
CREATE TABLE tokens_v1 (
  id TEXT PRIMARY KEY,
  token_hash TEXT NOT NULL,
  room_id TEXT NOT NULL,
  label TEXT,
  kind TEXT NOT NULL CHECK (kind IN ('cli', 'device')),
  user_id TEXT REFERENCES users(id) ON DELETE CASCADE,
  created_at TEXT NOT NULL,
  last_used TEXT
);
CREATE TABLE pairings_v1 (
  code TEXT PRIMARY KEY,
  room_id TEXT NOT NULL,
  expires_at TEXT NOT NULL,
  claimed INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL
);
CREATE TABLE devices (
  room_id TEXT NOT NULL,
  id TEXT NOT NULL,
  token_id TEXT NOT NULL UNIQUE REFERENCES tokens(id) ON DELETE CASCADE,
  name TEXT NOT NULL,
  model TEXT,
  fw TEXT,
  adopt_id INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL,
  last_seen TEXT,
  last_hold_at TEXT,
  telemetry TEXT,
  telemetry_at TEXT,
  PRIMARY KEY (room_id, id)
);
CREATE TABLE device_pairings (
  code TEXT PRIMARY KEY,
  secret_hash TEXT NOT NULL UNIQUE,
  device_id TEXT NOT NULL,
  model TEXT,
  fw TEXT,
  expires_at TEXT NOT NULL,
  state TEXT NOT NULL DEFAULT 'pending' CHECK (state IN ('pending', 'confirmed', 'delivered')),
  room_id TEXT,
  confirmed_at TEXT,
  name TEXT,
  created_at TEXT NOT NULL
);
CREATE TABLE room_settings (
  room_id TEXT PRIMARY KEY,
  mode TEXT NOT NULL DEFAULT 'ask' CHECK (mode IN ('ask', 'desk')),
  hold_ms INTEGER,
  share_text INTEGER NOT NULL DEFAULT 0,
  updated_at TEXT NOT NULL
);
CREATE TABLE sessions (
  id TEXT PRIMARY KEY,
  user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  token_hash TEXT NOT NULL UNIQUE,
  created_at TEXT NOT NULL,
  expires_at TEXT NOT NULL,
  last_used TEXT
);
`;

const V1_INDEXES = `
CREATE UNIQUE INDEX tokens_token_hash ON tokens(token_hash);
CREATE INDEX tokens_room ON tokens(room_id);
CREATE INDEX users_room ON users(room_id);
CREATE INDEX pairings_room ON pairings(room_id);
CREATE INDEX sessions_user ON sessions(user_id);
`;

const HEX6_RE = /^[0-9a-f]{6}/;

/**
 * Legacy device tokens get a registry row with the provisional id
 * dv-<first 6 hex of the token id>; on a clash within the room the hex value
 * is incremented until it is free. adopt_id lets the row take the board's
 * real id on its first register.
 */
function createLegacyDeviceRows(db: Db): number {
  const tokens = db.all<{ id: string; room_id: string; created_at: string }>(
    "SELECT id, room_id, created_at FROM tokens_v1 WHERE kind = 'device' ORDER BY created_at, id",
  );
  const taken = new Map<string, Set<string>>();
  for (const token of tokens) {
    let used = taken.get(token.room_id);
    if (!used) taken.set(token.room_id, (used = new Set()));
    const prefix = HEX6_RE.exec(token.id.toLowerCase())?.[0] ?? randomBytes(3).toString("hex");
    let value = parseInt(prefix, 16);
    let id = `dv-${prefix}`;
    while (used.has(id)) {
      value = (value + 1) % 0x1000000;
      id = `dv-${value.toString(16).padStart(6, "0")}`;
    }
    used.add(id);
    db.run(
      "INSERT INTO devices (room_id, id, token_id, name, adopt_id, created_at) VALUES (?, ?, ?, 'device', 1, ?)",
      token.room_id,
      id,
      token.id,
      token.created_at,
    );
  }
  return tokens.length;
}

function migrateV0toV1(db: Db): { tokens: number; devices: number } {
  db.exec(V0_SCHEMA);
  db.exec(V1_TABLES);
  // A legacy user gets the room of its oldest linked token, or a new room.
  db.exec(`
    INSERT INTO users_v1 (id, email, password_hash, room_id, created_at)
    SELECT u.id, u.email, u.password_hash,
      COALESCE(
        (SELECT t.room_id FROM user_tokens ut JOIN tokens t ON t.id = ut.token_id
          WHERE ut.user_id = u.id ORDER BY t.created_at, t.id LIMIT 1),
        lower(hex(randomblob(6)))),
      COALESCE(${ISO("u.created_at")}, ${NOW_ISO})
    FROM users u;
    INSERT OR IGNORE INTO tokens_v1 (id, token_hash, room_id, label, kind, user_id, created_at, last_used)
    SELECT t.id, t.token_hash, t.room_id, t.label,
      CASE WHEN t.label = 'device' THEN 'device' ELSE 'cli' END,
      (SELECT ut.user_id FROM user_tokens ut WHERE ut.token_id = t.id ORDER BY ut.user_id LIMIT 1),
      COALESCE(${ISO("t.created_at")}, ${NOW_ISO}),
      ${ISO("t.last_used")}
    FROM tokens t ORDER BY t.rowid;
    INSERT OR IGNORE INTO pairings_v1 (code, room_id, expires_at, claimed, created_at)
    SELECT p.pairing_code, p.room_id, COALESCE(${ISO("p.expires_at")}, ${NOW_ISO}),
      CASE WHEN p.claimed THEN 1 ELSE 0 END, COALESCE(${ISO("p.created_at")}, ${NOW_ISO})
    FROM pairings p;
  `);
  const devices = createLegacyDeviceRows(db);
  const tokens = db.get<{ n: number }>("SELECT COUNT(*) AS n FROM tokens_v1")?.n ?? 0;
  db.exec(`
    DROP TABLE user_tokens;
    DROP TABLE tokens;
    DROP TABLE users;
    DROP TABLE pairings;
    ALTER TABLE users_v1 RENAME TO users;
    ALTER TABLE tokens_v1 RENAME TO tokens;
    ALTER TABLE pairings_v1 RENAME TO pairings;
  `);
  db.exec(V1_INDEXES);
  return { tokens, devices };
}

function migrateV1toV2(db: Db): void {
  db.exec("ALTER TABLE device_pairings ADD COLUMN token_id TEXT REFERENCES tokens(id) ON DELETE SET NULL");
}

/** Brings the schema to SCHEMA_VERSION. Requires foreign keys to be off. */
export function migrate(db: Db, log: Logger): void {
  const version = db.get<{ user_version: number }>("PRAGMA user_version")?.user_version ?? 0;
  if (version > SCHEMA_VERSION) {
    throw new Error(`schema version ${version} is newer than this server supports (${SCHEMA_VERSION})`);
  }
  if (version === SCHEMA_VERSION) return;
  db.transaction(() => {
    let counts = {};
    if (version < 1) counts = migrateV0toV1(db);
    if (version < 2) migrateV1toV2(db);
    const violations = db.all("PRAGMA foreign_key_check");
    if (violations.length > 0) throw new Error(`migration left ${violations.length} foreign key violations`);
    db.exec(`PRAGMA user_version = ${SCHEMA_VERSION}`);
    log.info("database migrated", { from: version, to: SCHEMA_VERSION, ...counts });
  });
}
