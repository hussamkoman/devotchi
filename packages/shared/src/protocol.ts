// Devotchi protocol v2 types and constants. docs/protocol.md is the contract;
// section numbers below refer to it.

export const PROTO_VERSION = 2;

// --- Enums (section 5) ---

export const MOODS = [
  "happy",
  "sad",
  "excited",
  "anxious",
  "sleeping",
  "angry",
  "proud",
  "idle",
  "focused",
  "working",
  "thinking",
] as const;
export type Mood = (typeof MOODS)[number];

export function isMood(value: unknown): value is Mood {
  return typeof value === "string" && (MOODS as readonly string[]).includes(value);
}

export const ROLES = ["producer", "consumer"] as const;
export type Role = (typeof ROLES)[number];

export const TOKEN_KINDS = ["cli", "device"] as const;
export type TokenKind = (typeof TOKEN_KINDS)[number];

export const PRODUCER_KINDS = ["mcp", "cli"] as const;
export type ProducerKind = (typeof PRODUCER_KINDS)[number];
export const CONSUMER_KINDS = ["device", "dashboard", "sim"] as const;
export type ConsumerKind = (typeof CONSUMER_KINDS)[number];
export type ClientKind = ProducerKind | ConsumerKind;

export const ROOM_MODES = ["ask", "desk"] as const;
export type RoomMode = (typeof ROOM_MODES)[number];

export const RISK_LEVELS = ["low", "med", "high"] as const;
export type Risk = (typeof RISK_LEVELS)[number];

export const DISMISS_REASONS = ["answered", "cancelled", "timeout", "released"] as const;
export type DismissReason = (typeof DISMISS_REASONS)[number];
export type AnswerOutcome = "allow" | "deny";

export const BATTERY_STATES = ["charging", "full", "discharging", "absent"] as const;
export type BatteryState = (typeof BATTERY_STATES)[number];
export const POWER_SOURCES = ["usb", "battery"] as const;
export type PowerSource = (typeof POWER_SOURCES)[number];
export const POWER_STAGES = ["active", "dim", "off"] as const;
export type PowerStage = (typeof POWER_STAGES)[number];
export const BATTERY_ALERTS = ["battery_low", "battery_critical"] as const;
export type BatteryAlert = (typeof BATTERY_ALERTS)[number];
export const RESET_REASONS = ["poweron", "sw", "panic", "wdt", "brownout", "deepsleep", "usb", "other"] as const;
export type ResetReason = (typeof RESET_REASONS)[number];

export const ERROR_CODES = [
  "INVALID_MESSAGE",
  "MESSAGE_TOO_LARGE",
  "RATE_LIMITED",
  "NOT_ALLOWED",
  "UNKNOWN_REQUEST",
  "UNSUPPORTED_PROTO",
  "ROOM_FULL",
] as const;
export type ErrorCode = (typeof ERROR_CODES)[number];

// --- Identifiers ---

export const DEVICE_ID_RE = /^dv-[0-9a-f]{6}$/;
export const SIM_ID_RE = /^sim-[a-z0-9-]{1,12}$/;
export const REQUEST_ID_RE = /^[0-9a-f]{32}$/;
export const SID_RE = /^[0-9a-f]{6}$/;
export const ROOM_ID_RE = /^[0-9a-f]{12}$/;
export const NOTIFY_EVENT_RE = /^[a-z0-9_]+$/;

/** A consumer id accepted in `register.client.deviceId` (section 3.2). */
export function isValidDeviceId(id: unknown): id is string {
  return typeof id === "string" && (DEVICE_ID_RE.test(id) || SIM_ID_RE.test(id));
}

// --- Limits (sections 2 and 11) ---

export const MAX_FRAME_BYTES = 2048;
export const HARD_FRAME_BYTES = 8192;

/** String limits in UTF-8 bytes. */
export const LIMITS = {
  msg: 200,
  summary: 200,
  reason: 200,
  detail: 300,
  details: 600,
  tool: 40,
  event: 40,
  project: 32,
  source: 32,
  name: 24,
} as const;

export const MAX_URGENCY = 3;
export const DEFAULT_URGENCY = 1;
export const HOLDING_MAX_IDS = 8;
export const IDENTIFY_MIN_MS = 1000;
export const IDENTIFY_MAX_MS = 10000;
export const DEFAULT_IDENTIFY_MS = 3000;

// --- Timing ---

export const HEARTBEAT_INTERVAL_MS = 30_000;
export const HEARTBEAT_DEAD_MS = 70_000;
export const REGISTER_DEADLINE_MS = 10_000;
export const HOLDING_INTERVAL_MS = 4_000;
export const HOLDING_ACK_DEBOUNCE_MS = 100;
export const HOLDING_ACK_MAX_DELAY_MS = 500;
export const HOLDING_BUCKET = 8;
export const HOLDING_REFILL_PER_S = 2;
export const CONTENT_RATE_PER_MIN = 60;
export const DEFAULT_ACK_MS = 3_000;
export const DEFAULT_ALIVE_MS = 12_000;
export const DEFAULT_HOLD_MS = 30_000;
export const HOLD_MIN_MS = 10_000;
export const HOLD_MAX_MS = 300_000;
export const MIN_REPLAY_REMAINING_MS = 1_000;
export const REPLAY_MAX_AGE_MS = 30 * 60_000;
export const PAIRING_CODE_TTL_MS = 300_000;

