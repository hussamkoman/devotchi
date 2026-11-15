// `devotchi sim`: a headless virtual device that speaks the device side of
// protocol v2 (pairing, register, holding acks and heartbeats, answers,
// telemetry, dismiss handling). Every event is reported as one JSON object,
// which the CLI prints as one line on stdout. Importable for tests.

import { readFile } from "node:fs/promises";
import WebSocket, { type RawData } from "ws";
import {
  CLOSE_CODES,
  HARD_FRAME_BYTES,
  HEARTBEAT_DEAD_MS,
  HOLDING_ACK_MAX_DELAY_MS,
  HOLDING_INTERVAL_MS,
  HoldingAckScheduler,
  SIM_ID_RE,
  buildDecide,
  buildExtend,
  buildHolding,
  buildPermissionDefer,
  buildPermissionResponse,
  buildPong,
  buildQuestionResponse,
  buildRegister,
  buildTelemetry,
  isCap,
  isMood,
  serialize,
  type BatteryState,
  type Cap,
  type DecideChoice,
  type QuestionAnswer,
  type DevicePairPollResponse,
  type DevicePairStartResponse,
  type Mood,
  type PairClaimResponse,
  type TelemetryMessage,
} from "@devotchi/shared";
import { ApiClient, ApiError } from "./api.js";
import { serverWsUrl } from "./config.js";
import { CliError, UsageError, errorMessage } from "./errors.js";
import { isErrno, writeFileAtomic } from "./fsutil.js";
import { SimPets } from "./sim-pets.js";

/** Requests a device keeps at once (firmware on-device queue). */
export const SIM_QUEUE_SIZE = 4;
/** Questions and plans among them (protocol 14.10 "On-device question or plan store"). */
export const SIM_REQUEST_STORE = 2;
/** Everything the sim can present (protocol 14.1). */
export const SIM_CAPS: readonly Cap[] = ["decide", "ask", "plan", "pets"];
export const SIM_MODEL = "devotchi-sim";
/** Exit code after the server revoked the token (close 4003). */
export const EXIT_REVOKED = 3;
/** Exit code after the server refused the protocol version (close 4002). */
export const EXIT_PROTO = 4;

const MIN_BACKOFF_MS = 500;
const MAX_BACKOFF_MS = 10_000;
/** Consecutive upgrade 401s after which the sim gives up its token. */
const MAX_UNAUTHORIZED = 3;
/** Dismissed ids remembered so that a late replay or policy timer never answers them. */
const DISMISSED_MEMORY = 256;

export type SimPolicy =
  | { kind: "all" }
  | { kind: "none" }
  | { kind: "defer" }
  | { kind: "manual" }
  | { kind: "after"; ms: number };

export type AckMode = "debounced" | "none" | "naive";

/** `--answer`: how the sim answers a question. */
export type QuestionPolicy = "first" | "last" | "none" | "null" | "terminal" | "decline" | "manual";
export const QUESTION_POLICIES: readonly QuestionPolicy[] = ["first", "last", "none", "null", "terminal", "decline"];
/** `--plan`: how the sim answers a plan. */
export type PlanPolicy = "approve" | "accept-edits" | "revise" | "stop" | "terminal" | "manual";
export const PLAN_POLICIES: readonly PlanPolicy[] = ["approve", "accept-edits", "revise", "stop", "terminal"];
/** `--decide`: a decide choice the sim sends for every permission instead of the `--approve` answer. */
export const SIM_PERMISSION_CHOICES: readonly DecideChoice[] = ["allow_session", "deny_ask_why", "deny_stop"];

export interface BatterySample {
  pct: number | null;
  state: BatteryState;
}

export interface SimOptions {
  serverUrl: string;
  /** deviceId is sim-<name>. */
  name: string;
  version: string;
  policy: SimPolicy;
  /** Flow B: claim this code instead of showing one. */
  code?: string;
  /** Use this token directly; a cli token makes a memory-only consumer (protocol 3.2). */
  token?: string;
  /** Keep the paired credentials in this file. */
  statePath?: string;
  ack?: AckMode;
  /** Stop sending holding frames this long after the first request arrived. */
  stopHeartbeatAfterMs?: number;
  heartbeatIntervalMs?: number;
  telemetryIntervalMs?: number;
  /** Scripted battery values, one per telemetry frame; the last one repeats. */
  battery?: BatterySample[];
  /** After a revocation, pair again instead of exiting. */
  repair?: boolean;
  /** Declared in register (default SIM_CAPS); empty makes a phase-1 sim. */
  caps?: readonly Cap[];
  /** Default: derived from `policy` (all/after -> first, none -> decline, defer -> terminal, manual -> manual). */
  answer?: QuestionPolicy;
  /** Default: derived from `policy` (all/after -> approve, none -> revise, defer -> terminal, manual -> manual). */
  plan?: PlanPolicy;
  /** Sent for every permission instead of the `policy` answer. */
  decide?: DecideChoice;
  /** Send one `extend` for every question and plan when it is presented. */
  extend?: boolean;
  emit: (event: SimEvent) => void;
}

