// Pending requests (permissions, questions, plans): the state machine of
// protocol 8.3 and 14.
//
// The rule that shapes this module: a request resolves to allow or deny only
// through a consumer's explicit answer (permission_response, decide,
// question_response). Every other path resolves neutral, which leaves Claude
// Code's own prompt in charge. Replies are built from the stored hook input,
// never from text a consumer sent.

import { createHash, randomBytes } from "node:crypto";
import {
  DEADLINE_MIN_MOVE_MS,
  EXTEND_MS,
  MIN_REPLAY_REMAINING_MS,
  REQUEST_MAX_HOLD_MS,
  ROOM_MAX_PENDING,
  answerStrings,
  buildDeadline,
  buildDismiss,
  buildError,
  buildPermission,
  buildPlanFrame,
  buildQuestionFrames,
  checkAnswers,
  deriveGrant,
  formatToolCall,
  questionSpecs,
  summarisePlan,
  type Cap,
  type DecideChoice,
  type DismissChoice,
  type DismissReason,
  type HookToolInput,
  type LastRequest,
  type PendingRequestInfo,
  type PermissionMessage,
  type PlanMessage,
  type QuestionAnswer,
  type QuestionMessage,
  type QuestionSpec,
  type RequestDetail,
  type RequestKind,
  type RoomMode,
  type RoomRequestDetail,
  type ServerFeatures,
  type ServerToConsumerMessage,
  type SessionGrant,
} from "@devotchi/shared";
import type { Db } from "../db/index.js";
import { touchDeviceHold } from "../db/devices.js";
import type { Logger } from "../log.js";
import type { Conn } from "../rooms/conn.js";
import type { Room, RoomRegistry } from "../rooms/registry.js";

export type NeutralDetail = Exclude<RequestDetail, "answered">;

/** What an allow carries into the hook reply. */
export type AllowAnswer =
  /** A plain `permission_response granted:true`. */
  | { choice: null }
  | { choice: "allow_session"; ruleContent: string }
  | { choice: "approve" | "approve_accept_edits"; toolInput: HookToolInput }
  | { choice: "answered"; toolInput: HookToolInput; answers: Record<string, string> };

export type DenyChoice = Extract<DecideChoice, "deny_ask_why" | "deny_stop" | "revise" | "decline">;

/** How a request ended, from the hook's point of view. */
export type PermissionOutcome =
  | { kind: "allow"; by: string; answer: AllowAnswer }
  /** `choice` null is a plain `permission_response granted:false`. */
  | { kind: "deny"; by: string; choice: DenyChoice | null }
  | { kind: "neutral"; detail: NeutralDetail }
  /** The hook's HTTP client is gone; there is nobody to reply to. */
  | { kind: "none" };

interface RequestBase {
  id: string;
  roomId: string;
  sid: string | undefined;
  /** Hook `agent_id`; null for the main conversation. */
  agent: string | null;
  /** Raw hook `tool_name`, compared by the cancelling events. */
  toolName: string;
  /** What PostToolUse / PostToolUseFailure must match to cancel it (protocol 8.3, 14.4). */
  matchKey: string;
  /** The hook's `tool_input`, unchanged. */
  toolInput: unknown;
  tool: string;
  summary: string;
  risk: PermissionMessage["risk"];
  project: string | undefined;
  createdAt: number;
  /** End of the hold; `extend` moves it for questions and plans. */
  deadline: number;
  state: "CREATED" | "HELD";
  /** Consumers presenting the request, with the time of their last `holding`. */
  holders: Map<Conn, number>;
  sentTo: Set<Conn>;
  timer: NodeJS.Timeout | null;
  resolve: (outcome: PermissionOutcome) => void;
}

export interface PermissionRequest extends RequestBase {
  kind: "permission";
  /** The frame of consumers without `decide`; replays change only `timeout` and `timestamp`. */
  phase1: PermissionMessage;
  /** The frame of consumers with `decide` (protocol 14.2). */
  phase2: PermissionMessage;
  cut: boolean;
  grant: SessionGrant | null;
}

