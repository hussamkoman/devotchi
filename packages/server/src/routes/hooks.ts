// Claude Code HTTP hooks (protocol section 8): POST /hook/event and the v1
// aliases /hook/permission, /hook/status and /hook/stop.

import Router from "@koa/router";
import type { Middleware } from "koa";
import type { HookReply } from "@devotchi/shared";
import type { HookBody, HookDispatcher } from "../hooks/dispatcher.js";
import { authOf, requireAuth, requireCli, type Authenticator } from "../http/auth.js";
import { byToken, type RateLimits } from "../http/rate-limit.js";
import { errorFields, type Logger } from "../log.js";
import type { PermissionManager, PermissionOutcome } from "../permissions/manager.js";

export interface HookRouteDeps {
  auth: Authenticator;
  limits: RateLimits;
  dispatcher: HookDispatcher;
  permissions: PermissionManager;
  log: Logger;
}

const NEUTRAL: HookReply = {};

/**
 * The hook reply for an outcome (protocol 8.3). A decision exists only for
 * the two outcomes a consumer's permission_response produces.
 */
export function hookReply(outcome: PermissionOutcome): HookReply {
  if (outcome.kind === "allow") {
    return { hookSpecificOutput: { hookEventName: "PermissionRequest", decision: { behavior: "allow" } } };
  }
  if (outcome.kind === "deny") {
    return {
      hookSpecificOutput: {
        hookEventName: "PermissionRequest",
        decision: { behavior: "deny", message: "Denied on Devotchi" },
      },
    };
  }
  return NEUTRAL;
}

function hookBody(body: unknown): HookBody {
  return typeof body === "object" && body !== null && !Array.isArray(body) ? (body as HookBody) : {};
}

export function hookRoutes(deps: HookRouteDeps): Router {
  const { auth, limits, dispatcher, permissions, log } = deps;
  const router = new Router({ prefix: "/hook" });
  const guard = [requireAuth(auth, { allowQuery: true }), requireCli(), limits.middleware(600, byToken)];

  /** `alias` names the event of a v1 route, used when the body has no hook_event_name. */
  const handler = (alias?: string): Middleware => async (ctx) => {
    const body = hookBody(ctx.request.body);
    const event = typeof body.hook_event_name === "string" ? body.hook_event_name : alias;
    const roomId = authOf(ctx).roomId;
    ctx.body = NEUTRAL;
    try {
      if (event !== "PermissionRequest") {
        dispatcher.handle(roomId, event, body);
        return;
      }
      const ticket = dispatcher.permissionRequest(roomId, body);
      const { id } = ticket;
      if (id !== undefined) {
        const res = ctx.res;
        // The response stream closing before a reply was written is the
        // client abort. The request stream is no signal: it closes as soon
        // as the body has been read.
        res.on("close", () => {
          if (!res.writableEnded) permissions.cancelByHttpAbort(id);
        });
        if (res.destroyed) permissions.cancelByHttpAbort(id);
      }
      ctx.body = hookReply(await ticket.outcome);
    } catch (err) {
      // A hook must never fail Claude Code's tool call; the neutral reply leaves the terminal in charge.
      log.error("hook handler failed", { event: event?.slice(0, 40), ...errorFields(err) });
      ctx.body = NEUTRAL;
    }
  };

  router.post("/event", ...guard, handler());
  router.post("/permission", ...guard, handler("PermissionRequest"));
  router.post("/status", ...guard, handler("PostToolUse"));
  router.post("/stop", ...guard, handler("Stop"));
  return router;
}