export type SimEvent = { event: string; at: string } & Record<string, unknown>;

interface StoredState {
  serverUrl: string;
  token: string;
  roomId: string;
  deviceId: string;
  name: string;
}

type RequestKind = "permission" | "question" | "plan";

interface QueuedRequest {
  id: string;
  kind: RequestKind;
  tool: string;
  summary: string;
  risk: string;
  deadline: number;
  /** Scheduled answers and extends. */
  timers: NodeJS.Timeout[];
  /** Drops the request at its deadline; a `deadline` message re-arms it. */
  expiry: NodeJS.Timeout | null;
  /** Questions: the frames by index; presented once all `count` arrived. */
  questions?: Record<string, unknown>[];
  count?: number;
  /** Plans: steps left out and the `cut` flag. */
  more?: number;
  cut?: boolean;
  /** Presented: the request was complete and acknowledged to the scheduler. */
  shown: boolean;
}

export function parseQuestionPolicy(value: string): QuestionPolicy {
  if ((QUESTION_POLICIES as readonly string[]).includes(value)) return value as QuestionPolicy;
  throw new UsageError(`--answer must be one of ${QUESTION_POLICIES.join(", ")}, not "${value}"`);
}

export function parsePlanPolicy(value: string): PlanPolicy {
  if ((PLAN_POLICIES as readonly string[]).includes(value)) return value as PlanPolicy;
  throw new UsageError(`--plan must be one of ${PLAN_POLICIES.join(", ")}, not "${value}"`);
}

export function parseDecideChoice(value: string): DecideChoice {
  if ((SIM_PERMISSION_CHOICES as readonly string[]).includes(value)) return value as DecideChoice;
  throw new UsageError(`--decide must be one of ${SIM_PERMISSION_CHOICES.join(", ")}, not "${value}"`);
}

/** `--caps`: "none" or a comma-separated list of decide, ask, plan, pets. */
export function parseCaps(value: string): Cap[] {
  if (value === "none") return [];
  const caps = value.split(",").map((cap) => cap.trim());
  const unknown = caps.find((cap) => !isCap(cap));
  if (unknown !== undefined) throw new UsageError(`--caps must be none or a list of decide, ask, plan, pets, not "${unknown}"`);
  return [...new Set(caps as Cap[])];
}

/**
 * One manual `answer` argument: per question, separated by `;`, a
 * comma-separated list of option indexes, `-` for none, or `null`.
 */
export function parseAnswers(value: string): QuestionAnswer[] {
  return value.split(";").map((entry) => {
    const text = entry.trim();
    if (text === "null") return null;
    if (text === "-" || text === "") return [];
    return text.split(",").map((index) => {
      if (!/^\d+$/.test(index.trim())) throw new UsageError(`answer "${value}" must be indexes, - or null per question`);
      return Number(index.trim());
    });
  });
}

const PLAN_CHOICES: Readonly<Record<Exclude<PlanPolicy, "terminal" | "manual">, DecideChoice>> = {
  approve: "approve",
  "accept-edits": "approve_accept_edits",
  revise: "revise",
  stop: "deny_stop",
};

export function parsePolicy(value: string): SimPolicy {
  if (value === "all" || value === "none" || value === "defer" || value === "manual") return { kind: value };
  const match = /^after=(\d+)$/.exec(value);
  if (match) return { kind: "after", ms: Number(match[1]) };
  throw new UsageError(`--approve must be all, none, defer, manual or after=<ms>, not "${value}"`);
}

const BATTERY_STATES: readonly BatteryState[] = ["charging", "full", "discharging", "absent"];

/** "82,discharging" or "absent". */
export function parseBattery(value: string): BatterySample {
  if (value === "absent") return { pct: null, state: "absent" };
  const match = /^(\d{1,3}),([a-z]+)$/.exec(value);
  const pct = match ? Number(match[1]) : NaN;
  const state = match?.[2] as BatteryState | undefined;
  if (!match || pct > 100 || state === undefined || !BATTERY_STATES.includes(state) || state === "absent") {
    throw new UsageError(`--battery must be <pct>,<charging|full|discharging> or absent, not "${value}"`);
  }
  return { pct, state };
}

/** Rough LiPo curve for the telemetry `mv` field. */
function millivolts(sample: BatterySample): number {
  if (sample.pct === null) return 0;
  return Math.round(3300 + sample.pct * 9);
}

function defaultQuestionPolicy(policy: SimPolicy): QuestionPolicy {
  if (policy.kind === "none") return "decline";
  if (policy.kind === "defer") return "terminal";
  if (policy.kind === "manual") return "manual";
  return "first";
}