export interface QuestionRequest extends RequestBase {
  kind: "question";
  specs: QuestionSpec[];
  frames: QuestionMessage[];
}

export interface PlanRequest extends RequestBase {
  kind: "plan";
  frame: PlanMessage;
}

export type PendingRequest = PermissionRequest | QuestionRequest | PlanRequest;

export interface PermissionInput {
  mode: RoomMode;
  holdMs: number;
  sid?: string;
  project?: string;
  agent: string | null;
  toolName: unknown;
  toolInput: unknown;
  cwd?: string;
  /** Hook `permission_suggestions`. */
  suggestions?: unknown;
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

const QUESTION_TOOL = "AskUserQuestion";
const PLAN_TOOL = "ExitPlanMode";
/** Every plan of a session matches the PostToolUse of ExitPlanMode (protocol 14.4 "Cancelling"). */
const PLAN_MATCH_KEY = "plan";

/** Which choices of `decide` fit which request kind (protocol 14.3). */
const CHOICES_BY_KIND: Readonly<Record<RequestKind, readonly DecideChoice[]>> = {
  permission: ["allow_session", "deny_ask_why", "deny_stop"],
  question: ["decline"],
  plan: ["approve", "approve_accept_edits", "revise", "deny_stop"],
};

/** The cap a consumer needs to answer a request of the kind with `decide` or `question_response`. */
const CAP_BY_KIND: Readonly<Record<RequestKind, Cap>> = { permission: "decide", question: "ask", plan: "plan" };

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

/**
 * The key a later PostToolUse of the same call has. AskUserQuestion compares
 * the `questions` only, because the terminal adds `answers`.
 */
export function matchKey(toolName: unknown, toolInput: unknown): string {
  if (toolName === PLAN_TOOL) return PLAN_MATCH_KEY;
  if (toolName === QUESTION_TOOL) {
    const questions = typeof toolInput === "object" && toolInput !== null ? (toolInput as HookToolInput).questions : null;
    return hashToolInput(questions ?? null);
  }
  return hashToolInput(toolInput);
}

function isObject(value: unknown): value is HookToolInput {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isDeviceLike(conn: Conn): boolean {
  const kind = conn.registration?.kind;
  return kind === "device" || kind === "sim";
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
    private readonly features: ServerFeatures,
    private readonly now: () => number = Date.now,
  ) {}

  setResolutionListener(listener: ResolutionListener): void {
    this.listener = listener;
  }

  /**
   * Handles one PermissionRequest hook. The outcome is already settled when
   * the request cannot be shown to anybody (protocol 8.3, first table; 14.1).
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
    const kind: RequestKind =
      input.toolName === QUESTION_TOOL ? "question" : input.toolName === PLAN_TOOL ? "plan" : "permission";
    const capable = (cap: Cap) => [...room.consumers].some((conn) => conn.caps.has(cap));
    if (kind === "question" && !(this.features.questions && capable("ask"))) return immediate("skipped_tool");
    if (kind === "plan" && !(this.features.plans && capable("plan"))) return immediate("skipped_tool");
    if (room.pending.length >= ROOM_MAX_PENDING) return immediate("room_busy");

    const createdAt = this.now();
    const id = randomBytes(16).toString("hex");
    const base = {
      id,
      sid: input.sid,
      project: input.project,
      timeout: input.holdMs,
      timestamp: new Date(createdAt).toISOString(),
    };
    let resolve!: (outcome: PermissionOutcome) => void;
    const outcome = new Promise<PermissionOutcome>((r) => (resolve = r));
    const common: RequestBase = {
      id,
      roomId: room.id,
      sid: input.sid,
      agent: input.agent,
      toolName: typeof input.toolName === "string" ? input.toolName : "",
      matchKey: matchKey(input.toolName, input.toolInput),
      toolInput: input.toolInput,
      tool: formatted.tool,
      summary: formatted.summary,
      risk: formatted.risk,
      project: input.project,
      createdAt,
      deadline: createdAt + input.holdMs,
      state: "CREATED",
      holders: new Map(),
      sentTo: new Set(),
      timer: null,
      resolve,
    };

    let request: PendingRequest;
    if (kind === "question") {
      const specs = questionSpecs(input.toolInput);
      const frames = specs && buildQuestionFrames(specs, base);
      if (!specs || !frames) return immediate("skipped_tool");
      request = { ...common, kind, specs, frames };
    } else if (kind === "plan") {
      const plan = isObject(input.toolInput) ? input.toolInput.plan : undefined;
      if (typeof plan !== "string" || plan.trim() === "") return immediate("skipped_tool");
      request = { ...common, kind, frame: buildPlanFrame(summarisePlan(plan), base) };
    } else {
      const { tool, summary, details, risk, cmd, desc, reason } = formatted;
      const phase1 = buildPermission({ ...base, tool, summary, details, risk });
      const derived = deriveGrant(
        input.toolName,
        input.toolInput,
        risk,
        formatted.cut === true,
        input.cwd,
        input.suggestions,
        this.features.sessionGrants,
      );
      const phase2 = buildPermission({
        ...base,
        tool,
        summary,
        risk,
        cmd,
        cut: formatted.cut,
        desc,
        reason,
        grant: derived ? { rule: derived.rule, match: derived.match } : undefined,
      });
      // The frame builder drops the grant when it had to shorten the command.
      const cut = phase2.cut === true;
      request = { ...common, kind, phase1, phase2, cut, grant: phase2.grant && derived ? derived : null };
    }

    room.pending.push(request);
    this.byId.set(id, request);
    for (const conn of room.consumers) this.deliver(request, conn, createdAt, true);
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

  /** `permission_response`: for permissions only (protocol 14.3). */
  onResponse(conn: Conn, id: string, granted: boolean): void {
    const request = this.pendingFor(conn, id);
    if (!request) return;
    if (request.kind !== "permission") {
      conn.send(buildError("INVALID_MESSAGE", `a ${request.kind} is not answered with permission_response`, id));
      return;
    }
    if (granted && this.refusesCutAllow(conn, request)) return;
    const by = this.identity(conn);
    this.settle(request, granted ? { kind: "allow", by, answer: { choice: null } } : { kind: "deny", by, choice: null });
  }

  /** `decide` (protocol 14.3, 14.5). */
  onDecide(conn: Conn, id: string, choice: DecideChoice): void {
    const request = this.pendingFor(conn, id);
    if (!request) return;
    if (!conn.caps.has(CAP_BY_KIND[request.kind])) {
      conn.send(buildError("NOT_ALLOWED", `decide on a ${request.kind} needs the ${CAP_BY_KIND[request.kind]} cap`, id));
      return;
    }
    if (!CHOICES_BY_KIND[request.kind].includes(choice)) {
      conn.send(buildError("INVALID_MESSAGE", `${choice} does not answer a ${request.kind}`, id));
      return;
    }
    const by = this.identity(conn);
    if (request.kind === "permission") {
      if (choice === "allow_session") {
        if (this.refusesCutAllow(conn, request)) return;
        if (!request.grant) {
          conn.send(buildError("INVALID_MESSAGE", "this request offers no session grant", id));
          return;
        }
        this.settle(request, { kind: "allow", by, answer: { choice, ruleContent: request.grant.ruleContent } });
        return;
      }
    } else if (request.kind === "plan" && (choice === "approve" || choice === "approve_accept_edits")) {
      const { more, cut } = request.frame;
      const complete = more === 0 && cut !== true;
      if (isDeviceLike(conn) && (more > 0 || (choice === "approve_accept_edits" && !complete))) {
        const why = more > 0 ? "the device did not show every step" : "the device did not show the whole plan";
        conn.send(buildError("NOT_ALLOWED", `${choice} is not allowed: ${why}`, id));
        return;
      }
      this.settle(request, { kind: "allow", by, answer: { choice, toolInput: this.originalInput(request) } });
      return;
    }
    this.settle(request, { kind: "deny", by, choice: choice as DenyChoice });
  }

  /** `question_response` (protocol 14.4): indexes only; the strings come from the stored input. */
  onQuestionResponse(conn: Conn, id: string, answers: readonly QuestionAnswer[]): void {
    const request = this.pendingFor(conn, id);
    if (!request) return;
    if (request.kind !== "question") {
      conn.send(buildError("INVALID_MESSAGE", `a ${request.kind} is not answered with question_response`, id));
      return;
    }
    if (!conn.caps.has("ask")) {
      conn.send(buildError("NOT_ALLOWED", "question_response needs the ask cap", id));
      return;
    }
    const problem = checkAnswers(request.specs, answers);
    if (problem !== null) {
      conn.send(buildError("INVALID_MESSAGE", problem, id));
      return;
    }
    this.settle(request, {
      kind: "allow",
      by: this.identity(conn),
      answer: { choice: "answered", toolInput: this.originalInput(request), answers: answerStrings(request.specs, answers) },
    });
  }

  /** `extend` (protocol 14.6). Permissions, unknown ids and consumers without `ask`/`plan` are ignored. */
  onExtend(conn: Conn, id: string): void {
    const request = this.byId.get(id);
    if (!request || request.roomId !== conn.roomId || request.kind === "permission") return;
    if (!conn.caps.has(CAP_BY_KIND[request.kind])) return;
    const now = this.now();
    const deadline = Math.min(Math.max(request.deadline, now + EXTEND_MS), request.createdAt + REQUEST_MAX_HOLD_MS);
    if (deadline - request.deadline < DEADLINE_MIN_MOVE_MS) return;
    request.deadline = deadline;
    const msg = buildDeadline(id, deadline - now);
    for (const target of request.sentTo) target.send(msg);
    this.arm(request);
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

  /** Replay after register_ack (protocol 3.3 item 3), in the shape that fits the consumer. */
  onConsumerRegistered(conn: Conn, room: Room): void {
    const now = this.now();
    for (const request of room.pending) {
      if (request.deadline - now < MIN_REPLAY_REMAINING_MS || request.sentTo.has(conn)) continue;
      this.deliver(request, conn, now, false);
    }
  }

  /**
   * PostToolUse / PostToolUseFailure: the oldest request of the session with
   * that tool and input; for ExitPlanMode every plan of the session.
   */
  cancelMatching(room: Room, sid: string | undefined, toolName: string, toolInput: unknown): number {
    const key = matchKey(toolName, toolInput);
    const matches = room.pending.filter((r) => r.sid === sid && r.toolName === toolName && r.matchKey === key);
    const cancelled = toolName === PLAN_TOOL ? matches : matches.slice(0, 1);
    for (const request of cancelled) this.settle(request, { kind: "neutral", detail: "cancelled" });
    return cancelled.length;
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
      const info: PendingRequestInfo = {
        id: request.id,
        kind: request.kind,
        tool: request.tool,
        summary: request.summary,
        risk: request.risk,
        ageMs: Math.max(0, now - request.createdAt),
        remainingMs: Math.max(0, request.deadline - now),
        holders: [...request.holders.keys()].map((conn) => this.identity(conn)),
      };
      if (request.project !== undefined) info.project = request.project;
      if (request.sid !== undefined) info.sid = request.sid;
      return info;
    });
  }

