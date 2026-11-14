import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { chmodSync, existsSync, readFileSync, writeFileSync } from "node:fs";
import { rm } from "node:fs/promises";
import { join } from "node:path";
import { DatabaseSync } from "node:sqlite";
import { fileURLToPath } from "node:url";
import { afterEach, describe, expect, it } from "vitest";
import { DatabaseError, createServer } from "../src/app.js";
import { openDatabase } from "../src/db/index.js";
import { Logger } from "../src/log.js";
import { TestSocket, makeTempDir, startTestServer, type TestServer } from "./helpers.js";

const dirs: string[] = [];
let ts: TestServer | undefined;
const quiet = new Logger("error", () => undefined);

async function tempDir(): Promise<string> {
  const dir = await makeTempDir("devotchi-db-");
  dirs.push(dir);
  return dir;
}

afterEach(async () => {
  await ts?.close();
  ts = undefined;
  for (const dir of dirs.splice(0)) {
    chmodSync(dir, 0o755);
    await rm(dir, { recursive: true, force: true });
  }
});

const hash = (raw: string) => createHash("sha256").update(raw).digest("hex");

const CLI_TOKEN = "11111111-1111-4111-8111-111111111111";
const DEVICE_TOKEN = "22222222-2222-4222-8222-222222222222";
const DEVICE_TOKEN_2 = "33333333-3333-4333-8333-333333333333";
const OTHER_ROOM_DEVICE = "44444444-4444-4444-8444-444444444444";
const ROOM = "5e140e256d56";
const OTHER_ROOM = "aaaaaaaaaaaa";

/** A database exactly as the sql.js server wrote it (user_version 0). */
function writeV0Fixture(path: string): void {
  const db = new DatabaseSync(path);
  db.exec(`
    CREATE TABLE tokens (id TEXT PRIMARY KEY, token_hash TEXT NOT NULL, room_id TEXT NOT NULL, label TEXT,
      created_at TEXT DEFAULT (datetime('now')), last_used TEXT DEFAULT (datetime('now')));
    CREATE TABLE users (id TEXT PRIMARY KEY, email TEXT UNIQUE NOT NULL, password_hash TEXT NOT NULL,
      created_at TEXT DEFAULT (datetime('now')));
    CREATE TABLE user_tokens (user_id TEXT REFERENCES users(id) ON DELETE CASCADE,
      token_id TEXT REFERENCES tokens(id) ON DELETE CASCADE, PRIMARY KEY (user_id, token_id));
    CREATE TABLE pairings (id TEXT PRIMARY KEY, room_id TEXT NOT NULL, pairing_code TEXT UNIQUE NOT NULL,
      expires_at TEXT NOT NULL, claimed INTEGER DEFAULT 0, created_at TEXT DEFAULT (datetime('now')));
  `);
  const insert = db.prepare("INSERT INTO tokens (id, token_hash, room_id, label, created_at, last_used) VALUES (?, ?, ?, ?, ?, ?)");
  insert.run("a1b2c3d4", hash(CLI_TOKEN), ROOM, "cli", "2026-01-02 03:04:05", "2026-01-03 00:00:00");
  insert.run("e356e4ff", hash(DEVICE_TOKEN), ROOM, "device", "2026-01-04 10:00:00", null);
  // Same first 6 hex as the previous token in the same room.
  insert.run("e356e4aa", hash(DEVICE_TOKEN_2), ROOM, "device", "2026-01-05 10:00:00", null);
  insert.run("e356e4bb", hash(OTHER_ROOM_DEVICE), OTHER_ROOM, "device", "2026-01-06 10:00:00", null);
  db.prepare("INSERT INTO users (id, email, password_hash, created_at) VALUES (?, ?, ?, ?)").run(
    "u1",
    "a@example.com",
    "$2a$12$hash",
    "2026-01-01 00:00:00",
  );
  db.prepare("INSERT INTO user_tokens (user_id, token_id) VALUES (?, ?)").run("u1", "a1b2c3d4");
  db.prepare("INSERT INTO pairings (id, room_id, pairing_code, expires_at) VALUES (?, ?, ?, ?)").run(
    "p1",
    ROOM,
    "FBMNDP",
    "2026-01-01T00:05:00.000Z",
  );
  db.close();
}