export const ROOM_MAX_PENDING = 8;
export const ROOM_MAX_PRODUCERS = 8;
export const ROOM_MAX_CONSUMERS = 8;

// --- Close codes (section 3.6) ---

export const CLOSE_CODES = {
  NORMAL: 1000,
  GOING_AWAY: 1001,
  POLICY_VIOLATION: 1008,
  MESSAGE_TOO_BIG: 1009,
  UNSUPPORTED_PROTO: 4002,
  REVOKED: 4003,
} as const;
export type CloseCode = (typeof CLOSE_CODES)[keyof typeof CLOSE_CODES];

/** Header that marks a Claude Code hook as installed by `devotchi setup` (section 8.7). */
export const HOOK_MARKER_HEADER = "X-Devotchi-Hook";

// --- Messages (section 4) ---

export interface Peers {
  producers: number;
  consumers: number;
  devices: number;
}

export interface RegisterClient {
  kind: ClientKind;
  deviceId?: string;
  model?: string;
  version?: string;
  project?: string;
}

export interface RegisterMessage {
  type: "register";
  role: Role;
  proto: typeof PROTO_VERSION;
  client: RegisterClient;
}

export interface RegisterAckMessage {
  type: "register_ack";
  role: Role;
  roomId: string;
  proto: typeof PROTO_VERSION;
  serverVersion: string;
  serverTime: number;
  mode: RoomMode;
  peers: Peers;
  device?: { id: string; name: string };
}

export interface PingMessage {
  type: "ping";
}

export interface PongMessage {
  type: "pong";
}

export interface ErrorMessage {
  type: "error";
  code: ErrorCode;
  message: string;
  id?: string;
}

export interface PeersMessage extends Peers {
  type: "peers";
}

// `timestamp` on content messages is set by the server on everything it emits
// or forwards (section 2); producers send none.

export interface StatusMessage {
  type: "status";
  msg: string;
  /** Absent means "keep the current mood". */
  mood?: Mood;
  sid?: string;
  project?: string;
  timestamp?: string;
}

export interface NotifyMessage {
  type: "notify";
  event: string;
  mood: Mood;
  detail?: string;
  urgency?: number;
  source?: string;
  sid?: string;
  project?: string;
  timestamp?: string;
}

export interface MoodMessage {
  type: "mood";
  mood: Mood;
  reason?: string;
  timestamp?: string;
}

export interface PermissionMessage {
  type: "permission";
  id: string;
  tool: string;
  summary: string;
  details?: string;
  risk: Risk;
  sid?: string;
  project?: string;
  /** Remaining hold time in ms when sent, always >= 1000. */
  timeout: number;
  timestamp?: string;
}

export interface HoldingMessage {
  type: "holding";
  ids: string[];
}

export interface PermissionResponseMessage {
  type: "permission_response";
  id: string;
  granted: boolean;
}

export interface PermissionDeferMessage {
  type: "permission_defer";
  id: string;
}

export type DismissMessage =
  | { type: "dismiss"; id: string; reason: "answered"; outcome: AnswerOutcome; by: string }
  | { type: "dismiss"; id: string; reason: Exclude<DismissReason, "answered"> };

export interface ModeMessage {
  type: "mode";
  mode: RoomMode;
}

export interface SetModeMessage {
  type: "set_mode";
  mode: RoomMode;
}

export interface TelemetryBattery {
  state: BatteryState;
  pct: number | null;
  mv: number;
}

export interface TelemetryMessage {
  type: "telemetry";
  battery: TelemetryBattery;
  power: PowerSource;
  rssi: number;
  uptimeS: number;
  heapFree: number;
  heapMin: number;
  resetReason: string;
  fw: string;
  screen?: string;
  stage?: PowerStage;
  alert?: BatteryAlert;
}

export interface IdentifyMessage {
  type: "identify";
  durationMs: number;
}

export type ProducerToServerMessage = RegisterMessage | PingMessage | PongMessage | StatusMessage | NotifyMessage | MoodMessage;

export type ConsumerToServerMessage =
  | RegisterMessage
  | PingMessage
  | PongMessage
  | HoldingMessage
  | PermissionResponseMessage
  | PermissionDeferMessage
  | SetModeMessage
  | TelemetryMessage;

export type InboundMessage = ProducerToServerMessage | ConsumerToServerMessage;

export type ServerToProducerMessage = RegisterAckMessage | PingMessage | PongMessage | ErrorMessage | PeersMessage;

export type ServerToConsumerMessage =
  | ServerToProducerMessage
  | StatusMessage
  | NotifyMessage
  | MoodMessage
  | PermissionMessage
  | DismissMessage
  | ModeMessage
  | IdentifyMessage;

