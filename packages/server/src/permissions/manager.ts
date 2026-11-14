// Pending permission requests: the state machine of protocol 8.3.
//
// The rule that shapes this module: a request resolves to allow or deny only
// through onResponse(), i.e. a permission_response sent by a consumer. Every
// other path resolves neutral, which leaves Claude Code's own prompt in charge.

import { createHash, randomBytes } from "node:crypto";
import {
  MIN_REPLAY_REMAINING_MS,
  ROOM_MAX_PENDING,
  buildDismiss,
  buildError,
  buildPermission,
  formatToolCall,
  type DismissReason,
  type LastRequest,
  type PendingRequestInfo,
  type PermissionMessage,
  type RequestDetail,
  type RoomMode,
} from "@devotchi/shared";
import type { Db } from "../db/index.js";
import { touchDeviceHold } from "../db/devices.js";
import type { Logger } from "../log.js";
import type { Conn } from "../rooms/conn.js";
import type { Room, RoomRegistry } from "../rooms/registry.js";

export type NeutralDetail = Exclude<RequestDetail, "answered">;

/** How a request ended, from the hook's point of view. */
export type PermissionOutcome =
  | { kind: "allow"; by: string }
  | { kind: "deny"; by: string }
  | { kind: "neutral"; detail: NeutralDetail }
  /** The hook's HTTP client is gone; there is nobody to reply to. */
  | { kind: "none" };

export interface PendingRequest {
  id: string;
  roomId: string;
  sid: string | undefined;
  /** Hook `agent_id`; null for the main conversation. */
  agent: string | null;
  /** Raw hook `tool_name`, compared by the cancelling events. */
  toolName: string;
  /** SHA-256 of the canonical JSON of `tool_input`. */
  inputHash: string;
  /** The frame as first broadcast; replays change only `timeout`. */
  message: PermissionMessage;
  createdAt: number;
  holdMs: number;
  state: "CREATED" | "HELD";
  /** Consumers presenting the request, with the time of their last `holding`. */
  holders: Map<Conn, number>;
  sentTo: Set<Conn>;
  timer: NodeJS.Timeout | null;
  resolve: (outcome: PermissionOutcome) => void;
}

export interface PermissionInput {
  mode: RoomMode;
  holdMs: number;
  sid?: string;
  project?: string;
  agent: string | null;
  toolName: unknown;
  toolInput: unknown;
  cwd?: string;
}

export interface PermissionTicket {
  /** Absent when the request was answered neutrally without being created. */
  id?: string;
  outcome: Promise<PermissionOutcome>;
}

export interface PermissionTiming {
  ackMs: number;
  aliveMs: number;
  holdWriteMs: number;
}

/** Called synchronously for every request outcome, created or not. */
export type ResolutionListener = (room: Room, sid: string | undefined, outcome: PermissionOutcome) => void;

/** A bare allow is not sufficient for these tools (protocol 8.3), so they are left to the terminal. */
const SKIPPED_TOOLS = new Set(["AskUserQuestion", "ExitPlanMode"]);

function dismissReason(detail: NeutralDetail): Exclude<DismissReason, "answered"> {
  if (detail === "timeout" || detail === "cancelled") return detail;
  return "released";
}

function canonical(value: unknown): unknown {
  if (Array.isArray(value)) return value.map(canonical);
  if (typeof value === "object" && value !== null) {
    const sorted: Record<string, unknown> = {};
    for (const key of Object.keys(value).sort()) sorted[key] = canonical((value as Record<string, unknown>)[key]);
    return sorted;
  }
  return value;
}

/** SHA-256 of `tool_input` serialised with sorted object keys, so key order does not matter. */
export function hashToolInput(toolInput: unknown): string {
  return createHash("sha256")
    .update(JSON.stringify(canonical(toolInput ?? null)))
    .digest("hex");
}

export class PermissionManager {
  private readonly byId = new Map<string, PendingRequest>();
  private listener: ResolutionListener = () => undefined;
  private closed = false;

  constructor(
    private readonly rooms: RoomRegistry,
    private readonly db: Db,
    private readonly log: Logger,
    private readonly timing: PermissionTiming,
    private readonly now: () => number = Date.now,
  ) {}

  setResolutionListener(listener: ResolutionListener): void {
    this.listener = listener;
  }