describe("migration from the v0 schema", () => {
  it("backs up the file, keeps the tokens and creates provisional device rows", async () => {
    const dir = await tempDir();
    const dbPath = join(dir, "devotchi.db");
    writeV0Fixture(dbPath);
    const original = readFileSync(dbPath);

    ts = await startTestServer({ dir, dbPath });
    const backup = `${dbPath}.v0.bak`;
    expect(readFileSync(backup).equals(original)).toBe(true);
    expect(ts.logs.some((l) => l.includes("v0.bak"))).toBe(true);

    const { db } = ts.server.internals;
    expect(db.get<{ user_version: number }>("PRAGMA user_version")?.user_version).toBe(1);
    expect(db.get("SELECT name FROM sqlite_master WHERE name = 'user_tokens'")).toBeUndefined();
    const tokens = db.all("SELECT id, room_id, label, kind, user_id, created_at, last_used FROM tokens ORDER BY id");
    expect(tokens).toEqual([
      { id: "a1b2c3d4", room_id: ROOM, label: "cli", kind: "cli", user_id: "u1", created_at: "2026-01-02T03:04:05.000Z", last_used: "2026-01-03T00:00:00.000Z" },
      { id: "e356e4aa", room_id: ROOM, label: "device", kind: "device", user_id: null, created_at: "2026-01-05T10:00:00.000Z", last_used: null },
      { id: "e356e4bb", room_id: OTHER_ROOM, label: "device", kind: "device", user_id: null, created_at: "2026-01-06T10:00:00.000Z", last_used: null },
      { id: "e356e4ff", room_id: ROOM, label: "device", kind: "device", user_id: null, created_at: "2026-01-04T10:00:00.000Z", last_used: null },
    ]);
    const devices = db.all("SELECT room_id, id, token_id, name, adopt_id, created_at FROM devices ORDER BY room_id, id");
    expect(devices).toEqual([
      { room_id: ROOM, id: "dv-e356e4", token_id: "e356e4ff", name: "device", adopt_id: 1, created_at: "2026-01-04T10:00:00.000Z" },
      { room_id: ROOM, id: "dv-e356e5", token_id: "e356e4aa", name: "device", adopt_id: 1, created_at: "2026-01-05T10:00:00.000Z" },
      { room_id: OTHER_ROOM, id: "dv-e356e4", token_id: "e356e4bb", name: "device", adopt_id: 1, created_at: "2026-01-06T10:00:00.000Z" },
    ]);
    expect(db.get("SELECT room_id FROM users WHERE id = 'u1'")).toEqual({ room_id: ROOM });
    expect(db.get("SELECT code, room_id, claimed FROM pairings")).toEqual({ code: "FBMNDP", room_id: ROOM, claimed: 0 });

    const me = await ts.fetch("/api/me", { token: CLI_TOKEN });
    expect(await me.json()).toMatchObject({ kind: "token", roomId: ROOM, tokenId: "a1b2c3d4" });
    const device = await TestSocket.connect(ts.wsUrl, { token: DEVICE_TOKEN });
    const ack = await device.register("consumer", { kind: "device", deviceId: "dv-e356e4" });
    expect(ack.device).toEqual({ id: "dv-e356e4", name: "device" });
    device.close();
  });

  it("never overwrites an existing backup and does not back up a migrated file", async () => {
    const dir = await tempDir();
    const dbPath = join(dir, "devotchi.db");
    writeV0Fixture(dbPath);
    writeFileSync(`${dbPath}.v0.bak`, "older backup");
    ts = await startTestServer({ dir, dbPath });
    await ts.close();
    ts = await startTestServer({ dir, dbPath });
    expect(readFileSync(`${dbPath}.v0.bak`, "utf8")).toBe("older backup");
  });

  it("creates no backup for a new database", async () => {
    const dir = await tempDir();
    ts = await startTestServer({ dir });
    expect(existsSync(`${ts.dbPath}.v0.bak`)).toBe(false);
    const { db } = ts.server.internals;
    expect(db.get<{ journal_mode: string }>("PRAGMA journal_mode")?.journal_mode).toBe("wal");
    expect(db.get<{ foreign_keys: number }>("PRAGMA foreign_keys")?.foreign_keys).toBe(1);
  });
});