  /** `GET /api/room/requests/:id` (protocol 14.6a); null unless pending in the room. */
  detail(roomId: string, id: string): RoomRequestDetail | null {
    const request = this.byId.get(id);
    if (!request || request.roomId !== roomId) return null;
    return {
      id,
      kind: request.kind,
      tool: request.toolName,
      toolInput: request.toolInput ?? null,
      remainingMs: Math.max(0, request.deadline - this.now()),
    };
  }

  /** Hands every pending request back to the terminal; later requests are answered neutrally at once. */
  shutdown(): void {
    this.closed = true;
    for (const request of [...this.byId.values()]) this.settle(request, { kind: "neutral", detail: "shutdown" });
  }

  /**
   * Protocol 14.2: a device or sim that declared `decide` was told the
   * command is cut and offers no allow for it.
   */
  private refusesCutAllow(conn: Conn, request: PermissionRequest): boolean {
    if (!request.cut || !isDeviceLike(conn) || !conn.caps.has("decide")) return false;
    conn.send(buildError("NOT_ALLOWED", "the command was shortened on the device; allow it in the terminal", request.id));
    return true;
  }

  /** The stored `tool_input`; questions and plans are only created for object inputs. */
  private originalInput(request: PendingRequest): HookToolInput {
    return isObject(request.toolInput) ? request.toolInput : {};
  }

