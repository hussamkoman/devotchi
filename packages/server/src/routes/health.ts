import Router from "@koa/router";
import { PROTO_VERSION, type HealthResponse, type ServerMode } from "@devotchi/shared";
import { SERVER_VERSION } from "../version.js";

/** Protocol 7.2: GET /health, with / as alias. */
export function healthRoutes(mode: ServerMode, startedAt: number): Router {
  const router = new Router();
  const health = (): HealthResponse => ({
    status: "ok",
    version: SERVER_VERSION,
    proto: PROTO_VERSION,
    mode,
    uptimeS: Math.floor((Date.now() - startedAt) / 1000),
  });
  router.get("/health", (ctx) => {
    ctx.body = health();
  });
  router.get("/", (ctx) => {
    ctx.body = health();
  });
  return router;
}