describe("durability", () => {
  it("keeps data across close and reopen", async () => {
    const dir = await tempDir();
    ts = await startTestServer({ dir });
    const minted = await ts.mintToken("keep");
    await ts.close();
    ts = await startTestServer({ dir });
    expect((await ts.fetch("/api/me", { token: minted.token })).status).toBe(200);
  });

  it("keeps committed data when the handle is never closed", async () => {
    const dir = await tempDir();
    const path = join(dir, "devotchi.db");
    const first = openDatabase(path, quiet);
    first.run("INSERT INTO tokens (id, token_hash, room_id, label, kind, created_at) VALUES ('deadbeef', 'h', 'r', null, 'cli', 'now')");
    const second = openDatabase(path, quiet);
    expect(second.get("SELECT id FROM tokens")).toEqual({ id: "deadbeef" });
    second.close();
    first.close();
  });

  it("keeps a minted token after the server process is killed with SIGKILL", async () => {
    const dir = await tempDir();
    const dbPath = join(dir, "devotchi.db");
    const entry = fileURLToPath(new URL("../dist/index.js", import.meta.url));
    expect(existsSync(entry), "run `pnpm run build` before the tests").toBe(true);
    const child = spawn(process.execPath, [entry], {
      env: { PATH: process.env.PATH, HOME: dir, DEVOTCHI_HOST: "127.0.0.1", DEVOTCHI_PORT: "0", DEVOTCHI_DB_PATH: dbPath },
      stdio: ["ignore", "pipe", "pipe"],
    });
    try {
      const port = await new Promise<number>((resolve, reject) => {
        let out = "";
        child.stdout.on("data", (chunk: Buffer) => {
          out += chunk.toString();
          const line = out.split("\n").find((l) => l.includes('"listening"'));
          if (line) resolve(JSON.parse(line).port as number);
        });
        child.once("exit", (code) => reject(new Error(`server exited with ${code}`)));
      });
      const res = await fetch(`http://127.0.0.1:${port}/api/auth/token`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: "{}",
      });
      const minted = (await res.json()) as { token: string };
      const exited = new Promise((resolve) => child.once("exit", resolve));
      child.kill("SIGKILL");
      await exited;
      ts = await startTestServer({ dir, dbPath });
      expect((await ts.fetch("/api/me", { token: minted.token })).status).toBe(200);
    } finally {
      if (child.exitCode === null && child.signalCode === null) child.kill("SIGKILL");
    }
  });
});

describe("startup failures", () => {
  it("refuses a corrupt file with a one-line error and leaves it in place", async () => {
    const dir = await tempDir();
    const dbPath = join(dir, "devotchi.db");
    const garbage = Buffer.alloc(8192, 0x5a);
    writeFileSync(dbPath, garbage);
    const err = await createServer({ dbPath, port: 0, logSink: () => undefined }).catch((e: unknown) => e);
    expect(err).toBeInstanceOf(DatabaseError);
    expect((err as Error).message).not.toContain("\n");
    expect(readFileSync(dbPath).equals(garbage)).toBe(true);
  });

  it("refuses a file with a valid header but damaged pages and does not delete it", async () => {
    const dir = await tempDir();
    const dbPath = join(dir, "devotchi.db");
    ts = await startTestServer({ dir, dbPath });
    await ts.mintToken();
    await ts.close();
    ts = undefined;
    const bytes = readFileSync(dbPath);
    bytes.fill(0xa5, 100, bytes.length);
    writeFileSync(dbPath, bytes);
    const err = await createServer({ dbPath, port: 0, logSink: () => undefined }).catch((e: unknown) => e);
    expect(err).toBeInstanceOf(DatabaseError);
    expect((err as Error).message).not.toContain("\n");
    expect(existsSync(dbPath)).toBe(true);
  });

  it("refuses a database from a newer server", async () => {
    const dir = await tempDir();
    const dbPath = join(dir, "devotchi.db");
    const db = new DatabaseSync(dbPath);
    db.exec("PRAGMA user_version = 7");
    db.close();
    const err = await createServer({ dbPath, port: 0, logSink: () => undefined }).catch((e: unknown) => e);
    expect(err).toBeInstanceOf(DatabaseError);
    expect((err as Error).message).toContain("newer");
  });

  it.skipIf(process.getuid?.() === 0)("fails at startup when the file or directory is not writable", async () => {
    const dir = await tempDir();
    const dbPath = join(dir, "devotchi.db");
    ts = await startTestServer({ dir, dbPath });
    await ts.close();
    ts = undefined;
    chmodSync(dbPath, 0o444);
    const fileErr = await createServer({ dbPath, port: 0, logSink: () => undefined }).catch((e: unknown) => e);
    expect(fileErr).toBeInstanceOf(DatabaseError);
    chmodSync(dbPath, 0o644);

    const readonlyDir = await tempDir();
    chmodSync(readonlyDir, 0o555);
    const dirErr = await createServer({ dbPath: join(readonlyDir, "x.db"), port: 0, logSink: () => undefined }).catch(
      (e: unknown) => e,
    );
    expect(dirErr).toBeInstanceOf(DatabaseError);
    expect((dirErr as Error).message).toContain("not writable");
  });
});
