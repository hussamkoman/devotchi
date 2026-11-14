// JSON request bodies (protocol section 7): Content-Type application/json,
// 16 KB on /api/*, 2 MB on /hook/* (hook bodies carry file contents).

import type { IncomingMessage } from "node:http";
import type { Middleware } from "koa";
import { API_BODY_LIMIT_BYTES, HOOK_BODY_LIMIT_BYTES } from "@devotchi/shared";
import { badRequest, payloadTooLarge } from "./errors.js";

declare module "koa" {
  interface Request {
    /** Parsed JSON body; undefined when the request had none. */
    body?: unknown;
  }
}

export function bodyLimitFor(path: string): number {
  return path === "/hook" || path.startsWith("/hook/") ? HOOK_BODY_LIMIT_BYTES : API_BODY_LIMIT_BYTES;
}

/**
 * Collects the body up to `limit` bytes. Past the limit the rest is read and
 * discarded rather than destroying the socket, so the client still receives
 * the 413 instead of a connection reset.
 */
function readBody(req: IncomingMessage, limit: number): Promise<Buffer> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    let size = 0;
    let overflow = false;
    req.on("data", (chunk: Buffer) => {
      if (overflow) return;
      size += chunk.length;
      if (size > limit) {
        overflow = true;
        chunks.length = 0;
        reject(payloadTooLarge(limit));
        return;
      }
      chunks.push(chunk);
    });
    req.on("end", () => resolve(Buffer.concat(chunks)));
    req.on("error", reject);
  });
}

export function bodyParser(): Middleware {
  return async (ctx, next) => {
    const declared = ctx.get("Content-Length");
    const chunked = ctx.get("Transfer-Encoding") !== "";
    if (!chunked && (declared === "" || declared === "0")) return next();

    const limit = bodyLimitFor(ctx.path);
    if (declared !== "" && Number(declared) > limit) {
      ctx.set("Connection", "close");
      ctx.req.resume();
      throw payloadTooLarge(limit);
    }
    if (!ctx.is("application/json")) {
      ctx.set("Connection", "close");
      ctx.req.resume();
      throw badRequest("Content-Type must be application/json");
    }

    let text: string;
    try {
      text = (await readBody(ctx.req, limit)).toString("utf8");
    } catch (err) {
      ctx.set("Connection", "close");
      throw err;
    }
    if (text.trim() !== "") {
      try {
        ctx.request.body = JSON.parse(text);
      } catch {
        throw badRequest("Request body is not valid JSON");
      }
    }
    return next();
  };
}

/** Rejects a query string that names any parameter twice. */
export function rejectDuplicateQuery(): Middleware {
  return async (ctx, next) => {
    if (ctx.querystring !== "") {
      const seen = new Set<string>();
      for (const key of new URLSearchParams(ctx.querystring).keys()) {
        if (seen.has(key)) throw badRequest(`Query parameter "${key.slice(0, 40)}" is given more than once`);
        seen.add(key);
      }
    }
    return next();
  };
}