  /**
   * Handles one PermissionRequest hook. The outcome is already settled when
   * the request cannot be shown to anybody (protocol 8.3, first table).
   */
  create(room: Room, input: PermissionInput): PermissionTicket {
    const formatted = formatToolCall(input.toolName, input.toolInput, input.cwd);
    const immediate = (detail: NeutralDetail): PermissionTicket => {
      const outcome: PermissionOutcome = { kind: "neutral", detail };
      this.record(room, formatted.tool, outcome);
      this.listener(room, input.sid, outcome);
      return { outcome: Promise.resolve(outcome) };
    };
    if (this.closed) return immediate("shutdown");
    if (input.mode === "desk") return immediate("desk");
    if (room.consumers.size === 0) return immediate("no_consumer");
    if (typeof input.toolName === "string" && SKIPPED_TOOLS.has(input.toolName)) return immediate("skipped_tool");
    if (room.pending.length >= ROOM_MAX_PENDING) return immediate("room_busy");

    const createdAt = this.now();
    const id = randomBytes(16).toString("hex");
    const message = buildPermission({
      id,
      ...formatted,
      sid: input.sid,
      project: input.project,
      timeout: input.holdMs,
      timestamp: new Date(createdAt).toISOString(),
    });
    let resolve!: (outcome: PermissionOutcome) => void;
    const outcome = new Promise<PermissionOutcome>((r) => (resolve = r));
    const request: PendingRequest = {
      id,
      roomId: room.id,
      sid: input.sid,
      agent: input.agent,
      toolName: typeof input.toolName === "string" ? input.toolName : "",
      inputHash: hashToolInput(input.toolInput),
      message,
      createdAt,
      holdMs: input.holdMs,
      state: "CREATED",
      holders: new Map(),
      sentTo: new Set(room.consumers),
      timer: null,
      resolve,
    };
    room.pending.push(request);
    this.byId.set(id, request);
    this.rooms.broadcastToConsumers(room, message, createdAt);
    this.arm(request);
    return { id, outcome };
  }

  /** Delivery ack and heartbeat (protocol 4.7). Ids not pending in the sender's room are ignored. */
  onHolding(conn: Conn, ids: readonly string[]): void {
    const now = this.now();
    conn.lastHoldAt = now;
    if (conn.tokenKind === "device" && now - (conn.lastHoldWrittenAt ?? -Infinity) >= this.timing.holdWriteMs) {
      this.writeHold(conn, now);
    }
    for (const id of ids) {
      const request = this.byId.get(id);
      if (!request || request.roomId !== conn.roomId) continue;
      request.holders.set(conn, now);
      request.state = "HELD";
      this.arm(request);
    }
  }

  /** The only path to allow or deny: a consumer of the request's room answered. */
  onResponse(conn: Conn, id: string, granted: boolean): void {
    const request = this.pendingFor(conn, id);
    if (!request) return;
    const by = conn.registration?.identity ?? "unknown";
    this.settle(request, granted ? { kind: "allow", by } : { kind: "deny", by });
  }

  onDefer(conn: Conn, id: string): void {
    const request = this.pendingFor(conn, id);
    if (request) this.settle(request, { kind: "neutral", detail: "deferred" });
  }

  /** A closed socket stops being a holder at once. */
  onConsumerGone(conn: Conn, room: Room): void {
    if (conn.lastHoldAt !== null && conn.lastHoldAt !== conn.lastHoldWrittenAt && conn.tokenKind === "device") {
      this.writeHold(conn, conn.lastHoldAt);
    }
    for (const request of [...room.pending]) {
      request.sentTo.delete(conn);
      if (!request.holders.delete(conn)) continue;
      if (request.holders.size === 0) this.settle(request, { kind: "neutral", detail: "holder_lost" });
      else this.arm(request);
    }
  }

  /** Replay after register_ack (protocol 3.3 item 3). */
  onConsumerRegistered(conn: Conn, room: Room): void {
    const now = this.now();
    for (const request of room.pending) {
      const remaining = request.createdAt + request.holdMs - now;
      if (remaining < MIN_REPLAY_REMAINING_MS || request.sentTo.has(conn)) continue;
      request.sentTo.add(conn);
      conn.send({ ...request.message, timeout: Math.round(remaining), timestamp: new Date(now).toISOString() });
    }
  }

  /** PostToolUse / PostToolUseFailure: the oldest request of the session with that tool and input. */
  cancelMatching(room: Room, sid: string | undefined, toolName: string, inputHash: string): number {
    const match = room.pending.find((r) => r.sid === sid && r.toolName === toolName && r.inputHash === inputHash);
    if (!match) return 0;
    this.settle(match, { kind: "neutral", detail: "cancelled" });
    return 1;
  }

  /** PostToolBatch: every request of the session issued by that agent (null is the main conversation). */
  cancelByAgent(room: Room, sid: string | undefined, agent: string | null): number {
    return this.cancelWhere(room, (r) => r.sid === sid && r.agent === agent);
  }

  /** Stop, StopFailure, UserPromptSubmit. */
  cancelMainConversation(room: Room, sid: string | undefined): number {
    return this.cancelByAgent(room, sid, null);
  }

  /** SessionEnd. */
  cancelSession(room: Room, sid: string | undefined): number {
    return this.cancelWhere(room, (r) => r.sid === sid);
  }

  /** The hook's response stream closed before a reply was written. */
  cancelByHttpAbort(id: string): void {
    const request = this.byId.get(id);
    if (request) this.settle(request, { kind: "none" });
  }

  hasPending(room: Room, sid: string | undefined): boolean {
    return room.pending.some((r) => r.sid === sid);
  }

  /** Number of pending requests the connection currently holds (protocol 7.5 `holding`). */
  holdingCount(conn: Conn, room: Room): number {
    let count = 0;
    for (const request of room.pending) if (request.holders.has(conn)) count++;
    return count;
  }

