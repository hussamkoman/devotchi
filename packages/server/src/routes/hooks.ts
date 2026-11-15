// Claude Code HTTP hooks (protocol section 8): POST /hook/event and the v1
// aliases /hook/permission, /hook/status and /hook/stop.

import Router from "@koa/router";
import type { Middleware } from "koa";
import type { HookReply, PermissionDecision, PermissionDecisionReply, SessionPermissionUpdate } from "@devotchi/shared";
import type { HookBody, HookDispatcher } from "../hooks/dispatcher.js";
import { authOf, requireAuth, requireCli, type Authenticator } from "../http/auth.js";
import { byToken, type RateLimits } from "../http/rate-limit.js";
import { errorFields, type Logger } from "../log.js";
import type { AllowAnswer, DenyChoice, PermissionManager, PermissionOutcome } from "../permissions/manager.js";

export interface HookRouteDeps {
  auth: Authenticator;
  limits: RateLimits;
  dispatcher: HookDispatcher;
  permissions: PermissionManager;
  log: Logger;
}

const NEUTRAL: HookReply = {};

/** Protocol 14.3: the deny messages, by choice. */
export const DENY_MESSAGES: Readonly<Record<DenyChoice | "deny", string>> = {
  deny: "Denied on Devotchi. Do not retry this call; try another way or ask the user.",
  deny_ask_why:
    "Denied on Devotchi for now. The user wants to know why: explain what this call is for and why it is needed, then you may ask again.",
  deny_stop: "Denied on Devotchi. The user asked you to stop.",
  revise:
    "The user read the plan on Devotchi and wants changes. Stay in plan mode and ask the user what to change.",
  decline: "The user declined to answer on Devotchi. Use your best judgment, or ask again later.",
};

function wrap(decision: PermissionDecision): PermissionDecisionReply {
  return { hookSpecificOutput: { hookEventName: "PermissionRequest", decision } };
}

function allowDecision(answer: AllowAnswer): PermissionDecision {
  switch (answer.choice) {
    case null:
      return { behavior: "allow" };
    case "answered":
      return { behavior: "allow", updatedInput: { ...answer.toolInput, answers: answer.answers } };
    case "allow_session":
    case "approve":
    case "approve_accept_edits": {
      const update: SessionPermissionUpdate =
        answer.choice === "allow_session"
          ? {
              type: "addRules",
              rules: [{ toolName: "Bash", ruleContent: answer.ruleContent }],
              behavior: "allow",
              destination: "session",
            }
          : { type: "setMode", mode: answer.choice === "approve" ? "default" : "acceptEdits", destination: "session" };
      const input = answer.choice === "allow_session" ? {} : { updatedInput: answer.toolInput };
      // The one place a reply carries permission updates.
      return { behavior: "allow", ...input, updatedPermissions: [update] } as PermissionDecision;
    }
  }
}

/**
 * The hook reply for an outcome (protocol 8.3, 14.3, 14.4). A decision
 * exists only for an explicit answer of a consumer; everything else is the
 * neutral reply. The JSON is pinned byte for byte by tests: Claude Code
 * treats a reply that fails its schema as no decision.
 */
export function hookReply(outcome: PermissionOutcome): HookReply {
  if (outcome.kind === "allow") return wrap(allowDecision(outcome.answer));
  if (outcome.kind === "deny") {
    const message = DENY_MESSAGES[outcome.choice ?? "deny"];
    return wrap(outcome.choice === "deny_stop" ? { behavior: "deny", message, interrupt: true } : { behavior: "deny", message });
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
