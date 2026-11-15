// Protocol 7.6 (room state and settings), 7.7 (notifications from scripts)
// and the set_mode message of 4.11.

import Router from "@koa/router";
import {
  HOLD_MAX_MS,
  HOLD_MIN_MS,
  LIMITS,
  MAX_URGENCY,
  NOTIFY_EVENT_RE,
  REQUEST_ID_RE,
  ROOM_MODES,
  buildMode,
  canonicalTimeZone,
  buildNotify,
  defaultMoodForEvent,
  isMood,
  utf8ByteLength,
  type Device,
  type NotifyResponse,
  type Room as RoomDto,
  type RoomMode,
  type RoomSettings,
} from "@devotchi/shared";
import type { ResolvedConfig } from "../config.js";
import type { Db } from "../db/index.js";
import { getRoomSettings, updateRoomSettings } from "../db/room-settings.js";
import { authOf, requireAuth, requireCli, type Authenticator } from "../http/auth.js";
import { badRequest, notFound } from "../http/errors.js";
import { objectBody, optionalString } from "../http/fields.js";
import { byToken, type RateLimits } from "../http/rate-limit.js";
import type { PermissionManager } from "../permissions/manager.js";
import type { RoomRegistry } from "../rooms/registry.js";
import type { WsServer } from "../ws/server.js";

export interface RoomRouteDeps {
  db: Db;
  config: ResolvedConfig;
  auth: Authenticator;
  limits: RateLimits;
  rooms: RoomRegistry;
  ws: WsServer;
  permissions: PermissionManager;
  /** The `devices` array of GET /api/room. */
  listDevices: (roomId: string) => Device[];
  /** Protocol 14.9: the room's time zone setting changed. */
  onTimeZoneChanged: (roomId: string) => void;
}

function parseSettings(body: unknown): Partial<RoomSettings> {
  const fields = objectBody(body);
  const changes: Partial<RoomSettings> = {};
  if (fields.mode !== undefined) {
    if (!(ROOM_MODES as readonly unknown[]).includes(fields.mode)) {
      throw badRequest(`mode must be one of ${ROOM_MODES.join(", ")}`);
    }
    changes.mode = fields.mode as RoomMode;
  }
  if (fields.holdMs !== undefined) {
    const holdMs = fields.holdMs;
    if (typeof holdMs !== "number" || !Number.isInteger(holdMs) || holdMs < HOLD_MIN_MS || holdMs > HOLD_MAX_MS) {
      throw badRequest(`holdMs must be an integer in ${HOLD_MIN_MS}..${HOLD_MAX_MS}`);
    }
    changes.holdMs = holdMs;
  }
  if (fields.shareText !== undefined) {
    if (typeof fields.shareText !== "boolean") throw badRequest("shareText must be a boolean");
    changes.shareText = fields.shareText;
  }
  if (fields.timeZone !== undefined) {
    const zone = fields.timeZone;
    const canonical = typeof zone === "string" && zone !== "" ? canonicalTimeZone(zone) : null;
    if (zone !== "" && canonical === null) {
      throw badRequest('timeZone must be an IANA time zone name such as "Europe/Berlin", or ""');
    }
    changes.timeZone = canonical ?? "";
  }
  return changes;
}

export function roomRoutes(deps: RoomRouteDeps): Router {
  const { db, config, auth, limits, rooms, ws, permissions, listDevices, onTimeZoneChanged } = deps;
  const router = new Router();
  const guard = [requireAuth(auth), requireCli()];
  const defaultHoldMs = config.timing.holdMs;

  const applySettings = (roomId: string, changes: Partial<RoomSettings>): RoomSettings => {
    const before = getRoomSettings(db, roomId, defaultHoldMs);
    const after = updateRoomSettings(db, roomId, changes, defaultHoldMs);
    const room = rooms.get(roomId);
    if (room && after.mode !== before.mode) rooms.broadcastToConsumers(room, buildMode(after.mode));
    if (after.timeZone !== before.timeZone) onTimeZoneChanged(roomId);
    return after;
  };

  // Protocol 4.11: the sender gets the `mode` broadcast too, as its confirmation.
  ws.handle("set_mode", (_conn, msg, room) => {
    updateRoomSettings(db, room.id, { mode: msg.mode }, defaultHoldMs);
    rooms.broadcastToConsumers(room, buildMode(msg.mode));
  });

  router.get("/api/room", ...guard, (ctx) => {
    const roomId = authOf(ctx).roomId;
    const room = rooms.get(roomId);
    const seen = room?.diagnostics.dialogWhilePending;
    const body: RoomDto = {
      roomId,
      settings: getRoomSettings(db, roomId, defaultHoldMs),
      peers: room?.peers() ?? { producers: 0, consumers: 0, devices: 0 },
      pending: room ? permissions.describe(room) : [],
      lastRequest: room?.lastRequest ?? null,
      diagnostics: {
        dialogWhilePending: {
          count: seen?.count ?? 0,
          lastSeenAt: seen?.lastSeenAt != null ? new Date(seen.lastSeenAt).toISOString() : null,
        },
      },
      devices: listDevices(roomId),
    };
    ctx.body = body;
  });

  router.get("/api/room/requests/:id", ...guard, (ctx) => {
    const id = ctx.params.id ?? "";
    const detail = REQUEST_ID_RE.test(id) ? permissions.detail(authOf(ctx).roomId, id) : null;
    if (!detail) throw notFound("No such pending request");
    ctx.body = detail;
  });

  router.put("/api/room/settings", ...guard, (ctx) => {
    ctx.body = applySettings(authOf(ctx).roomId, parseSettings(ctx.request.body));
  });

  router.post("/api/notify", ...guard, limits.middleware(30, byToken), (ctx) => {
    const fields = objectBody(ctx.request.body);
    const event = fields.event;
    if (typeof event !== "string" || !NOTIFY_EVENT_RE.test(event) || utf8ByteLength(event) > LIMITS.event) {
      throw badRequest(`event must match [a-z0-9_]+ and be at most ${LIMITS.event} bytes`);
    }
    if (fields.mood !== undefined && !isMood(fields.mood)) throw badRequest("mood is not a known mood");
    const urgency = fields.urgency;
    if (urgency !== undefined && (typeof urgency !== "number" || !Number.isInteger(urgency) || urgency < 0 || urgency > MAX_URGENCY)) {
      throw badRequest(`urgency must be an integer in 0..${MAX_URGENCY}`);
    }
    const source = optionalString(fields, "source");
    const msg = buildNotify({
      event,
      mood: fields.mood ?? defaultMoodForEvent(event),
      detail: optionalString(fields, "detail"),
      urgency,
      source,
      project: source,
      timestamp: new Date().toISOString(),
    });
    // The room is created so that a consumer registering later still gets the mood replayed.
    const body: NotifyResponse = { delivered: rooms.broadcastToConsumers(rooms.getOrCreate(authOf(ctx).roomId), msg) };
    ctx.status = 202;
    ctx.body = body;
  });

  return router;
}