function defaultPlanPolicy(policy: SimPolicy): PlanPolicy {
  if (policy.kind === "none") return "revise";
  if (policy.kind === "defer") return "terminal";
  if (policy.kind === "manual") return "manual";
  return "approve";
}

const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

export class Sim {
  readonly deviceId: string;
  private readonly api: ApiClient;
  private readonly ack: AckMode;
  private readonly heartbeatIntervalMs: number;
  private readonly scheduler: HoldingAckScheduler;
  private readonly queue = new Map<string, QueuedRequest>();
  /** Ids this sim was told are no longer pending; it never answers them. */
  private readonly dismissed = new Set<string>();
  private ws: WebSocket | null = null;
  private token: string | null = null;
  private stopped = false;
  private attempt = 0;
  private unauthorized = 0;
  private ackTimer: NodeJS.Timeout | null = null;
  private telemetryTimer: NodeJS.Timeout | null = null;
  private retryTimer: NodeJS.Timeout | null = null;
  private deadTimer: NodeJS.Timeout | null = null;
  private heartbeatStopAt: number | null = null;
  private batteryIndex = 0;
  private startedAt = Date.now();
  private resolveDone!: (code: number) => void;
  /** Resolves with the process exit code once the sim has stopped. */
  readonly done: Promise<number>;
  mood: Mood = "idle";
  status: string | null = null;
  mode: string | null = null;
  private readonly pets: SimPets;
  /** Both sides offer `pets` (protocol 14.1). */
  private petsOn = false;

  constructor(private readonly opts: SimOptions) {
    const deviceId = `sim-${opts.name}`;
    if (!SIM_ID_RE.test(deviceId)) throw new UsageError(`--name must be 1..12 of a-z, 0-9 and -, not "${opts.name}"`);
    if (opts.code !== undefined && opts.token !== undefined) throw new UsageError("use either --code or --token");
    this.deviceId = deviceId;
    this.api = new ApiClient(opts.serverUrl);
    this.ack = opts.ack ?? "debounced";
    this.heartbeatIntervalMs = opts.heartbeatIntervalMs ?? HOLDING_INTERVAL_MS;
    this.scheduler = new HoldingAckScheduler({ intervalMs: this.heartbeatIntervalMs });
    this.done = new Promise((resolve) => (this.resolveDone = resolve));
    this.pets = new SimPets({
      fetchPack: (id) =>
        fetch(`${opts.serverUrl.replace(/\/+$/, "")}/api/pets/${id}/pack`, {
          headers: { Authorization: `Bearer ${this.token ?? ""}` },
          signal: AbortSignal.timeout(10_000),
        }),
      send: (msg) => this.send(msg),
      emit: (event, fields) => this.emit(event, fields),
    });
  }

  /** Ids currently in the on-device queue, oldest first. */
  get requests(): string[] {
    return [...this.queue.keys()];
  }

  async start(): Promise<void> {
    this.startedAt = Date.now();
    try {
      this.token = await this.credentials();
    } catch (err) {
      this.finish(err instanceof CliError ? err.exitCode : 1, errorMessage(err));
      return;
    }
    this.connect();
  }

  /** Stops the sim; `done` resolves with `code`. */
  async stop(code = 0): Promise<void> {
    this.finish(code, null);
    await this.done;
  }

  approve(id?: string): boolean {
    return this.answer(id, (target) => buildPermissionResponse(target, true));
  }

  reject(id?: string): boolean {
    return this.answer(id, (target) => buildPermissionResponse(target, false));
  }

  defer(id?: string): boolean {
    return this.answer(id, (target) => buildPermissionDefer(target));
  }

  decide(choice: DecideChoice, id?: string): boolean {
    return this.answer(id, (target) => buildDecide(target, choice));
  }

  /** Sends the indexes as given; the server checks them against the question. */
  answerQuestion(answers: QuestionAnswer[], id?: string): boolean {
    return this.answer(id ?? this.firstOf("question"), (target) => buildQuestionResponse(target, answers));
  }

  extend(id?: string): boolean {
    return this.answer(id, (target) => buildExtend(target));
  }

