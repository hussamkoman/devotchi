// Claude Code hook events (protocol 8.2-8.5): session bookkeeping, the
// messages each event emits, request cancellation and the dialog diagnosis.
// Everything here is synchronous; only a held PermissionRequest waits.

import { createHash } from "node:crypto";
import {
  LIMITS,
  buildNotify,
  buildStatus,
  formatToolCall,
  isOwnMcpTool,
  sanitizeAscii,
  utf8Truncate,
  type Mood,
  type NotifyInput,
} from "@devotchi/shared";
import type { ResolvedConfig } from "../config.js";
import type { Db } from "../db/index.js";
import { getRoomSettings } from "../db/room-settings.js";
import type { PermissionManager, PermissionTicket } from "../permissions/manager.js";
import type { Room, RoomRegistry, RoomSession } from "../rooms/registry.js";

export type HookBody = Record<string, unknown>;

export interface HookDispatcherDeps {
  config: ResolvedConfig;
  db: Db;
  rooms: RoomRegistry;
  permissions: PermissionManager;
}

/** Fields every hook input carries (protocol 8.2). */
interface HookSession {
  /** Key of the session in `room.sessions`; "" when the hook sent no session_id. */
  key: string;
  sid: string | undefined;
  project: string | undefined;
  agent: string | null;
  cwd: string | undefined;
}

interface StatusThrottle {
  lastSentAt: number;
  queued: { msg: string; sid?: string; project?: string } | null;
  timer: NodeJS.Timeout | null;
}

/** A PostToolUse status may replace these moods with `working`; any other mood was set on purpose and stays. */
const MOODS_REPLACED_BY_WORKING: ReadonlySet<Mood> = new Set(["thinking", "idle", "sleeping"]);
const FAILURES_BEFORE_NOTIFY = 3;
const STOP_TEXT_BYTES = 120;

function str(value: unknown): string | undefined {
  return typeof value === "string" && value !== "" ? value : undefined;
}

function hookSession(body: HookBody): HookSession {
  const sessionId = str(body.session_id);
  const cwd = str(body.cwd);
  const base = cwd?.split(/[\\/]/).filter((part) => part !== "").pop();
  const project = base === undefined ? undefined : utf8Truncate(sanitizeAscii(base), LIMITS.project);
  const sid = sessionId === undefined ? undefined : createHash("sha256").update(sessionId).digest("hex").slice(0, 6);
  return {
    key: sid ?? "",
    sid,
    project: project === "" ? undefined : project,
    agent: str(body.agent_id) ?? null,
    cwd,
  };
}

function formatDuration(ms: number): string {
  const seconds = Math.max(0, Math.round(ms / 1000));
  return `${Math.floor(seconds / 60)}m${String(seconds % 60).padStart(2, "0")}s`;
}

export class HookDispatcher {
  private readonly throttles = new WeakMap<Room, StatusThrottle>();
  private readonly timers = new Set<NodeJS.Timeout>();

  constructor(private readonly deps: HookDispatcherDeps) {
    // A neutral reply means a terminal dialog for that call may now be open
    // (protocol 8.4). Recorded synchronously so that the event which caused
    // the resolution can clear the flag again afterwards.
    deps.permissions.setResolutionListener((room, sid, outcome) => {
      const session = room.sessions.get(sid ?? "");
      if (session && outcome.kind === "neutral") session.dialogMaybeOpen = true;
    });
  }

  /** PermissionRequest: the only event whose reply may be held. */
  permissionRequest(roomId: string, body: HookBody): PermissionTicket {
    const { config, db, rooms, permissions } = this.deps;
    const room = rooms.getOrCreate(roomId);
    const hook = hookSession(body);
    this.touch(room, hook);
    const settings = getRoomSettings(db, roomId, config.timing.holdMs);
    return permissions.create(room, {
      mode: settings.mode,
      holdMs: settings.holdMs,
      sid: hook.sid,
      project: hook.project,
      agent: hook.agent,
      toolName: body.tool_name,
      toolInput: body.tool_input,
      cwd: hook.cwd,
      suggestions: body.permission_suggestions,
    });
  }

