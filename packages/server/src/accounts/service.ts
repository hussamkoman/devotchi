// Multi-user accounts (architecture 2.5): users with bcrypt password hashes,
// each owning one room, and opaque login sessions stored as SHA-256 hashes.

import { createHash, randomBytes } from "node:crypto";
import bcrypt from "bcryptjs";
import {
  EMAIL_RE,
  PASSWORD_MAX_BYTES,
  PASSWORD_MIN_BYTES,
  SESSION_TOKEN_PREFIX,
  utf8ByteLength,
  type LoginResponse,
} from "@devotchi/shared";
import type { Db } from "../db/index.js";
import { allocateRoomId } from "../db/tokens.js";
import type { AuthState } from "../http/auth.js";

export interface AccountTiming {
  loginSessionTtlMs: number;
  lastUsedWriteMs: number;
}

/** Longest address SMTP allows; anything longer is not an email. */
const EMAIL_MAX_BYTES = 254;

const sha256 = (value: string) => createHash("sha256").update(value).digest("hex");

/** Trimmed and lower-cased (protocol 7.3); null when it is not an email address. */
export function normaliseEmail(value: string): string | null {
  const email = value.trim().toLowerCase();
  return EMAIL_RE.test(email) && utf8ByteLength(email) <= EMAIL_MAX_BYTES ? email : null;
}

export function isAcceptablePassword(password: string): boolean {
  const bytes = utf8ByteLength(password);
  return bytes >= PASSWORD_MIN_BYTES && bytes <= PASSWORD_MAX_BYTES;
}

export class EmailTakenError extends Error {}

interface SessionRow {
  id: string;
  user_id: string;
  expires_at: string;
  last_used: string | null;
  room_id: string;
}

export class AccountService {
  /** Compared against when the email is unknown, so both login paths cost one bcrypt compare. */
  private readonly dummyHash: Promise<string>;

  constructor(
    private readonly db: Db,
    private readonly cost: number,
    private readonly timing: AccountTiming,
  ) {
    this.dummyHash = bcrypt.hash(randomBytes(16).toString("hex"), cost);
  }

  /** Creates a user with a new room. Throws EmailTakenError. */
  async register(email: string, password: string): Promise<string> {
    if (this.db.get("SELECT 1 FROM users WHERE email = ?", email)) throw new EmailTakenError();
    const hash = await bcrypt.hash(password, this.cost);
    const id = randomBytes(8).toString("hex");
    try {
      this.db.transaction(() => {
        this.db.run(
          "INSERT INTO users (id, email, password_hash, room_id, created_at) VALUES (?, ?, ?, ?, ?)",
          id,
          email,
          hash,
          allocateRoomId(this.db),
          new Date().toISOString(),
        );
      });
    } catch (err) {
      // A concurrent registration of the same address won the race while this one was hashing.
      if (this.db.get("SELECT 1 FROM users WHERE email = ?", email)) throw new EmailTakenError();
      throw err;
    }
    return id;
  }

  /**
   * Checks the password with exactly one bcrypt compare whether or not the
   * email exists. Returns a new session, or null.
   */
  async login(email: string | null, password: string, now = Date.now()): Promise<LoginResponse | null> {
    const user =
      email === null
        ? undefined
        : this.db.get<{ id: string; password_hash: string }>("SELECT id, password_hash FROM users WHERE email = ?", email);
    // bcrypt ignores bytes after the 72nd, so a longer password could match a shorter one.
    const usable = user !== undefined && utf8ByteLength(password) <= PASSWORD_MAX_BYTES;
    const matched = await bcrypt.compare(password, usable ? user.password_hash : await this.dummyHash);
    if (!usable || !matched) return null;

    const sessionToken = SESSION_TOKEN_PREFIX + randomBytes(32).toString("base64url");
    const expiresAt = new Date(now + this.timing.loginSessionTtlMs).toISOString();
    this.db.run(
      "INSERT INTO sessions (id, user_id, token_hash, created_at, expires_at, last_used) VALUES (?, ?, ?, ?, ?, ?)",
      randomBytes(8).toString("hex"),
      user.id,
      sha256(sessionToken),
      new Date(now).toISOString(),
      expiresAt,
      new Date(now).toISOString(),
    );
    return { sessionToken, userId: user.id, expiresAt };
  }

  /** The session resolver installed into the Authenticator. */
  resolveSession(raw: string, now: number): AuthState | null {
    const row = this.db.get<SessionRow>(
      `SELECT s.id, s.user_id, s.expires_at, s.last_used, u.room_id
       FROM sessions s JOIN users u ON u.id = s.user_id WHERE s.token_hash = ?`,
      sha256(raw),
    );
    if (!row || Date.parse(row.expires_at) <= now) return null;
    const last = row.last_used === null ? NaN : Date.parse(row.last_used);
    if (Number.isNaN(last) || now - last >= this.timing.lastUsedWriteMs) {
      this.db.run("UPDATE sessions SET last_used = ? WHERE id = ?", new Date(now).toISOString(), row.id);
    }
    return { kind: "session", tokenId: row.id, roomId: row.room_id, userId: row.user_id };
  }

  logout(sessionId: string): void {
    this.db.run("DELETE FROM sessions WHERE id = ?", sessionId);
  }

  prune(now = Date.now()): void {
    this.db.run("DELETE FROM sessions WHERE expires_at <= ?", new Date(now).toISOString());
  }
}
