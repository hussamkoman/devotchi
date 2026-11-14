// Every HTTP failure is answered as {"error": <message>, "code": <code>}
// (protocol section 7). Stack traces never reach the client.

import type { Middleware } from "koa";
import type { ApiErrorCode } from "@devotchi/shared";
import { errorFields, type Logger } from "../log.js";

export class HttpError extends Error {
  constructor(
    readonly status: number,
    readonly code: ApiErrorCode,
    message: string,
    /** Extra response headers, e.g. Retry-After. */
    readonly headers: Record<string, string> = {},
  ) {
    super(message);
  }
}

export const badRequest = (message: string) => new HttpError(400, "invalid_request", message);
export const unauthorized = (message = "Invalid token") => new HttpError(401, "unauthorized", message);
export const forbidden = (message: string) => new HttpError(403, "forbidden", message);
export const notFound = (message = "Not found") => new HttpError(404, "not_found", message);
export const payloadTooLarge = (limit: number) =>
  new HttpError(413, "payload_too_large", `Request body exceeds ${limit} bytes`);

export function rateLimited(retryAfterMs: number): HttpError {
  const seconds = Math.max(1, Math.ceil(retryAfterMs / 1000));
  return new HttpError(429, "rate_limited", "Too many requests", { "Retry-After": String(seconds) });
}

/** Status codes Koa or its dependencies may raise themselves (http-errors). */
const STATUS_CODES: Record<number, ApiErrorCode> = {
  400: "invalid_request",
  401: "unauthorized",
  403: "forbidden",
  404: "not_found",
  405: "not_found",
  409: "conflict",
  413: "payload_too_large",
  415: "invalid_request",
  429: "rate_limited",
};

export function errorMiddleware(log: Logger): Middleware {
  return async (ctx, next) => {
    try {
      await next();
      if (ctx.status === 404 && ctx.body === undefined) throw notFound();
    } catch (err) {
      let status = 500;
      let body: { error: string; code: ApiErrorCode } = { error: "Internal server error", code: "internal" };
      if (err instanceof HttpError) {
        status = err.status;
        body = { error: err.message, code: err.code };
        for (const [name, value] of Object.entries(err.headers)) ctx.set(name, value);
      } else {
        const exposed = err as { status?: unknown; expose?: unknown; message?: unknown };
        const code = typeof exposed.status === "number" ? STATUS_CODES[exposed.status] : undefined;
        if (code && exposed.expose === true) {
          status = exposed.status === 405 ? 404 : (exposed.status as number);
          body = { error: String(exposed.message), code };
        } else {
          log.error("unhandled error", { method: ctx.method, path: ctx.path, ...errorFields(err) });
        }
      }
      ctx.status = status;
      ctx.type = "application/json";
      ctx.body = body;
    }
  };
}
