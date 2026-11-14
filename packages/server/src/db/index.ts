// SQLite through node:sqlite (architecture 2.2): WAL, foreign keys on,
// synchronous writes, schema migrations keyed by PRAGMA user_version.

import { accessSync, closeSync, constants, copyFileSync, existsSync, mkdirSync, openSync, readSync, statSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { DatabaseSync, type SQLInputValue, type SQLOutputValue, type StatementSync } from "node:sqlite";
import type { Logger } from "../log.js";
import { StartupError } from "../startup-error.js";
import { migrate } from "./migrations.js";

export type SqlParam = SQLInputValue;
export type Row = Record<string, SQLOutputValue>;

/** Startup failure with a one-line message (corrupt, unopenable or unwritable database). */
export class DatabaseError extends StartupError {
  override name = "DatabaseError";
}

export class Db {
  private readonly statements = new Map<string, StatementSync>();
  private depth = 0;
  private open = true;

  constructor(
    private readonly handle: DatabaseSync,
    readonly path: string,
  ) {}

  private prepare(sql: string): StatementSync {
    let stmt = this.statements.get(sql);
    if (!stmt) {
      stmt = this.handle.prepare(sql);
      this.statements.set(sql, stmt);
    }
    return stmt;
  }

  run(sql: string, ...params: SqlParam[]): { changes: number } {
    const result = this.prepare(sql).run(...params);
    return { changes: Number(result.changes) };
  }

  get<T extends object = Row>(sql: string, ...params: SqlParam[]): T | undefined {
    return this.prepare(sql).get(...params) as T | undefined;
  }

  all<T extends object = Row>(sql: string, ...params: SqlParam[]): T[] {
    return this.prepare(sql).all(...params) as T[];
  }

  /** Runs one or more statements without parameters. */
  exec(sql: string): void {
    this.handle.exec(sql);
  }

  /** Runs fn atomically; nested calls become savepoints. */
  transaction<T>(fn: () => T): T {
    const savepoint = `sp${this.depth}`;
    this.handle.exec(this.depth === 0 ? "BEGIN IMMEDIATE" : `SAVEPOINT ${savepoint}`);
    this.depth++;
    try {
      const result = fn();
      this.depth--;
      this.handle.exec(this.depth === 0 ? "COMMIT" : `RELEASE ${savepoint}`);
      return result;
    } catch (err) {
      this.depth--;
      this.handle.exec(this.depth === 0 ? "ROLLBACK" : `ROLLBACK TO ${savepoint}; RELEASE ${savepoint}`);
      throw err;
    }
  }

  get isOpen(): boolean {
    return this.open;
  }

  close(): void {
    if (!this.open) return;
    this.open = false;
    // node:sqlite finalises every prepared statement when the handle closes.
    this.statements.clear();
    this.handle.close();
  }
}

const SQLITE_MAGIC = "SQLite format 3\0";

interface FileHeader {
  userVersion: number;
  wal: boolean;
}

/** Reads the SQLite file header without opening the database. */
function readHeader(path: string): FileHeader | null {
  const fd = openSync(path, "r");
  try {
    const buf = Buffer.alloc(100);
    const n = readSync(fd, buf, 0, 100, 0);
    if (n < 100 || buf.toString("latin1", 0, 16) !== SQLITE_MAGIC) return null;
    // Offsets 18/19: file format write/read version, 2 means WAL. Offset 60: user_version.
    return { userVersion: buf.readUInt32BE(60), wal: buf[18] === 2 || buf[19] === 2 };
  } finally {
    closeSync(fd);
  }
}

function hasTokensTable(path: string): boolean {
  const ro = new DatabaseSync(path, { readOnly: true });
  try {
    return ro.prepare("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'tokens'").get() !== undefined;
  } finally {
    ro.close();
  }
}

function message(err: unknown): string {
  return err instanceof Error ? err.message : String(err);
}

/**
 * Copies a v0 (sql.js era) database to <path>.v0.bak before it is opened for
 * writing. A WAL-mode file was written by this server, so its header
 * user_version may lag behind the WAL and is not a v0 marker.
 */
function backupV0(path: string, log: Logger): void {
  if (statSync(path).size === 0) return;
  const header = readHeader(path);
  if (!header) throw new DatabaseError(`database ${path} is not a SQLite file (left untouched)`);
  if (header.wal || header.userVersion !== 0 || !hasTokensTable(path)) return;
  const backup = `${path}.v0.bak`;
  if (existsSync(backup)) {
    log.info("v0 database backup already exists", { path: backup });
    return;
  }
  copyFileSync(path, backup, constants.COPYFILE_EXCL);
  log.info("copied v0 database before migrating it", { path: backup });
}

function checkWritable(path: string): void {
  const dir = dirname(path);
  try {
    mkdirSync(dir, { recursive: true });
    // WAL needs to create the -wal and -shm files next to the database.
    accessSync(dir, constants.W_OK);
    if (existsSync(path)) accessSync(path, constants.R_OK | constants.W_OK);
  } catch (err) {
    throw new DatabaseError(`database ${path} is not writable: ${message(err)}`);
  }
}

/** Opens (creating if needed) and migrates the database. Throws DatabaseError. */
export function openDatabase(dbPath: string, log: Logger): Db {
  const memory = dbPath === ":memory:";
  const path = memory ? dbPath : resolve(dbPath);
  if (!memory) {
    checkWritable(path);
    try {
      if (existsSync(path)) backupV0(path, log);
    } catch (err) {
      if (err instanceof DatabaseError) throw err;
      throw new DatabaseError(`cannot read database ${path}: ${message(err)} (left untouched)`);
    }
  }

  let handle: DatabaseSync;
  try {
    // Foreign keys stay off until the migrations ran: rebuilding a table with
    // them on would cascade deletes into the tables that reference it.
    handle = new DatabaseSync(path, { timeout: 5000, enableForeignKeyConstraints: false });
  } catch (err) {
    throw new DatabaseError(`cannot open database ${path}: ${message(err)}`);
  }
  const db = new Db(handle, path);
  try {
    if (!memory) {
      const mode = db.get<{ journal_mode: string }>("PRAGMA journal_mode = WAL");
      if (mode?.journal_mode !== "wal") throw new Error(`journal_mode is ${mode?.journal_mode ?? "unknown"}, not wal`);
    }
    db.exec("PRAGMA synchronous = FULL");
    migrate(db, log);
    db.exec("PRAGMA foreign_keys = ON");
    // Proves at startup that writes work, instead of failing on the first request.
    db.transaction(() => db.exec("CREATE TABLE IF NOT EXISTS write_probe (x); DROP TABLE write_probe"));
  } catch (err) {
    db.close();
    if (err instanceof DatabaseError) throw err;
    throw new DatabaseError(`cannot use database ${path}: ${message(err)}`);
  }
  return db;
}