  private identity(conn: Conn): string {
    return conn.registration?.identity ?? "unknown";
  }

  /** The frames of the request in the consumer's shape, or none when it lacks the cap. */
  private framesFor(request: PendingRequest, conn: Conn): ServerToConsumerMessage[] {
    if (request.kind === "permission") return [conn.caps.has("decide") ? request.phase2 : request.phase1];
    if (request.kind === "question") return conn.caps.has("ask") ? request.frames : [];
    return conn.caps.has("plan") ? [request.frame] : [];
  }

  /** First delivery keeps the creation frame as is; a replay carries the remaining hold. */
  private deliver(request: PendingRequest, conn: Conn, now: number, first: boolean): void {
    const frames = this.framesFor(request, conn);
    if (frames.length === 0) return;
    request.sentTo.add(conn);
    for (const frame of frames) {
      if (first) conn.send(frame);
      else conn.send({ ...frame, timeout: Math.round(request.deadline - now), timestamp: new Date(now).toISOString() } as ServerToConsumerMessage);
    }
  }

  private cancelWhere(room: Room, predicate: (request: PendingRequest) => boolean): number {
    const matches = room.pending.filter(predicate);
    for (const request of matches) this.settle(request, { kind: "neutral", detail: "cancelled" });
    return matches.length;
  }