  /**
   * A line from stdin in manual mode: approve|reject|defer|extend [id],
   * decide <choice> [id], answer <answers> [id] (see parseAnswers),
   * plan approve|accept-edits|revise|stop|terminal [id].
   */
  command(line: string): void {
    const [verb, ...args] = line.trim().split(/\s+/);
    if (verb === undefined || verb === "") return;
    let sent: boolean;
    let id: string | undefined;
    try {
      switch (verb) {
        case "approve":
        case "reject":
        case "defer":
        case "extend":
          id = args[0];
          sent = verb === "approve" ? this.approve(id) : verb === "reject" ? this.reject(id) : verb === "defer" ? this.defer(id) : this.extend(id);
          break;
        case "decide":
          id = args[1];
          sent = this.decide(args[0] as DecideChoice, id);
          break;
        case "answer":
          id = args[1];
          sent = this.answerQuestion(parseAnswers(args[0] ?? ""), id);
          break;
        case "plan": {
          id = args[1] ?? this.firstOf("plan");
          const policy = parsePlanPolicy(args[0] ?? "");
          sent = policy === "terminal" || policy === "manual" ? this.defer(id) : this.decide(PLAN_CHOICES[policy], id);
          break;
        }
        default:
          this.emit("error", {
            message: `unknown command "${verb}"; use approve, reject, defer, extend, decide, answer or plan`,
          });
          return;
      }
    } catch (err) {
      this.emit("error", { message: errorMessage(err) });
      return;
    }
    if (!sent) this.emit("error", { message: id === undefined ? "no pending request" : `no pending request ${id}` });
  }

  private emit(event: string, fields: Record<string, unknown> = {}): void {
    this.opts.emit({ event, at: new Date().toISOString(), ...fields });
  }

  private finish(code: number, reason: string | null): void {
    if (this.stopped) return;
    this.stopped = true;
    this.clearRequests();
    for (const timer of [this.ackTimer, this.telemetryTimer, this.retryTimer, this.deadTimer]) {
      if (timer !== null) clearTimeout(timer);
    }
    const ws = this.ws;
    this.ws = null;
    if (ws !== null && ws.readyState !== WebSocket.CLOSED) {
      ws.once("close", () => undefined);
      if (ws.readyState === WebSocket.OPEN) ws.close(CLOSE_CODES.NORMAL);
      else ws.terminate();
    }
    this.emit("exit", { code, ...(reason !== null ? { reason } : {}) });
    this.resolveDone(code);
  }

  // --- Pairing ---

  /** A flow B code is single use, so pairing again after a revocation always uses flow A. */
  private async credentials(repairing = false): Promise<string> {
    if (this.opts.token !== undefined) return this.opts.token;
    const stored = await this.loadState();
    if (stored !== null) {
      this.emit("paired", { deviceId: stored.deviceId, name: stored.name, roomId: stored.roomId, source: "state" });
      return stored.token;
    }
    const creds = this.opts.code !== undefined && !repairing ? await this.claim(this.opts.code) : await this.pairFlowA();
    this.emit("paired", { deviceId: creds.deviceId, name: creds.name, roomId: creds.roomId });
    await this.saveState({
      serverUrl: this.opts.serverUrl,
      token: creds.token,
      roomId: creds.roomId,
      deviceId: creds.deviceId,
      name: creds.name,
    });
    return creds.token;
  }

  private async claim(code: string): Promise<PairClaimResponse> {
    try {
      return await this.api.request<PairClaimResponse>("POST", "/api/pair/claim", {
        code,
        deviceId: this.deviceId,
        model: SIM_MODEL,
        fw: this.opts.version,
      });
    } catch (err) {
      if (err instanceof ApiError && err.status === 404) throw new CliError(`the server does not know code ${code}`);
      if (err instanceof ApiError && err.status === 410) throw new CliError(`code ${code} expired or was already used`);
      throw err;
    }
  }

  /** Flow A (protocol 9.2): show a code, poll until a PC confirms it. */
  private async pairFlowA(): Promise<PairClaimResponse> {
    while (!this.stopped) {
      let started: DevicePairStartResponse;
      try {
        started = await this.api.request<DevicePairStartResponse>("POST", "/api/device/pair/start", {
          deviceId: this.deviceId,
          model: SIM_MODEL,
          fw: this.opts.version,
        });
      } catch (err) {
        if (!(err instanceof CliError) || !err.retryable) throw err;
        this.emit("error", { message: errorMessage(err) });
        await sleep(this.backoff());
        continue;
      }
      this.attempt = 0;
      this.emit("pairing", { code: started.code, expiresInS: started.expiresInS, deviceId: this.deviceId });
      for (;;) {
        await sleep(started.pollIntervalMs);
        if (this.stopped) throw new CliError("stopped while pairing");
        let polled: DevicePairPollResponse;
        try {
          polled = await this.api.request<DevicePairPollResponse>("POST", "/api/device/pair/poll", {
            deviceSecret: started.deviceSecret,
          });
        } catch (err) {
          if (err instanceof ApiError && (err.status === 404 || err.status === 410)) {
            this.emit("pairing_expired", { code: started.code });
            break;
          }
          if (err instanceof CliError && err.retryable) continue;
          throw err;
        }
        if ("token" in polled) return polled;
      }
    }
    throw new CliError("stopped while pairing");
  }

