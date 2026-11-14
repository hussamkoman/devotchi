import type { Middleware } from "koa";

export function securityHeaders(): Middleware {
  return async (ctx, next) => {
    ctx.set("X-Content-Type-Options", "nosniff");
    ctx.set("X-Frame-Options", "DENY");
    ctx.set("Referrer-Policy", "no-referrer");
    // API answers carry tokens and room state; nothing may be cached.
    ctx.set("Cache-Control", "no-store");
    return next();
  };
}

const ALLOWED_METHODS = "GET, POST, PUT, PATCH, DELETE";
const ALLOWED_HEADERS = "Authorization, Content-Type, X-Devotchi-Admin";

/**
 * Cross-origin access only for the configured origins; with none configured
 * no Access-Control-* header is ever sent, so browsers block other origins.
 */
export function cors(origins: readonly string[]): Middleware {
  const allowed = new Set(origins);
  return async (ctx, next) => {
    const origin = ctx.get("Origin");
    if (origin === "" || !allowed.has(origin)) return next();
    ctx.vary("Origin");
    ctx.set("Access-Control-Allow-Origin", origin);
    if (ctx.method === "OPTIONS" && ctx.get("Access-Control-Request-Method") !== "") {
      ctx.set("Access-Control-Allow-Methods", ALLOWED_METHODS);
      ctx.set("Access-Control-Allow-Headers", ALLOWED_HEADERS);
      ctx.set("Access-Control-Max-Age", "600");
      ctx.status = 204;
      return;
    }
    ctx.set("Access-Control-Expose-Headers", "Retry-After");
    return next();
  };
}
