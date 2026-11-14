// Pairing routes (protocol 7.4) with the throttles of 7.8.

import Router from "@koa/router";
import type { Middleware } from "koa";
import { isValidDeviceId, normalizePairingCode } from "@devotchi/shared";
import { authOf, requireAuth, requireCli, type Authenticator } from "../http/auth.js";
import { HttpError, badRequest, rateLimited } from "../http/errors.js";
import { objectBody, optionalMeta, parseDeviceName, type Fields } from "../http/fields.js";
import { byIp, byToken, type RateLimits } from "../http/rate-limit.js";
import type { KeyedRateLimiter } from "../limits.js";
import type { DeviceMeta, PairingService } from "../pairing/service.js";

export interface PairingRouteDeps {
  auth: Authenticator;
  limits: RateLimits;
  pairing: PairingService;
}

const SECRET_RE = /^[0-9a-f]{32}$/;

function parseCode(fields: Fields): string {
  const code = normalizePairingCode(fields.code);
  if (code === null) throw badRequest("code must be a pairing code");
  return code;
}

function parseMeta(fields: Fields, required: boolean): Partial<DeviceMeta> {
  const meta: Partial<DeviceMeta> = {};
  const deviceId = fields.deviceId;
  if (deviceId !== undefined || required) {
    if (!isValidDeviceId(deviceId)) throw badRequest("deviceId must look like dv-1a2b3c or sim-name");
    meta.deviceId = deviceId;
  }
  const model = optionalMeta(fields, "model");
  if (model !== undefined) meta.model = model;
  const fw = optionalMeta(fields, "fw");
  if (fw !== undefined) meta.fw = fw;
  return meta;
}

/**
 * Refuses with 429 while the IP has used up its failed-code budget, and
 * counts every 404 or 410 of the guarded handler against it.
 */
function failureBudget(failures: KeyedRateLimiter): Middleware {
  return async (ctx, next) => {
    const key = byIp(ctx);
    const wait = failures.retryAfter(key, Date.now());
    if (wait > 0) throw rateLimited(wait);
    try {
      await next();
    } catch (err) {
      if (err instanceof HttpError && (err.status === 404 || err.status === 410)) failures.record(key, Date.now());
      throw err;
    }
  };
}

export function pairingRoutes(deps: PairingRouteDeps): Router {
  const { auth, limits, pairing } = deps;
  const router = new Router();
  const cli = [requireAuth(auth), requireCli()];
  const hourly = 60;

  router.post("/api/pair", ...cli, (ctx) => {
    objectBody(ctx.request.body, true);
    ctx.status = 201;
    ctx.body = pairing.createCode(authOf(ctx).roomId);
  });

  router.post("/api/pair/claim", limits.middleware(10, byIp), failureBudget(limits.create(20, hourly)), (ctx) => {
    const fields = objectBody(ctx.request.body);
    const code = parseCode(fields);
    const meta = parseMeta(fields, false);
    ctx.status = 201;
    ctx.body = pairing.claim(code, meta);
  });

  router.post("/api/device/pair/start", limits.middleware(6, byIp), (ctx) => {
    const meta = parseMeta(objectBody(ctx.request.body), true) as DeviceMeta;
    ctx.status = 201;
    ctx.body = pairing.start(meta);
  });

  router.post("/api/device/pair/poll", limits.middleware(60, byIp), (ctx) => {
    const secret = objectBody(ctx.request.body).deviceSecret;
    if (typeof secret !== "string" || !SECRET_RE.test(secret)) throw badRequest("deviceSecret must be 32 hex characters");
    const result = pairing.poll(secret);
    ctx.status = "status" in result ? 202 : 200;
    ctx.body = result;
  });

  // The per-token limit alone would not help: single-user tokens are free to mint.
  router.post(
    "/api/pair/confirm",
    limits.middleware(10, byIp),
    ...cli,
    limits.middleware(10, byToken),
    failureBudget(limits.create(100, hourly)),
    (ctx) => {
      const fields = objectBody(ctx.request.body);
      const code = parseCode(fields);
      const name = fields.name === undefined ? undefined : parseDeviceName(fields.name);
      ctx.body = pairing.confirm(authOf(ctx).roomId, code, name);
    },
  );

  return router;
}