  private async loadState(): Promise<StoredState | null> {
    if (this.opts.statePath === undefined) return null;
    let text: string;
    try {
      text = await readFile(this.opts.statePath, "utf8");
    } catch (err) {
      if (isErrno(err, "ENOENT")) return null;
      throw new CliError(`cannot read ${this.opts.statePath}: ${errorMessage(err)}`);
    }
    const state = JSON.parse(text) as Partial<StoredState>;
    if (typeof state.token !== "string" || state.serverUrl !== this.opts.serverUrl) return null;
    return state as StoredState;
  }

  private async saveState(state: StoredState): Promise<void> {
    if (this.opts.statePath === undefined) return;
    await writeFileAtomic(this.opts.statePath, JSON.stringify(state, null, 2) + "\n", 0o600);
  }

  private async forgetState(): Promise<void> {
    if (this.opts.statePath === undefined) return;
    await writeFileAtomic(this.opts.statePath, "{}\n", 0o600);
  }

  // --- WebSocket ---

  private backoff(): number {
    const base = Math.min(MAX_BACKOFF_MS, MIN_BACKOFF_MS * 2 ** this.attempt);
    this.attempt += 1;
    return Math.round(base / 2 + (Math.random() * base) / 2);
  }

  private connect(): void {
    if (this.stopped || this.token === null) return;
    const ws = new WebSocket(serverWsUrl(this.opts.serverUrl), {
      headers: { Authorization: `Bearer ${this.token}` },
      maxPayload: HARD_FRAME_BYTES,
      handshakeTimeout: 10_000,
    });
    this.ws = ws;
    let httpStatus: number | null = null;

    ws.on("unexpected-response", (_req, res) => {
      httpStatus = res.statusCode ?? 0;
      res.resume();
      ws.terminate();
    });
    ws.on("error", () => undefined);
    ws.on("open", () => {
      this.armDeadTimer(ws);
      const caps = [...(this.opts.caps ?? SIM_CAPS)];
      this.send(
        buildRegister({
          role: "consumer",
          client: {
            kind: "sim",
            deviceId: this.deviceId,
            model: SIM_MODEL,
            version: this.opts.version,
            ...(caps.length > 0 ? { caps } : {}),
          },
        }),
      );
    });
    ws.on("message", (data: RawData, isBinary: boolean) => {
      if (this.ws !== ws || isBinary) return;
      this.armDeadTimer(ws);
      this.onFrame(data.toString());
    });
    ws.on("close", (code: number, reason: Buffer) => {
      if (this.ws !== ws) return;
      this.ws = null;
      this.onClosed(code, reason.toString(), httpStatus);
    });
  }

  private armDeadTimer(ws: WebSocket): void {
    if (this.deadTimer !== null) clearTimeout(this.deadTimer);
    this.deadTimer = setTimeout(() => {
      if (this.ws === ws) ws.terminate();
    }, HEARTBEAT_DEAD_MS);
  }

  private onClosed(code: number, reason: string, httpStatus: number | null): void {
    if (this.telemetryTimer !== null) clearInterval(this.telemetryTimer);
    if (this.deadTimer !== null) clearTimeout(this.deadTimer);
    this.telemetryTimer = null;
    this.deadTimer = null;
    // Pending requests are not kept across a reconnect: the server replays the ones still held.
    this.clearRequests();
    if (this.stopped) return;
    this.emit("closed", { code, ...(reason !== "" ? { reason } : {}), ...(httpStatus !== null ? { http: httpStatus } : {}) });
    if (code === CLOSE_CODES.REVOKED) {
      void this.onRevoked();
      return;
    }
    if (code === CLOSE_CODES.UNSUPPORTED_PROTO) {
      this.finish(EXIT_PROTO, "the server does not speak protocol 2");
      return;
    }
    if (httpStatus === 401) {
      this.unauthorized += 1;
      if (this.unauthorized >= MAX_UNAUTHORIZED) {
        void this.onRevoked();
        return;
      }
    } else this.unauthorized = 0;
    const delay = this.backoff();
    this.retryTimer = setTimeout(() => {
      this.retryTimer = null;
      this.connect();
    }, delay);
  }

  private async onRevoked(): Promise<void> {
    if (this.opts.repair !== true || this.opts.token !== undefined) {
      this.finish(EXIT_REVOKED, "token revoked (close 4003)");
      return;
    }
    try {
      await this.forgetState();
      this.unauthorized = 0;
      this.token = await this.credentials(true);
      this.connect();
    } catch (err) {
      this.finish(err instanceof CliError ? err.exitCode : 1, errorMessage(err));
    }
  }

  private send(msg: Parameters<typeof serialize>[0]): boolean {
    const ws = this.ws;
    if (ws === null || ws.readyState !== WebSocket.OPEN) return false;
    ws.send(serialize(msg));
    if (msg.type !== "pong") this.emit("tx", { msg });
    return true;
  }

