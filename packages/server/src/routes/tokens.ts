// Protocol 7.2 (GET /api/me) and the token routes of 7.3.

import { createHash, timingSafeEqual } from "node:crypto";
import Router from "@koa/router";
import type { Middleware } from "koa";
import {
  TOKEN_LABEL_MAX,
  utf8ByteLength,
  type CreateTokenResponse,
  type ListTokensResponse,
  type MeResponse,
} from "@devotchi/shared";
import type { ResolvedConfig } from "../config.js";
import type { Db } from "../db/index.js";
import { allocateRoomId, deleteToken, insertToken, listTokens } from "../db/tokens.js";
import { authOf, requireAuth, requireCli, requireSession, type Authenticator } from "../http/auth.js";
import { badRequest, notFound, unauthorized } from "../http/errors.js";
import { byIp, type RateLimits } from "../http/rate-limit.js";
import type { WsServer } from "../ws/server.js";

export interface TokenRouteDeps {
  db: Db;
  config: ResolvedConfig;
  auth: Authenticator;
  limits: RateLimits;
  ws: WsServer;
}

const TOKEN_ID_RE = /^[0-9a-f]{8}$/;
const ADMIN_HEADER = "X-Devotchi-Admin";

function digest(value: string): Buffer {
  return createHash("sha256").update(value).digest();
}

/** Constant-time comparison that does not leak the secret's length. */
function secretMatches(given: string, secret: string): boolean {
  return timingSafeEqual(digest(given), digest(secret));
}

function parseLabel(body: unknown): string | null {
  if (body === undefined) return null;
  if (typeof body !== "object" || body === null || Array.isArray(body)) throw badRequest("Body must be a JSON object");
  const label = (body as { label?: unknown }).label;
  if (label === undefined || label === null) return null;
  if (typeof label !== "string") throw badRequest("label must be a string");
  if (utf8ByteLength(label) > TOKEN_LABEL_MAX) throw badRequest(`label must be at most ${TOKEN_LABEL_MAX} bytes`);
  return label;
}

export function tokenRoutes(deps: TokenRouteDeps): Router {
  const { db, config, auth, limits, ws } = deps;
  const router = new Router();
  const authed = requireAuth(auth);

  const mintGuards =
    config.mode === "multi-user"
      ? [authed, requireSession()]
      : [
          (async (ctx, next) => {
            if (config.adminSecret !== undefined && !secretMatches(ctx.get(ADMIN_HEADER), config.adminSecret)) {
              throw unauthorized(`Minting a token requires the ${ADMIN_HEADER} header`);
            }
            return next();
          }) satisfies Middleware,
        ];

  router.post("/api/auth/token", limits.middleware(10, byIp), ...mintGuards, (ctx) => {
    const label = parseLabel(ctx.request.body);
    let minted;
    if (config.mode === "multi-user") {
      const session = authOf(ctx);
      minted = insertToken(db, { roomId: session.roomId, kind: "cli", label, userId: session.userId ?? null });
    } else {
      minted = db.transaction(() => insertToken(db, { roomId: allocateRoomId(db), kind: "cli", label }));
    }
    const body: CreateTokenResponse = { token: minted.token, id: minted.id, roomId: minted.roomId };
    ctx.status = 201;
    ctx.body = body;
  });

  router.get("/api/me", authed, requireCli(), (ctx) => {
    const state = authOf(ctx);
    let body: MeResponse;
    if (state.kind === "session") {
      const user = db.get<{ email: string }>("SELECT email FROM users WHERE id = ?", state.userId ?? "");
      if (!user || state.userId === undefined) throw unauthorized();
      body = { kind: "session", userId: state.userId, email: user.email, roomId: state.roomId, serverMode: config.mode };
    } else {
      body = { kind: "token", roomId: state.roomId, tokenId: state.tokenId, label: state.label ?? null, serverMode: config.mode };
    }
    ctx.body = body;
  });

  router.get("/api/tokens", authed, requireCli(), (ctx) => {
    const body: ListTokensResponse = { tokens: listTokens(db, authOf(ctx).roomId) };
    ctx.body = body;
  });

  router.delete("/api/tokens/:id", authed, requireCli(), (ctx) => {
    const id = ctx.params.id;
    if (!TOKEN_ID_RE.test(id) || !deleteToken(db, authOf(ctx).roomId, id)) throw notFound("No such token");
    ws.closeToken(id);
    ctx.status = 204;
  });

  return router;
}