export type ProtocolMessage = InboundMessage | ServerToConsumerMessage;
export type MessageType = ProtocolMessage["type"];

/** Section 3.7: the types each role may send to the server. */
export const INBOUND_TYPES: Readonly<Record<Role, readonly InboundMessage["type"][]>> = {
  producer: ["register", "ping", "pong", "status", "notify", "mood"],
  consumer: ["register", "ping", "pong", "holding", "permission_response", "permission_defer", "set_mode", "telemetry"],
};

// --- Notify reaction classes (section 5.2) ---

export type EventClass = "positive" | "negative" | "complete" | "none" | "attention";

const POSITIVE_EVENTS = new Set(["tests_passed", "build_succeeded", "deploy_complete"]);
const NEGATIVE_EVENTS = new Set(["tests_failed", "build_failed", "deploy_failed", "error"]);

/** Reaction class of a notification: by event name first, then by mood. */
export function eventClass(event: string, mood?: string): EventClass {
  if (POSITIVE_EVENTS.has(event) || event.endsWith("_succeeded") || event.endsWith("_passed")) return "positive";
  if (NEGATIVE_EVENTS.has(event) || event.endsWith("_failed")) return "negative";
  if (event === "task_complete") return "complete";
  if (event === "task_paused") return "none";
  if (event === "needs_input") return "attention";
  if (mood === "happy" || mood === "excited" || mood === "proud") return "positive";
  if (mood === "sad" || mood === "angry") return "negative";
  return "none";
}

/** Mood used by `POST /api/notify` when the body has none (section 7.7). */
export function defaultMoodForEvent(event: string): Mood {
  const cls = eventClass(event);
  if (cls === "positive") return "happy";
  if (cls === "negative") return "sad";
  return "idle";
}

// --- Holding ack and heartbeat scheduling (section 4.7) ---

export interface HoldingAckSchedulerOptions {
  debounceMs?: number;
  maxDelayMs?: number;
  intervalMs?: number;
}

/**
 * Decides when a consumer sends `holding`. Pure: the caller supplies the
 * clock, calls `onTick(now)` at or after `nextDueAt()`, and sends a
 * `holding` frame with the returned ids whenever they are not null.
 *
 * The ack goes out `debounceMs` after the newest unacknowledged request
 * arrived, but never later than `maxDelayMs` after the oldest one, and names
 * every request still held. After that a heartbeat follows every
 * `intervalMs` while at least one request is held.
 */
export class HoldingAckScheduler {
  private readonly debounceMs: number;
  private readonly maxDelayMs: number;
  private readonly intervalMs: number;
  /** Held ids in arrival order. */
  private readonly held: string[] = [];
  private readonly unacked = new Set<string>();
  private firstUnackedAt = 0;
  private lastArrivalAt = 0;
  private lastSentAt = 0;

  constructor(options: HoldingAckSchedulerOptions = {}) {
    this.debounceMs = options.debounceMs ?? HOLDING_ACK_DEBOUNCE_MS;
    this.maxDelayMs = options.maxDelayMs ?? HOLDING_ACK_MAX_DELAY_MS;
    this.intervalMs = options.intervalMs ?? HOLDING_INTERVAL_MS;
  }

  /**
   * A request became visible. Calling it again for an id already held (a
   * replay after reconnecting) makes it unacknowledged again, because the
   * server tracks holders per socket.
   */
  onPermission(id: string, now: number): void {
    if (!this.held.includes(id)) this.held.push(id);
    if (this.unacked.size === 0) this.firstUnackedAt = now;
    this.unacked.add(id);
    this.lastArrivalAt = now;
  }

  /** The request was dismissed, answered, or is no longer visible. */
  onRemove(id: string): void {
    const index = this.held.indexOf(id);
    if (index >= 0) this.held.splice(index, 1);
    this.unacked.delete(id);
  }

  /** Forget everything, e.g. when the socket closed. */
  clear(): void {
    this.held.length = 0;
    this.unacked.clear();
  }

  get ids(): readonly string[] {
    return this.held;
  }

  /** Time at which the next `holding` is due, or null when nothing is held. */
  nextDueAt(): number | null {
    if (this.unacked.size > 0) {
      return Math.min(this.lastArrivalAt + this.debounceMs, this.firstUnackedAt + this.maxDelayMs);
    }
    if (this.held.length > 0) return this.lastSentAt + this.intervalMs;
    return null;
  }

  /** Returns the ids for a `holding` frame that is due now, or null. */
  onTick(now: number): string[] | null {
    const due = this.nextDueAt();
    if (due === null || now < due) return null;
    const unackedFirst = [
      ...this.held.filter((id) => this.unacked.has(id)),
      ...this.held.filter((id) => !this.unacked.has(id)),
    ];
    this.unacked.clear();
    this.lastSentAt = now;
    return unackedFirst.slice(0, HOLDING_MAX_IDS);
  }
}