  private onFrame(text: string): void {
    let msg: Record<string, unknown>;
    try {
      msg = JSON.parse(text) as Record<string, unknown>;
    } catch {
      return;
    }
    if (msg.type === "ping") {
      this.send(buildPong());
      return;
    }
    this.emit("rx", { msg });
    switch (msg.type) {
      case "register_ack":
        this.attempt = 0;
        this.unauthorized = 0;
        this.mode = typeof msg.mode === "string" ? msg.mode : null;
        this.emit("registered", {
          deviceId: this.deviceId,
          roomId: msg.roomId,
          mode: msg.mode,
          peers: msg.peers,
          device: msg.device,
          caps: msg.caps,
          utcOffsetMin: msg.utcOffsetMin,
        });
        this.sendTelemetry();
        this.petsOn = (this.opts.caps ?? SIM_CAPS).includes("pets") && Array.isArray(msg.caps) && msg.caps.includes("pets");
        if (this.petsOn) this.pets.report();
        this.telemetryTimer = setInterval(() => this.sendTelemetry(), this.opts.telemetryIntervalMs ?? 60_000);
        break;
      case "permission":
      case "plan":
        this.onRequest(msg, msg.type);
        break;
      case "question":
        this.onQuestionFrame(msg);
        break;
      case "deadline":
        this.onDeadline(msg);
        break;
      case "time":
        this.emit("time", { serverTime: msg.serverTime, utcOffsetMin: msg.utcOffsetMin });
        break;
      case "dismiss":
        if (typeof msg.id === "string") {
          this.dismissed.add(msg.id);
          if (this.dismissed.size > DISMISSED_MEMORY) this.dismissed.delete(this.dismissed.values().next().value!);
          const known = this.remove(msg.id);
          this.emit("dismiss", { id: msg.id, reason: msg.reason, outcome: msg.outcome, by: msg.by, choice: msg.choice, known });
        }
        break;
      case "error":
        if (msg.code === "UNKNOWN_REQUEST" && typeof msg.id === "string") this.remove(msg.id);
        break;
      case "status":
        if (typeof msg.msg === "string") this.status = msg.msg;
        if (isMood(msg.mood)) this.mood = msg.mood;
        break;
      case "notify":
      case "mood":
        this.mood = isMood(msg.mood) ? msg.mood : "idle";
        break;
      case "mode":
        if (typeof msg.mode === "string") this.mode = msg.mode;
        break;
      case "identify":
        this.emit("identify", { durationMs: msg.durationMs });
        break;
      case "pet_cmd":
        if (this.petsOn) void this.pets.onCommand(msg);
        break;
      default:
        break;
    }
  }

  // --- Requests ---

  /** Questions and plans the sim holds; permissions are not counted. */
  private storedRequests(): number {
    let count = 0;
    for (const request of this.queue.values()) if (request.kind !== "permission") count++;
    return count;
  }

  private firstOf(kind: RequestKind): string | undefined {
    for (const request of this.queue.values()) if (request.kind === kind && request.shown) return request.id;
    return undefined;
  }

  /** Queues a request frame; false when it was not accepted. */
  private enqueue(msg: Record<string, unknown>, kind: RequestKind, summary: string): QueuedRequest | null {
    const id = typeof msg.id === "string" ? msg.id : null;
    if (id === null || this.dismissed.has(id)) return null;
    const timeout = typeof msg.timeout === "number" ? msg.timeout : 0;
    const existing = this.queue.get(id);
    const full = kind === "permission" ? this.queue.size >= SIM_QUEUE_SIZE : this.storedRequests() >= SIM_REQUEST_STORE;
    if (!existing && (full || this.queue.size >= SIM_QUEUE_SIZE)) {
      this.emit("request", { id, kind, accepted: false, reason: "queue full" });
      return null;
    }
    // A replay of a request already shown starts its presentation again.
    const replay = existing !== undefined && (kind !== "question" || msg.index === 0);
    if (existing && !replay) return existing;
    if (existing) this.clearTimers(existing);
    const request: QueuedRequest = {
      id,
      kind,
      tool: String(msg.tool ?? kind),
      summary,
      risk: String(msg.risk ?? "low"),
      deadline: Date.now() + timeout,
      timers: [],
      expiry: null,
      shown: false,
    };
    this.queue.set(id, request);
    this.armExpiry(request);
    return request;
  }

  private armExpiry(request: QueuedRequest): void {
    if (request.expiry !== null) clearTimeout(request.expiry);
    request.expiry = setTimeout(() => {
      if (this.remove(request.id)) this.emit("expired", { id: request.id });
    }, Math.max(0, request.deadline - Date.now()));
  }

  private clearTimers(request: QueuedRequest): void {
    for (const timer of request.timers) clearTimeout(timer);
    if (request.expiry !== null) clearTimeout(request.expiry);
  }