  /** Every other event (protocol 8.5). Unknown events only refresh the session. */
  handle(roomId: string, event: string | undefined, body: HookBody): void {
    const { rooms, permissions } = this.deps;
    const room = rooms.getOrCreate(roomId);
    const hook = hookSession(body);
    const now = Date.now();
    const session = this.touch(room, hook, now);

    switch (event) {
      case "UserPromptSubmit":
        permissions.cancelMainConversation(room, hook.sid);
        session.dialogMaybeOpen = false;
        session.lastPromptAt = now;
        this.dropQueuedStatus(room);
        // The prompt text is never forwarded.
        this.emitStatus(room, hook, "Thinking...", "thinking");
        return;

      case "PostToolUse":
        session.failures = 0;
        this.cancelMatching(room, hook, body);
        session.dialogMaybeOpen = false;
        if (!isOwnMcpTool(body.tool_name)) {
          this.queueToolStatus(room, hook, formatToolCall(body.tool_name, body.tool_input, hook.cwd).summary, now);
        }
        return;

      case "PostToolUseFailure": {
        this.cancelMatching(room, hook, body);
        session.dialogMaybeOpen = false;
        if (!isOwnMcpTool(body.tool_name)) {
          this.dropQueuedStatus(room);
          const { tool } = formatToolCall(body.tool_name, body.tool_input, hook.cwd);
          this.emitStatus(room, hook, `${tool} failed`);
        }
        if (++session.failures >= FAILURES_BEFORE_NOTIFY) {
          session.failures = 0;
          this.emitNotify(room, hook, {
            event: "error",
            mood: "angry",
            detail: `${FAILURES_BEFORE_NOTIFY} tool failures in a row`,
            urgency: 2,
          });
        }
        return;
      }

      case "PostToolBatch":
        permissions.cancelByAgent(room, hook.sid, hook.agent);
        session.dialogMaybeOpen = false;
        return;

      case "Notification":
        this.onNotification(room, hook, session, body, now);
        return;

      case "Stop":
        permissions.cancelMainConversation(room, hook.sid);
        session.dialogMaybeOpen = false;
        this.dropQueuedStatus(room);
        this.onStop(room, hook, session, body, now);
        return;

      case "StopFailure":
        permissions.cancelMainConversation(room, hook.sid);
        session.dialogMaybeOpen = false;
        this.dropQueuedStatus(room);
        this.emitNotify(room, hook, {
          event: "error",
          mood: "sad",
          detail: utf8Truncate(str(body.error) ?? "unknown", STOP_TEXT_BYTES),
          urgency: 2,
        });
        return;

      case "SessionEnd":
        permissions.cancelSession(room, hook.sid);
        room.sessions.delete(hook.key);
        return;

      default:
        return;
    }
  }

  /** Stops the trailing status timers. */
  shutdown(): void {
    for (const timer of this.timers) clearTimeout(timer);
    this.timers.clear();
  }

  private touch(room: Room, hook: HookSession, now = Date.now()): RoomSession {
    let session = room.sessions.get(hook.key);
    if (session && now - session.lastSeen >= this.deps.config.timing.sessionTtlMs) session = undefined;
    if (!session) {
      session = { sid: hook.key, lastSeen: now, lastPromptAt: null, failures: 0, dialogMaybeOpen: false };
      room.sessions.set(hook.key, session);
    }
    session.lastSeen = now;
    if (hook.project !== undefined) session.project = hook.project;
    return session;
  }

  private cancelMatching(room: Room, hook: HookSession, body: HookBody): void {
    // Hashing a tool_input of up to 2 MB is only worth it when something could match.
    if (!this.deps.permissions.hasPending(room, hook.sid)) return;
    const toolName = typeof body.tool_name === "string" ? body.tool_name : "";
    this.deps.permissions.cancelMatching(room, hook.sid, toolName, body.tool_input);
  }

