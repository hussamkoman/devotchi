// Per-route sliding window limits (protocol 7.8), in memory, per instance.

import type { Context, Middleware } from "koa";
import { KeyedRateLimiter } from "../limits.js";
import { authOf } from "./auth.js";
import { rateLimited } from "./errors.js";

export type RateKey = (ctx: Context) => string;

/** Client IP: the socket address, or the proxy-appended X-Forwarded-For entry with trustProxy. */
export const byIp: RateKey = (ctx) => `ip:${ctx.ip}`;
/** The authenticated credential; use after requireAuth. */
export const byToken: RateKey = (ctx) => `token:${authOf(ctx).tokenId}`;

/** Owns every limiter of one server instance so their memory can be swept. */
export class RateLimits {
  private readonly limiters: KeyedRateLimiter[] = [];

  constructor(private readonly windowMs: number) {}

  /** A limiter allowing `limit` events per `windows` windows (default one minute). */
  create(limit: number, windows = 1): KeyedRateLimiter {
    const limiter = new KeyedRateLimiter(limit, this.windowMs * windows);
    this.limiters.push(limiter);
    return limiter;
  }

  /** Middleware that counts every request against `limit` per window and key. */
  middleware(limit: number, key: RateKey): Middleware {
    const limiter = this.create(limit);
    return async (ctx, next) => {
      const wait = limiter.hit(key(ctx), Date.now());
      if (wait > 0) throw rateLimited(wait);
      return next();
    };
  }

  sweep(now: number): void {
    for (const limiter of this.limiters) limiter.sweep(now);
  }
}