  private onRequest(msg: Record<string, unknown>, kind: "permission" | "plan"): void {
    const summary = kind === "plan" ? String(msg.title) : String(msg.summary);
    const request = this.enqueue(msg, kind, summary);
    if (request === null) return;
    if (kind === "plan") {
      request.more = typeof msg.more === "number" ? msg.more : 0;
      request.cut = msg.cut === true;
    }
    this.present(request, msg);
  }

  /** Protocol 14.4: a question is presented once all `count` frames arrived. */
  private onQuestionFrame(msg: Record<string, unknown>): void {
    const request = this.enqueue(msg, "question", String(msg.text));
    if (request === null) return;
    const index = typeof msg.index === "number" ? msg.index : 0;
    request.count = typeof msg.count === "number" ? msg.count : 1;
    request.questions ??= [];
    request.questions[index] = msg;
    const complete = request.questions.filter((frame) => frame !== undefined).length === request.count;
    if (complete && !request.shown) this.present(request, msg);
  }

  private onDeadline(msg: Record<string, unknown>): void {
    const request = typeof msg.id === "string" ? this.queue.get(msg.id) : undefined;
    if (request === undefined || typeof msg.timeout !== "number") return;
    request.deadline = Date.now() + msg.timeout;
    this.armExpiry(request);
    this.emit("deadline", { id: request.id, timeout: msg.timeout });
  }

  private present(request: QueuedRequest, msg: Record<string, unknown>): void {
    const now = Date.now();
    request.shown = true;
    const timeout = Math.max(0, request.deadline - now);
    const fields: Record<string, unknown> = { id: request.id, kind: request.kind, accepted: true, timeout };
    if (request.kind === "permission") {
      Object.assign(fields, { tool: request.tool, summary: request.summary, risk: request.risk });
      for (const key of ["cmd", "cut", "desc", "reason", "grant"]) if (msg[key] !== undefined) fields[key] = msg[key];
    } else if (request.kind === "plan") {
      Object.assign(fields, { title: msg.title, steps: msg.steps, more: request.more, cut: request.cut });
    } else {
      fields.questions = request.questions!.map((q) => ({ header: q.header, text: q.text, multi: q.multi, options: q.options }));
    }
    this.emit("request", fields);
    if (this.heartbeatStopAt === null && this.opts.stopHeartbeatAfterMs !== undefined) {
      this.heartbeatStopAt = now + this.opts.stopHeartbeatAfterMs;
    }

    const id = request.id;
    if (this.ack === "naive" && this.mayHold(now)) this.send(buildHolding([id]));
    if (this.ack !== "none") {
      this.scheduler.onPermission(id, now);
      // The naive ack already went out; the scheduler then only paces heartbeats.
      if (this.ack === "naive") this.scheduler.onTick(now + HOLDING_ACK_MAX_DELAY_MS);
      this.armAck();
    }
    this.applyPolicy(request);
  }

  private applyPolicy(request: QueuedRequest): void {
    const policy = this.opts.policy;
    const act = (fn: () => void, ms: number) => request.timers.push(setTimeout(fn, ms));
    // Answer after the ack had its chance to go out, as a person would.
    const ackDelay = this.ack === "none" ? 0 : 150;
    const delay = policy.kind === "after" ? policy.ms : ackDelay;
    if (request.kind !== "permission" && this.opts.extend === true) act(() => this.extend(request.id), ackDelay / 2);
    if (request.kind === "question") {
      const answer = this.opts.answer ?? defaultQuestionPolicy(policy);
      if (answer !== "manual") act(() => this.applyQuestionPolicy(request, answer), delay);
      return;
    }
    if (request.kind === "plan") {
      const plan = this.opts.plan ?? defaultPlanPolicy(policy);
      if (plan === "terminal") act(() => this.defer(request.id), delay);
      else if (plan !== "manual") act(() => this.decide(PLAN_CHOICES[plan], request.id), delay);
      return;
    }
    if (this.opts.decide !== undefined && policy.kind !== "manual") {
      const choice = this.opts.decide;
      act(() => this.decide(choice, request.id), delay);
    } else if (policy.kind === "all") act(() => this.approve(request.id), ackDelay);
    else if (policy.kind === "none") act(() => this.reject(request.id), ackDelay);
    else if (policy.kind === "defer") act(() => this.defer(request.id), ackDelay);
    else if (policy.kind === "after") act(() => this.approve(request.id), policy.ms);
  }

  private applyQuestionPolicy(request: QueuedRequest, policy: Exclude<QuestionPolicy, "manual">): void {
    if (policy === "terminal") {
      this.defer(request.id);
      return;
    }
    if (policy === "decline") {
      this.decide("decline", request.id);
      return;
    }
    const answers = (request.questions ?? []).map((q): QuestionAnswer => {
      const options = Array.isArray(q.options) ? q.options.length : 0;
      if (policy === "null") return null;
      if (policy === "none") return q.multi === true ? [] : null;
      return [policy === "first" ? 0 : Math.max(0, options - 1)];
    });
    this.answerQuestion(answers, request.id);
  }

