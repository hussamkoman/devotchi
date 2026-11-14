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
  buildHolding,
  buildPermissionDefer,
  buildPermissionResponse,
  buildPong,
  buildRegister,
  buildTelemetry,
  isMood,
  serialize,
  type BatteryState,
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

/** Requests a device keeps at once (firmware on-device queue). */
export const SIM_QUEUE_SIZE = 4;
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

interface QueuedRequest {
  id: string;
  tool: string;
  summary: string;
  risk: string;
  deadline: number;
  timers: NodeJS.Timeout[];
}

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

  /** A line from stdin in manual mode: approve|reject|defer [id]. */
  command(line: string): void {
    const [verb, id] = line.trim().split(/\s+/);
    if (verb === undefined || verb === "") return;
    const actions: Record<string, (id?: string) => boolean> = {
      approve: (x) => this.approve(x),
      reject: (x) => this.reject(x),
      defer: (x) => this.defer(x),
    };
    const action = actions[verb];
    if (action === undefined) {
      this.emit("error", { message: `unknown command "${verb}"; use approve, reject or defer [id]` });
      return;
    }
    if (!action(id)) this.emit("error", { message: id === undefined ? "no pending request" : `no pending request ${id}` });
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
      this.send(
        buildRegister({
          role: "consumer",
          client: { kind: "sim", deviceId: this.deviceId, model: SIM_MODEL, version: this.opts.version },
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
        this.emit("registered", { deviceId: this.deviceId, roomId: msg.roomId, mode: msg.mode, peers: msg.peers, device: msg.device });
        this.sendTelemetry();
        this.telemetryTimer = setInterval(() => this.sendTelemetry(), this.opts.telemetryIntervalMs ?? 60_000);
        break;
      case "permission":
        this.onPermission(msg);
        break;
      case "dismiss":
        if (typeof msg.id === "string") {
          this.dismissed.add(msg.id);
          if (this.dismissed.size > DISMISSED_MEMORY) this.dismissed.delete(this.dismissed.values().next().value!);
          const known = this.remove(msg.id);
          this.emit("dismiss", { id: msg.id, reason: msg.reason, outcome: msg.outcome, by: msg.by, known });
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
      default:
        break;
    }
  }

  // --- Requests ---

  private onPermission(msg: Record<string, unknown>): void {
    const id = typeof msg.id === "string" ? msg.id : null;
    if (id === null || this.dismissed.has(id)) return;
    const now = Date.now();
    const timeout = typeof msg.timeout === "number" ? msg.timeout : 0;
    const replay = this.queue.has(id);
    if (!replay && this.queue.size >= SIM_QUEUE_SIZE) {
      this.emit("request", { id, accepted: false, reason: "queue full" });
      return;
    }
    const existing = this.queue.get(id);
    for (const timer of existing?.timers ?? []) clearTimeout(timer);
    const request: QueuedRequest = {
      id,
      tool: String(msg.tool),
      summary: String(msg.summary),
      risk: String(msg.risk),
      deadline: now + timeout,
      timers: [],
    };
    this.queue.set(id, request);
    request.timers.push(
      setTimeout(() => {
        if (this.remove(id)) this.emit("expired", { id });
      }, timeout),
    );
    this.emit("request", { id, accepted: true, tool: request.tool, summary: request.summary, risk: request.risk, timeout });
    if (this.heartbeatStopAt === null && this.opts.stopHeartbeatAfterMs !== undefined) {
      this.heartbeatStopAt = now + this.opts.stopHeartbeatAfterMs;
    }

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
    if (policy.kind === "all") act(() => this.approve(request.id), ackDelay);
    else if (policy.kind === "none") act(() => this.reject(request.id), ackDelay);
    else if (policy.kind === "defer") act(() => this.defer(request.id), ackDelay);
    else if (policy.kind === "after") act(() => this.approve(request.id), policy.ms);
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
    for (const timer of request.timers) clearTimeout(timer);
    this.queue.delete(id);
    this.scheduler.onRemove(id);
    this.armAck();
    return true;
  }

  private clearRequests(): void {
    for (const request of this.queue.values()) for (const timer of request.timers) clearTimeout(timer);
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
    emit: (event) => out(JSON.stringify(event)),
  });
  const stop = () => void sim.stop(0);
  process.once("SIGINT", stop);
  process.once("SIGTERM", stop);
  if (options.approve.kind === "manual") {
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
