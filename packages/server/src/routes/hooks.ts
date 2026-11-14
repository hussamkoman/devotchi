// Claude Code HTTP hooks (protocol section 8): the v1 routes
// /hook/permission, /hook/status and /hook/stop.

import { randomBytes } from "node:crypto";
import Router from "@koa/router";
import {
  buildDismiss,
  buildError,
  buildNotify,
  buildPermission,
  buildStatus,
  formatToolCall,
  type HookReply,
} from "@devotchi/shared";
import type { ResolvedConfig } from "../config.js";
import { authOf, requireAuth, requireCli, type Authenticator } from "../http/auth.js";
import { byToken, type RateLimits } from "../http/rate-limit.js";
import type { Logger } from "../log.js";
import type { RoomRegistry } from "../rooms/registry.js";
import type { WsServer } from "../ws/server.js";

export interface HookRouteDeps {
  config: ResolvedConfig;
  auth: Authenticator;
  limits: RateLimits;
  rooms: RoomRegistry;
  ws: WsServer;
  log: Logger;
}

interface PendingHook {
  roomId: string;
  timer: NodeJS.Timeout;
  resolve: (reply: HookReply | null) => void;
}

type HookBody = Record<string, unknown>;

function hookBody(body: unknown): HookBody {
  return typeof body === "object" && body !== null && !Array.isArray(body) ? (body as HookBody) : {};
}

export interface HookRoutes {
  router: Router;
  /** Answers every held request with the neutral reply. */
  shutdown(): void;
}

export function hookRoutes(deps: HookRouteDeps): HookRoutes {
  const { config, auth, limits, rooms, ws, log } = deps;
  const router = new Router({ prefix: "/hook" });
  const guard = [requireAuth(auth, { allowQuery: true }), requireCli(), limits.middleware(600, byToken)];
  const pending = new Map<string, PendingHook>();

  ws.handle("permission_response", (conn, msg, room) => {
    const request = pending.get(msg.id);
    if (!request || request.roomId !== room.id) {
      conn.send(buildError("UNKNOWN_REQUEST", "not pending", msg.id));
      return;
    }
    clearTimeout(request.timer);
    pending.delete(msg.id);
    const outcome = msg.granted ? "allow" : "deny";
    rooms.broadcastToConsumers(
      room,
      buildDismiss({ id: msg.id, reason: "answered", outcome, by: conn.registration?.identity ?? "unknown" }),
    );
    request.resolve({
      hookSpecificOutput: {
        hookEventName: "PermissionRequest",
        decision: msg.granted ? { behavior: "allow" } : { behavior: "deny", message: "Denied on Devotchi" },
      },
    });
  });

  router.post("/permission", ...guard, async (ctx) => {
    const roomId = authOf(ctx).roomId;
    const room = rooms.get(roomId);
    if (!room || room.consumers.size === 0) {
      ctx.body = {};
      return;
    }
    const body = hookBody(ctx.request.body);
    const id = randomBytes(16).toString("hex");
    const formatted = formatToolCall(body.tool_name, body.tool_input, typeof body.cwd === "string" ? body.cwd : undefined);
    const holdMs = config.timing.holdMs;
    rooms.broadcastToConsumers(
      room,
      buildPermission({ id, ...formatted, timeout: holdMs, timestamp: new Date().toISOString() }),
    );

    const res = ctx.res;
    const reply = await new Promise<HookReply | null>((resolve) => {
      const timer = setTimeout(() => {
        pending.delete(id);
        rooms.broadcastToConsumers(room, buildDismiss({ id, reason: "timeout" }));
        resolve({});
      }, holdMs);
      pending.set(id, { roomId, timer, resolve });
      // The response stream closing before a reply was written is a client
      // abort; the request stream closes as soon as the body has been read.
      res.on("close", () => {
        if (res.writableEnded || !pending.has(id)) return;
        clearTimeout(timer);
        pending.delete(id);
        rooms.broadcastToConsumers(room, buildDismiss({ id, reason: "cancelled" }));
        resolve(null);
      });
    });
    log.info("permission request resolved", { decided: reply !== null && "hookSpecificOutput" in reply });
    ctx.body = reply ?? {};
  });

  router.post("/status", ...guard, (ctx) => {
    const body = hookBody(ctx.request.body);
    const room = rooms.get(authOf(ctx).roomId);
    if (room) {
      const { summary } = formatToolCall(body.tool_name, body.tool_input);
      rooms.broadcastToConsumers(room, buildStatus({ msg: summary, timestamp: new Date().toISOString() }));
    }
    ctx.body = {};
  });

  router.post("/stop", ...guard, (ctx) => {
    const room = rooms.get(authOf(ctx).roomId);
    if (room) {
      rooms.broadcastToConsumers(
        room,
        buildNotify({ event: "task_complete", mood: "sleeping", urgency: 2, detail: "Done", timestamp: new Date().toISOString() }),
      );
    }
    ctx.body = {};
  });

  return {
    router,
    shutdown() {
      for (const [id, request] of pending) {
        clearTimeout(request.timer);
        pending.delete(id);
        const room = rooms.get(request.roomId);
        if (room) rooms.broadcastToConsumers(room, buildDismiss({ id, reason: "released" }));
        request.resolve({});
      }
    },
  };
}