  private answer(id: string | undefined, build: (id: string) => Parameters<typeof serialize>[0]): boolean {
    const target = id ?? this.requests[0];
    if (target === undefined || !this.queue.has(target) || this.dismissed.has(target)) return false;
    // Stays queued until the server's dismiss confirms the answer.
    return this.send(build(target));
  }

  private mayHold(now: number): boolean {
    return this.heartbeatStopAt === null || now < this.heartbeatStopAt;
  }

  private armAck(): void {
    if (this.ackTimer !== null) clearTimeout(this.ackTimer);
    this.ackTimer = null;
    const due = this.scheduler.nextDueAt();
    if (due === null) return;
    this.ackTimer = setTimeout(() => {
      this.ackTimer = null;
      const now = Date.now();
      const ids = this.scheduler.onTick(now);
      if (ids !== null && ids.length > 0 && this.mayHold(now)) this.send(buildHolding(ids));
      this.armAck();
    }, Math.max(0, due - Date.now()));
  }

  private remove(id: string): boolean {
    const request = this.queue.get(id);
    if (request === undefined) return false;
    this.clearTimers(request);
    this.queue.delete(id);
    this.scheduler.onRemove(id);
    this.armAck();
    return true;
  }

  private clearRequests(): void {
    for (const request of this.queue.values()) this.clearTimers(request);
    this.queue.clear();
    this.scheduler.clear();
    if (this.ackTimer !== null) clearTimeout(this.ackTimer);
    this.ackTimer = null;
  }

  // --- Telemetry ---

  private sendTelemetry(): void {
    const script = this.opts.battery ?? [];
    const sample: BatterySample = script[Math.min(this.batteryIndex, script.length - 1)] ?? { pct: 100, state: "full" };
    if (this.batteryIndex < script.length) this.batteryIndex += 1;
    const msg: TelemetryMessage = buildTelemetry({
      battery: { state: sample.state, pct: sample.pct, mv: millivolts(sample) },
      power: sample.state === "discharging" ? "battery" : "usb",
      rssi: -50,
      uptimeS: Math.round((Date.now() - this.startedAt) / 1000),
      heapFree: 150_000,
      heapMin: 100_000,
      resetReason: "poweron",
      fw: this.opts.version,
      screen: this.queue.size > 0 ? "request" : "pet",
      stage: "active",
      pet: this.pets.selectedPet,
      ...(sample.state === "discharging" && sample.pct !== null && sample.pct < 10
        ? { alert: sample.pct < 5 ? ("battery_critical" as const) : ("battery_low" as const) }
        : {}),
    });
    this.send(msg);
  }
}

export interface SimCliOptions {
  server: string;
  name: string;
  approve: SimPolicy;
  code?: string;
  token?: string;
  state?: string;
  ack: AckMode;
  stopHeartbeatAfter?: number;
  heartbeatInterval?: number;
  telemetryInterval?: number;
  battery?: BatterySample[];
  repair?: boolean;
  caps?: Cap[];
  answer?: QuestionPolicy;
  plan?: PlanPolicy;
  decide?: DecideChoice;
  extend?: boolean;
  version: string;
}

/** `devotchi sim`: one JSON line per event on stdout; resolves with the exit code. */
export async function runSim(options: SimCliOptions, out: (line: string) => void): Promise<number> {
  const sim = new Sim({
    serverUrl: options.server,
    name: options.name,
    version: options.version,
    policy: options.approve,
    code: options.code,
    token: options.token,
    statePath: options.state,
    ack: options.ack,
    stopHeartbeatAfterMs: options.stopHeartbeatAfter,
    heartbeatIntervalMs: options.heartbeatInterval,
    telemetryIntervalMs: options.telemetryInterval,
    battery: options.battery,
    repair: options.repair,
    caps: options.caps,
    answer: options.answer,
    plan: options.plan,
    decide: options.decide,
    extend: options.extend,
    emit: (event) => out(JSON.stringify(event)),
  });
  const stop = () => void sim.stop(0);
  process.once("SIGINT", stop);
  process.once("SIGTERM", stop);
  if (options.approve.kind === "manual" || options.answer === "manual" || options.plan === "manual") {
    const { createInterface } = await import("node:readline");
    const rl = createInterface({ input: process.stdin, terminal: false });
    rl.on("line", (line) => sim.command(line));
    void sim.done.then(() => rl.close());
  }
  await sim.start();
  const code = await sim.done;
  process.off("SIGINT", stop);
  process.off("SIGTERM", stop);
  return code;
}
