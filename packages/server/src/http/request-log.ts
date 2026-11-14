import type { Middleware } from "koa";
import type { Logger } from "../log.js";

/**
 * One line per request. The path is logged without its query string, which
 * may carry a token.
 */
export function requestLog(log: Logger): Middleware {
  return async (ctx, next) => {
    const started = performance.now();
    try {
      await next();
    } finally {
      log.info("request", {
        method: ctx.method,
        path: ctx.path,
        status: ctx.status,
        ms: Math.round(performance.now() - started),
      });
    }
  };
}