  /** The `pending` array of GET /api/room. */
  describe(room: Room): PendingRequestInfo[] {
    const now = this.now();
    return room.pending.map((request) => {
      const { tool, summary, risk, project, sid } = request.message;
      const info: PendingRequestInfo = {
        id: request.id,
        tool,
        summary,
        risk,
        ageMs: Math.max(0, now - request.createdAt),
        remainingMs: Math.max(0, request.createdAt + request.holdMs - now),
        holders: [...request.holders.keys()].map((conn) => conn.registration?.identity ?? "unknown"),
      };
      if (project !== undefined) info.project = project;
      if (sid !== undefined) info.sid = sid;
      return info;
    });
  }

  /** Hands every pending request back to the terminal; later requests are answered neutrally at once. */
  shutdown(): void {
    this.closed = true;
    for (const request of [...this.byId.values()]) this.settle(request, { kind: "neutral", detail: "shutdown" });
  }

  private cancelWhere(room: Room, predicate: (request: PendingRequest) => boolean): number {
    const matches = room.pending.filter(predicate);
    for (const request of matches) this.settle(request, { kind: "neutral", detail: "cancelled" });
    return matches.length;
  }

  /** The request a response or defer refers to; answers UNKNOWN_REQUEST when there is none in the sender's room. */
  private pendingFor(conn: Conn, id: string): PendingRequest | undefined {
    const request = this.byId.get(id);
    if (request && request.roomId === conn.roomId) return request;
    conn.send(buildError("UNKNOWN_REQUEST", "not pending", id));
    return undefined;
  }

  private writeHold(conn: Conn, at: number): void {
    conn.lastHoldWrittenAt = at;
    touchDeviceHold(this.db, conn.tokenId, at);
  }

  /** One timer per request, set to its next deadline: ACK or HOLD while CREATED, ALIVE or HOLD while HELD. */
  private arm(request: PendingRequest): void {
    if (request.timer) clearTimeout(request.timer);
    const holdAt = request.createdAt + request.holdMs;
    let due = holdAt;
    if (request.state === "CREATED") {
      due = Math.min(due, request.createdAt + this.timing.ackMs);
    } else {
      for (const lastHoldAt of request.holders.values()) due = Math.min(due, lastHoldAt + this.timing.aliveMs);
    }
    request.timer = setTimeout(() => this.onTimer(request), Math.max(1, due - this.now()));
  }

  private onTimer(request: PendingRequest): void {
    request.timer = null;
    if (!this.byId.has(request.id)) return;
    const now = this.now();
    const holdExpired = now >= request.createdAt + request.holdMs;
    if (request.state === "CREATED") {
      if (now >= request.createdAt + this.timing.ackMs) return this.settle(request, { kind: "neutral", detail: "no_ack" });
      if (holdExpired) return this.settle(request, { kind: "neutral", detail: "timeout" });
    } else {
      if (holdExpired) return this.settle(request, { kind: "neutral", detail: "timeout" });
      for (const [conn, lastHoldAt] of request.holders) {
        if (now - lastHoldAt >= this.timing.aliveMs) request.holders.delete(conn);
      }
      if (request.holders.size === 0) return this.settle(request, { kind: "neutral", detail: "holder_lost" });
    }
    // The timer fired ahead of the deadline it was set for.
    this.arm(request);
  }

  /** Every resolution goes through here, once: the first caller removes the request. */
  private settle(request: PendingRequest, outcome: PermissionOutcome): void {
    if (!this.byId.delete(request.id)) return;
    if (request.timer) clearTimeout(request.timer);
    request.timer = null;
    const room = this.rooms.getOrCreate(request.roomId);
    const index = room.pending.indexOf(request);
    if (index >= 0) room.pending.splice(index, 1);

    if (outcome.kind === "allow" || outcome.kind === "deny") {
      this.rooms.broadcastToConsumers(
        room,
        buildDismiss({ id: request.id, reason: "answered", outcome: outcome.kind, by: outcome.by }),
      );
    } else {
      const detail = outcome.kind === "none" ? "cancelled" : outcome.detail;
      this.rooms.broadcastToConsumers(room, buildDismiss({ id: request.id, reason: dismissReason(detail) }));
    }
    this.record(room, request.message.tool, outcome);
    this.log.info("permission request resolved", {
      outcome: outcome.kind === "none" ? "neutral" : outcome.kind,
      detail: detailOf(outcome),
      state: request.state,
      ms: this.now() - request.createdAt,
    });
    this.listener(room, request.sid, outcome);
    request.resolve(outcome);
  }

  private record(room: Room, tool: string, outcome: PermissionOutcome): void {
    const last: LastRequest = {
      at: new Date(this.now()).toISOString(),
      tool,
      outcome: outcome.kind === "allow" || outcome.kind === "deny" ? outcome.kind : "neutral",
      detail: detailOf(outcome),
    };
    if (outcome.kind === "allow" || outcome.kind === "deny") last.by = outcome.by;
    room.lastRequest = last;
  }
}

function detailOf(outcome: PermissionOutcome): RequestDetail {
  if (outcome.kind === "neutral") return outcome.detail;
  return outcome.kind === "none" ? "cancelled" : "answered";
}
