// Credentials (protocol 7.1): API tokens looked up by SHA-256 hash, and
// session tokens (multi-user mode) resolved by a pluggable resolver.

import type { Context, Middleware } from "koa";
import { SESSION_TOKEN_PREFIX, type ServerMode, type TokenKind } from "@devotchi/shared";
import type { Db } from "../db/index.js";
import { findTokenByValue, touchToken } from "../db/tokens.js";
import { badRequest, forbidden, unauthorized } from "./errors.js";

export type CredentialKind = TokenKind | "session";

export interface AuthState {
  kind: CredentialKind;
  /** API token id, or the session id for a session. */
  tokenId: string;
  roomId: string;
  userId?: string;
  /** Label of an API token. */
  label?: string | null;
}

/** Resolves a `dvs_` session token; null when unknown or expired. */
export type SessionResolver = (raw: string, now: number) => AuthState | null;

const BEARER_RE = /^Bearer[ \t]+(\S+)[ \t]*$/i;

export class Authenticator {
  private resolveSession: SessionResolver = () => null;

  constructor(
    private readonly db: Db,
    private readonly mode: ServerMode,
    private readonly lastUsedWriteMs: number,
  ) {}

  /** Installs the multi-user session lookup; without one every session is rejected. */
  setSessionResolver(resolver: SessionResolver): void {
    this.resolveSession = resolver;
  }

  authenticate(raw: string, now = Date.now()): AuthState | null {
    if (raw.startsWith(SESSION_TOKEN_PREFIX)) {
      return this.mode === "multi-user" ? this.resolveSession(raw, now) : null;
    }
    const row = findTokenByValue(this.db, raw);
    if (!row) return null;
    touchToken(this.db, row, now, this.lastUsedWriteMs);
    const state: AuthState = { kind: row.kind, tokenId: row.id, roomId: row.room_id, label: row.label };
    if (row.user_id !== null) state.userId = row.user_id;
    return state;
  }
}

export type Credential = { ok: true; value: string } | { ok: false; reason: "missing" | "malformed" };

/**
 * The credential of a request: `Authorization: Bearer`, or `?token=` where
 * the route allows it (hooks and WebSocket upgrades only).
 */
export function extractCredential(
  authorization: string | undefined,
  queryToken: string | string[] | undefined,
  allowQuery: boolean,
): Credential {
  if (authorization !== undefined && authorization !== "") {
    const match = BEARER_RE.exec(authorization);
    return match ? { ok: true, value: match[1] } : { ok: false, reason: "malformed" };
  }
  if (allowQuery && queryToken !== undefined) {
    if (typeof queryToken !== "string" || queryToken === "") return { ok: false, reason: "malformed" };
    return { ok: true, value: queryToken };
  }
  return { ok: false, reason: "missing" };
}

export function authOf(ctx: Context): AuthState {
  const auth = (ctx.state as { auth?: AuthState }).auth;
  if (!auth) throw unauthorized();
  return auth;
}

/** Authenticates the request and stores the result in ctx.state.auth. */
export function requireAuth(auth: Authenticator, options: { allowQuery?: boolean } = {}): Middleware {
  return async (ctx, next) => {
    const query = ctx.query.token;
    if (Array.isArray(query)) throw badRequest('Query parameter "token" is given more than once');
    const credential = extractCredential(ctx.get("Authorization") || undefined, query, options.allowQuery === true);
    if (!credential.ok) throw unauthorized(credential.reason === "missing" ? "Missing token" : "Invalid token");
    const state = auth.authenticate(credential.value);
    if (!state) throw unauthorized();
    (ctx.state as { auth?: AuthState }).auth = state;
    return next();
  };
}

/** A cli token or a session; device tokens are consumer-WebSocket only. */
export function requireCli(): Middleware {
  return async (ctx, next) => {
    if (authOf(ctx).kind === "device") throw forbidden("Device tokens cannot use this route");
    return next();
  };
}

export function requireSession(): Middleware {
  return async (ctx, next) => {
    if (authOf(ctx).kind !== "session") throw unauthorized("A login session is required");
    return next();
  };
}