  /** The request an answer or defer refers to; answers UNKNOWN_REQUEST when there is none in the sender's room. */
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
    let due = request.deadline;
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
    const holdExpired = now >= request.deadline;
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
      const choice = choiceOf(outcome);
      this.rooms.broadcastToConsumers(
        room,
        buildDismiss({ id: request.id, reason: "answered", outcome: outcome.kind, by: outcome.by, choice }),
      );
    } else {
      const detail = outcome.kind === "none" ? "cancelled" : outcome.detail;
      this.rooms.broadcastToConsumers(room, buildDismiss({ id: request.id, reason: dismissReason(detail) }));
    }
    this.record(room, request.tool, outcome);
    this.log.info("permission request resolved", {
      kind: request.kind,
      outcome: outcome.kind === "none" ? "neutral" : outcome.kind,
      detail: detailOf(outcome),
      choice: choiceOf(outcome),
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
    if (outcome.kind === "allow" || outcome.kind === "deny") {
      last.by = outcome.by;
      const choice = choiceOf(outcome);
      if (choice !== undefined) last.choice = choice;
    }
    room.lastRequest = last;
  }
}

/** `dismiss.choice`: absent for a plain permission_response. */
function choiceOf(outcome: PermissionOutcome): DismissChoice | undefined {
  if (outcome.kind === "allow") return outcome.answer.choice ?? undefined;
  if (outcome.kind === "deny") return outcome.choice ?? undefined;
  return undefined;
}

function detailOf(outcome: PermissionOutcome): RequestDetail {
  if (outcome.kind === "neutral") return outcome.detail;
  return outcome.kind === "none" ? "cancelled" : "answered";
}