  private onNotification(room: Room, hook: HookSession, session: RoomSession, body: HookBody, now: number): void {
    const type = body.notification_type;
    if (type === "idle_prompt" || type === "agent_needs_input") {
      this.emitNotify(room, hook, { event: "needs_input", mood: "anxious", detail: "Claude is waiting", urgency: 3 });
      return;
    }
    if (type !== "permission_prompt") return;
    if (!this.deps.permissions.hasPending(room, hook.sid)) {
      this.emitNotify(room, hook, {
        event: "needs_input",
        mood: "anxious",
        detail: "Permission needed in terminal",
        urgency: 2,
      });
      return;
    }
    // A dialog is on screen while the hook is still pending, unless it belongs
    // to an earlier call of the session that Devotchi already handed back.
    if (!session.dialogMaybeOpen) {
      const seen = room.diagnostics.dialogWhilePending;
      seen.count++;
      seen.lastSeenAt = now;
    }
  }

  private onStop(room: Room, hook: HookSession, session: RoomSession, body: HookBody, now: number): void {
    const background = Array.isArray(body.background_tasks) ? body.background_tasks.length : 0;
    if (background > 0) {
      this.emitNotify(room, hook, {
        event: "task_paused",
        mood: "idle",
        detail: `${background} background ${background === 1 ? "task" : "tasks"}`,
        urgency: 1,
      });
      return;
    }
    const duration = session.lastPromptAt === null ? undefined : formatDuration(now - session.lastPromptAt);
    let detail = duration === undefined ? "Done" : `Done (${duration})`;
    const { config, db } = this.deps;
    if (typeof body.last_assistant_message === "string" && getRoomSettings(db, room.id, config.timing.holdMs).shareText) {
      const firstLine = body.last_assistant_message
        .split("\n")
        .map((line) => line.trim())
        .find((line) => line !== "");
      if (firstLine !== undefined) {
        const text = utf8Truncate(firstLine, STOP_TEXT_BYTES);
        detail = duration === undefined ? text : `${text} (${duration})`;
      }
    }
    this.emitNotify(room, hook, { event: "task_complete", mood: "sleeping", detail, urgency: 2 });
  }

  private emitNotify(room: Room, hook: HookSession, input: Omit<NotifyInput, "sid" | "project" | "timestamp">): void {
    this.deps.rooms.broadcastToConsumers(
      room,
      buildNotify({ ...input, sid: hook.sid, project: hook.project, timestamp: new Date().toISOString() }),
    );
  }

  private emitStatus(room: Room, at: { sid?: string; project?: string }, msg: string, mood?: Mood): void {
    this.deps.rooms.broadcastToConsumers(
      room,
      buildStatus({ msg, mood, sid: at.sid, project: at.project, timestamp: new Date().toISOString() }),
    );
  }

  /** PostToolUse statuses: at most one per interval and room; the newest waiting one wins. */
  private queueToolStatus(room: Room, hook: HookSession, msg: string, now: number): void {
    let throttle = this.throttles.get(room);
    if (!throttle) this.throttles.set(room, (throttle = { lastSentAt: -Infinity, queued: null, timer: null }));
    const state = throttle;
    const entry = { msg, sid: hook.sid, project: hook.project };
    const wait = state.lastSentAt + this.deps.config.timing.hookStatusIntervalMs - now;
    if (wait <= 0 && !state.timer) {
      state.lastSentAt = now;
      this.emitToolStatus(room, entry);
      return;
    }
    state.queued = entry;
    if (state.timer) return;
    const timer = setTimeout(() => {
      this.timers.delete(timer);
      state.timer = null;
      const queued = state.queued;
      state.queued = null;
      if (!queued) return;
      state.lastSentAt = Date.now();
      this.emitToolStatus(room, queued);
    }, wait);
    state.timer = timer;
    this.timers.add(timer);
  }

  /** A newer event of another kind describes the room better than a tool status still waiting. */
  private dropQueuedStatus(room: Room): void {
    const throttle = this.throttles.get(room);
    if (throttle) throttle.queued = null;
  }

  private emitToolStatus(room: Room, entry: { msg: string; sid?: string; project?: string }): void {
    // Decided when the frame is sent, not when the hook arrived (protocol 8.5).
    const working = room.mood === null || MOODS_REPLACED_BY_WORKING.has(room.mood.value);
    this.emitStatus(room, entry, entry.msg, working ? "working" : undefined);
  }
}
