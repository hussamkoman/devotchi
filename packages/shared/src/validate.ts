// The one validator for inbound frames and the builders for every message.
// The server parses each inbound frame with parseInbound and creates each
// frame it sends with a builder; clients use the builders before sending.

import {
  BATTERY_ALERTS,
  BATTERY_STATES,
  CLOSE_CODES,
  CONSUMER_KINDS,
  DEFAULT_IDENTIFY_MS,
  HOLDING_MAX_IDS,
  IDENTIFY_MAX_MS,
  IDENTIFY_MIN_MS,
  INBOUND_TYPES,
  LIMITS,
  MAX_FRAME_BYTES,
  MAX_URGENCY,
  MIN_REPLAY_REMAINING_MS,
  MOODS,
  NOTIFY_EVENT_RE,
  POWER_SOURCES,
  POWER_STAGES,
  PRODUCER_KINDS,
  PROTO_VERSION,
  REQUEST_ID_RE,
  RISK_LEVELS,
  ROLES,
  ROOM_MODES,
  SID_RE,
  isValidDeviceId,
  type AnswerOutcome,
  type ClientKind,
  type CloseCode,
  type DismissMessage,
  type ErrorCode,
  type ErrorMessage,
  type HoldingMessage,
  type IdentifyMessage,
  type InboundMessage,
  type ModeMessage,
  type Mood,
  type MoodMessage,
  type NotifyMessage,
  type PeersMessage,
  type Peers,
  type PermissionDeferMessage,
  type PermissionMessage,
  type PermissionResponseMessage,
  type PingMessage,
  type PongMessage,
  type ProtocolMessage,
  type RegisterAckMessage,
  type RegisterClient,
  type RegisterMessage,
  type Risk,
  type Role,
  type RoomMode,
  type SetModeMessage,
  type StatusMessage,
  type TelemetryBattery,
  type TelemetryMessage,
  type TokenKind,
} from "./protocol.js";
import { sanitizeAscii, utf8ByteLength, utf8Truncate } from "./text.js";

// --- Parsing ---

export type InboundContext =
  | { role: "unregistered"; tokenKind: TokenKind }
  | { role: Role; tokenKind?: TokenKind };

export type ParseFailure = {
  ok: false;
  code: ErrorCode;
  message: string;
  /** Set for a permission_response/permission_defer that carried a well-formed id. */
  id?: string;
  /** Set when the socket must be closed: every failure of a first frame. */
  close?: CloseCode;
};

export type ParseResult = { ok: true; msg: InboundMessage } | ParseFailure;

class Rejection extends Error {
  constructor(
    readonly code: ErrorCode,
    message: string,
    readonly id?: string,
  ) {
    super(message);
  }
}

function reject(code: ErrorCode, message: string, id?: string): never {
  throw new Rejection(code, message, id);
}

type Fields = Record<string, unknown>;

function isObject(value: unknown): value is Fields {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function reqString(obj: Fields, key: string, where: string, limit?: number): string {
  const value = obj[key];
  if (typeof value !== "string") reject("INVALID_MESSAGE", `${where}.${key} must be a string`);
  return utf8Truncate(value, limit ?? Infinity);
}

function optString(obj: Fields, key: string, where: string, limit: number): string | undefined {
  if (obj[key] === undefined) return undefined;
  return reqString(obj, key, where, limit);
}

function reqInt(obj: Fields, key: string, where: string, min: number, max: number): number {
  const value = obj[key];
  if (typeof value !== "number" || !Number.isInteger(value) || value < min || value > max) {
    reject("INVALID_MESSAGE", `${where}.${key} must be an integer in ${min}..${max}`);
  }
  return value;
}

function reqEnum<T extends string>(obj: Fields, key: string, where: string, values: readonly T[]): T {
  const value = obj[key];
  if (typeof value !== "string" || !(values as readonly string[]).includes(value)) {
    reject("INVALID_MESSAGE", `${where}.${key} must be one of ${values.join(", ")}`);
  }
  return value as T;
}

function optEnum<T extends string>(obj: Fields, key: string, where: string, values: readonly T[]): T | undefined {
  if (obj[key] === undefined) return undefined;
  return reqEnum(obj, key, where, values);
}

function optSid(obj: Fields, where: string): string | undefined {
  const value = obj.sid;
  if (value === undefined) return undefined;
  if (typeof value !== "string" || !SID_RE.test(value)) reject("INVALID_MESSAGE", `${where}.sid must be 6 hex characters`);
  return value;
}

function reqRequestId(obj: Fields, where: string): string {
  const value = obj.id;
  if (typeof value !== "string" || !REQUEST_ID_RE.test(value)) {
    reject("INVALID_MESSAGE", `${where}.id must be a 32 hex character request id`);
  }
  return value;
}

/** Copies the defined entries only, so an absent field stays absent. */
function compact<T extends object>(obj: T): T {
  const out: Fields = {};
  for (const [key, value] of Object.entries(obj)) if (value !== undefined) out[key] = value;
  return out as T;
}

const MAX_SAFE = Number.MAX_SAFE_INTEGER;

function parseRegister(obj: Fields, tokenKind: TokenKind | undefined): RegisterMessage {
  const role = reqEnum(obj, "role", "register", ROLES);
  if (obj.proto !== PROTO_VERSION) {
    reject("UNSUPPORTED_PROTO", `register.proto must be ${PROTO_VERSION}`);
  }
  if (!isObject(obj.client)) reject("INVALID_MESSAGE", "register.client must be an object");
  const client = obj.client;
  const kind = client.kind;
  if (typeof kind !== "string") reject("INVALID_MESSAGE", "register.client.kind must be a string");
  const allowedForRole: readonly string[] = role === "producer" ? PRODUCER_KINDS : CONSUMER_KINDS;
  if (!(PRODUCER_KINDS as readonly string[]).includes(kind) && !(CONSUMER_KINDS as readonly string[]).includes(kind)) {
    reject("INVALID_MESSAGE", `register.client.kind "${utf8Truncate(kind, LIMITS.name)}" is unknown`);
  }
  if (!allowedForRole.includes(kind)) reject("NOT_ALLOWED", `a ${role} cannot register as ${kind}`);
  if (tokenKind === "device" && kind !== "device" && kind !== "sim") {
    reject("NOT_ALLOWED", `a device token cannot register as ${kind}`);
  }
  if (tokenKind === "cli" && kind === "device") {
    reject("NOT_ALLOWED", "kind device requires a device token");
  }
  let deviceId: string | undefined;
  if (kind === "device" || kind === "sim") {
    if (!isValidDeviceId(client.deviceId)) {
      reject("INVALID_MESSAGE", "register.client.deviceId must match dv-<6 hex> or sim-<1..12 of a-z0-9->");
    }
    deviceId = client.deviceId;
  }
  const out: RegisterClient = compact({
    kind: kind as ClientKind,
    deviceId,
    model: optString(client, "model", "register.client", LIMITS.name),
    version: optString(client, "version", "register.client", LIMITS.name),
    project: role === "producer" ? optString(client, "project", "register.client", LIMITS.project) : undefined,
  });
  return { type: "register", role, proto: PROTO_VERSION, client: out };
}

function parseStatus(obj: Fields): StatusMessage {
  return compact({
    type: "status" as const,
    msg: reqString(obj, "msg", "status", LIMITS.msg),
    mood: optEnum(obj, "mood", "status", MOODS),
    sid: optSid(obj, "status"),
    project: optString(obj, "project", "status", LIMITS.project),
  });
}

function parseNotify(obj: Fields): NotifyMessage {
  const event = reqString(obj, "event", "notify", LIMITS.event);
  if (!NOTIFY_EVENT_RE.test(event)) reject("INVALID_MESSAGE", "notify.event must match [a-z0-9_]+");
  return compact({
    type: "notify" as const,
    event,
    mood: reqEnum(obj, "mood", "notify", MOODS),
    detail: optString(obj, "detail", "notify", LIMITS.detail),
    urgency: obj.urgency === undefined ? undefined : reqInt(obj, "urgency", "notify", 0, MAX_URGENCY),
    source: optString(obj, "source", "notify", LIMITS.source),
    sid: optSid(obj, "notify"),
    project: optString(obj, "project", "notify", LIMITS.project),
  });
}

function parseMood(obj: Fields): MoodMessage {
  return compact({
    type: "mood" as const,
    mood: reqEnum(obj, "mood", "mood", MOODS),
    reason: optString(obj, "reason", "mood", LIMITS.reason),
  });
}

function parseHolding(obj: Fields): HoldingMessage {
  const ids = obj.ids;
  if (!Array.isArray(ids) || ids.length < 1 || ids.length > HOLDING_MAX_IDS) {
    reject("INVALID_MESSAGE", `holding.ids must be an array of 1..${HOLDING_MAX_IDS} request ids`);
  }
  if (!ids.every((id) => typeof id === "string")) reject("INVALID_MESSAGE", "holding.ids must contain strings");
  // A malformed id can never be pending; protocol 4.7 ignores unknown ids silently.
  const wellFormed = (ids as string[]).filter((id) => REQUEST_ID_RE.test(id));
  return { type: "holding", ids: [...new Set(wellFormed)] };
}

function parsePermissionResponse(obj: Fields): PermissionResponseMessage {
  const id = reqRequestId(obj, "permission_response");
  if (typeof obj.granted !== "boolean") {
    reject("INVALID_MESSAGE", "permission_response.granted must be a boolean", id);
  }
  return { type: "permission_response", id, granted: obj.granted };
}

function parsePermissionDefer(obj: Fields): PermissionDeferMessage {
  return { type: "permission_defer", id: reqRequestId(obj, "permission_defer") };
}

function parseTelemetry(obj: Fields): TelemetryMessage {
  if (!isObject(obj.battery)) reject("INVALID_MESSAGE", "telemetry.battery must be an object");
  const battery = obj.battery;
  const pct = battery.pct === null ? null : reqInt(battery, "pct", "telemetry.battery", 0, 100);
  const out: TelemetryBattery = {
    state: reqEnum(battery, "state", "telemetry.battery", BATTERY_STATES),
    pct,
    mv: reqInt(battery, "mv", "telemetry.battery", 0, MAX_SAFE),
  };
  return compact({
    type: "telemetry" as const,
    battery: out,
    power: reqEnum(obj, "power", "telemetry", POWER_SOURCES),
    rssi: reqInt(obj, "rssi", "telemetry", -MAX_SAFE, MAX_SAFE),
    uptimeS: reqInt(obj, "uptimeS", "telemetry", 0, MAX_SAFE),
    heapFree: reqInt(obj, "heapFree", "telemetry", 0, MAX_SAFE),
    heapMin: reqInt(obj, "heapMin", "telemetry", 0, MAX_SAFE),
    resetReason: reqString(obj, "resetReason", "telemetry", LIMITS.name),
    fw: reqString(obj, "fw", "telemetry", LIMITS.name),
    screen: optString(obj, "screen", "telemetry", LIMITS.name),
    stage: optEnum(obj, "stage", "telemetry", POWER_STAGES),
    alert: optEnum(obj, "alert", "telemetry", BATTERY_ALERTS),
  });
}

function parseFields(obj: Fields, type: InboundMessage["type"], tokenKind: TokenKind | undefined): InboundMessage {
  switch (type) {
    case "register":
      return parseRegister(obj, tokenKind);
    case "ping":
      return { type: "ping" };
    case "pong":
      return { type: "pong" };
    case "status":
      return parseStatus(obj);
    case "notify":
      return parseNotify(obj);
    case "mood":
      return parseMood(obj);
    case "holding":
      return parseHolding(obj);
    case "permission_response":
      return parsePermissionResponse(obj);
    case "permission_defer":
      return parsePermissionDefer(obj);
    case "set_mode":
      return { type: "set_mode", mode: reqEnum(obj, "mode", "set_mode", ROOM_MODES) };
    case "telemetry":
      return parseTelemetry(obj);
  }
}

const decoder = new TextDecoder();

/**
 * Validates one inbound WebSocket text frame for a socket in the given state.
 * On success the message contains only known fields, with every string cut
 * to its byte limit. On failure the result carries the error to send back.
 */
export function parseInbound(raw: string | Uint8Array, ctx: InboundContext): ParseResult {
  const firstFrame = ctx.role === "unregistered";
  const failure = (code: ErrorCode, message: string, id?: string): ParseFailure => {
    const out: ParseFailure = { ok: false, code, message };
    if (id !== undefined) out.id = id;
    if (firstFrame) out.close = code === "UNSUPPORTED_PROTO" ? CLOSE_CODES.UNSUPPORTED_PROTO : CLOSE_CODES.POLICY_VIOLATION;
    return out;
  };

  const bytes = typeof raw === "string" ? utf8ByteLength(raw) : raw.byteLength;
  if (bytes > MAX_FRAME_BYTES) return failure("MESSAGE_TOO_LARGE", `frame of ${bytes} bytes exceeds ${MAX_FRAME_BYTES}`);

  let parsed: unknown;
  try {
    parsed = JSON.parse(typeof raw === "string" ? raw : decoder.decode(raw));
  } catch {
    return failure("INVALID_MESSAGE", "frame is not valid JSON");
  }
  if (!isObject(parsed)) return failure("INVALID_MESSAGE", "frame must be a JSON object");
  const type = parsed.type;
  if (typeof type !== "string") return failure("INVALID_MESSAGE", "type must be a string");

  if (firstFrame) {
    if (type !== "register") return failure("INVALID_MESSAGE", "the first message must be register");
  } else if (type === "register") {
    return failure("INVALID_MESSAGE", "already registered");
  } else if (!(INBOUND_TYPES[ctx.role] as readonly string[]).includes(type)) {
    return failure("NOT_ALLOWED", `a ${ctx.role} may not send ${utf8Truncate(type, LIMITS.event)}`);
  }

  try {
    return { ok: true, msg: parseFields(parsed, type as InboundMessage["type"], ctx.tokenKind) };
  } catch (err) {
    if (err instanceof Rejection) return failure(err.code, err.message, err.id);
    throw err;
  }
}

// --- Building ---

export class FrameTooLargeError extends RangeError {
  constructor(readonly bytes: number) {
    super(`serialized frame of ${bytes} bytes exceeds ${MAX_FRAME_BYTES}`);
  }
}

/** The one way to turn a message into a frame. Throws if it exceeds 2048 bytes. */
export function serialize(msg: ProtocolMessage): string {
  const frame = JSON.stringify(msg);
  const bytes = utf8ByteLength(frame);
  if (bytes > MAX_FRAME_BYTES) throw new FrameTooLargeError(bytes);
  return frame;
}

/**
 * Shortens the listed string fields, in order, until the serialized frame
 * fits. Removing n raw bytes removes at least n serialized bytes (JSON
 * escaping only ever grows a character), so one cut per field is enough;
 * the loop guards the code point rounding.
 */
function fitFrame<T extends object>(msg: T, shrinkable: { key: keyof T & string; optional: boolean }[]): T {
  const fields = msg as Fields;
  let size = utf8ByteLength(JSON.stringify(msg));
  for (const { key, optional } of shrinkable) {
    while (size > MAX_FRAME_BYTES && typeof fields[key] === "string" && fields[key] !== "") {
      const value = fields[key] as string;
      fields[key] = utf8Truncate(value, utf8ByteLength(value) - (size - MAX_FRAME_BYTES));
      if (optional && fields[key] === "") delete fields[key];
      size = utf8ByteLength(JSON.stringify(msg));
    }
  }
  if (size > MAX_FRAME_BYTES) throw new FrameTooLargeError(size);
  return msg;
}

function assertMember<T extends string>(value: string, values: readonly T[], what: string): asserts value is T {
  if (!(values as readonly string[]).includes(value)) throw new TypeError(`${what} "${value}" is not valid`);
}

function assertRequestId(id: string): void {
  if (!REQUEST_ID_RE.test(id)) throw new TypeError(`request id "${id}" is not 32 hex characters`);
}

function sid(value: string | undefined): string | undefined {
  return value !== undefined && SID_RE.test(value) ? value : undefined;
}

function cut(value: string | undefined, limit: number): string | undefined {
  return value === undefined ? undefined : utf8Truncate(value, limit);
}

export function buildRegister(input: { role: Role; client: RegisterClient }): RegisterMessage {
  const { client } = input;
  return {
    type: "register",
    role: input.role,
    proto: PROTO_VERSION,
    client: compact({
      kind: client.kind,
      deviceId: client.deviceId,
      model: cut(client.model, LIMITS.name),
      version: cut(client.version, LIMITS.name),
      project: cut(client.project, LIMITS.project),
    }),
  };
}

export function buildRegisterAck(input: Omit<RegisterAckMessage, "type" | "proto">): RegisterAckMessage {
  return compact({
    type: "register_ack" as const,
    role: input.role,
    roomId: input.roomId,
    proto: PROTO_VERSION,
    serverVersion: utf8Truncate(input.serverVersion, LIMITS.name),
    serverTime: input.serverTime,
    mode: input.mode,
    peers: buildPeersObject(input.peers),
    device: input.device && { id: input.device.id, name: utf8Truncate(input.device.name, LIMITS.name) },
  });
}

function buildPeersObject(peers: Peers): Peers {
  return { producers: peers.producers, consumers: peers.consumers, devices: peers.devices };
}

export function buildPeers(peers: Peers): PeersMessage {
  return { type: "peers", ...buildPeersObject(peers) };
}

export function buildPing(): PingMessage {
  return { type: "ping" };
}

export function buildPong(): PongMessage {
  return { type: "pong" };
}

export function buildError(code: ErrorCode, message: string, id?: string): ErrorMessage {
  const out: ErrorMessage = { type: "error", code, message: utf8Truncate(message, LIMITS.reason) };
  if (id !== undefined && REQUEST_ID_RE.test(id)) out.id = id;
  return out;
}

export interface StatusInput {
  msg: string;
  mood?: Mood;
  sid?: string;
  project?: string;
  timestamp?: string;
}

/** `mood` is included only when given: an absent mood keeps the pet's mood. */
export function buildStatus(input: StatusInput): StatusMessage {
  if (input.mood !== undefined) assertMember(input.mood, MOODS, "mood");
  return fitFrame(
    compact({
      type: "status" as const,
      msg: utf8Truncate(input.msg, LIMITS.msg),
      mood: input.mood,
      sid: sid(input.sid),
      project: cut(input.project, LIMITS.project),
      timestamp: input.timestamp,
    }),
    [
      { key: "msg", optional: false },
      { key: "project", optional: true },
    ],
  );
}

export interface NotifyInput {
  event: string;
  mood: Mood;
  detail?: string;
  urgency?: number;
  source?: string;
  sid?: string;
  project?: string;
  timestamp?: string;
}

export function buildNotify(input: NotifyInput): NotifyMessage {
  const event = utf8Truncate(input.event, LIMITS.event);
  if (!NOTIFY_EVENT_RE.test(event)) throw new TypeError(`notify event "${event}" must match [a-z0-9_]+`);
  assertMember(input.mood, MOODS, "mood");
  const urgency =
    input.urgency === undefined ? undefined : Math.min(MAX_URGENCY, Math.max(0, Math.round(input.urgency)));
  return fitFrame(
    compact({
      type: "notify" as const,
      event,
      mood: input.mood,
      detail: cut(input.detail, LIMITS.detail),
      urgency,
      source: cut(input.source, LIMITS.source),
      sid: sid(input.sid),
      project: cut(input.project, LIMITS.project),
      timestamp: input.timestamp,
    }),
    [
      { key: "detail", optional: true },
      { key: "source", optional: true },
      { key: "project", optional: true },
    ],
  );
}

export function buildMood(input: { mood: Mood; reason?: string; timestamp?: string }): MoodMessage {
  assertMember(input.mood, MOODS, "mood");
  return fitFrame(
    compact({
      type: "mood" as const,
      mood: input.mood,
      reason: cut(input.reason, LIMITS.reason),
      timestamp: input.timestamp,
    }),
    [{ key: "reason", optional: true }],
  );
}

export interface PermissionInput {
  id: string;
  tool: string;
  summary: string;
  details?: string;
  risk: Risk;
  sid?: string;
  project?: string;
  /** Remaining hold time in ms; raised to 1000 if lower. */
  timeout: number;
  timestamp?: string;
}

/**
 * Device-facing text (tool, summary, details, project) is restricted to
 * printable ASCII plus newline and cut to its limit; `details` and then
 * `summary` are shortened further if JSON escaping would exceed 2048 bytes.
 */
export function buildPermission(input: PermissionInput): PermissionMessage {
  assertRequestId(input.id);
  assertMember(input.risk, RISK_LEVELS, "risk");
  if (!Number.isFinite(input.timeout)) throw new TypeError("permission timeout must be a finite number");
  const ascii = (value: string | undefined, limit: number) =>
    value === undefined ? undefined : utf8Truncate(sanitizeAscii(value), limit);
  const details = ascii(input.details, LIMITS.details);
  const project = ascii(input.project, LIMITS.project);
  return fitFrame(
    compact({
      type: "permission" as const,
      id: input.id,
      tool: ascii(input.tool, LIMITS.tool) as string,
      summary: ascii(input.summary, LIMITS.summary) as string,
      details: details === "" ? undefined : details,
      risk: input.risk,
      sid: sid(input.sid),
      project: project === "" ? undefined : project,
      timeout: Math.max(MIN_REPLAY_REMAINING_MS, Math.round(input.timeout)),
      timestamp: input.timestamp,
    }),
    [
      { key: "details", optional: true },
      { key: "summary", optional: false },
      { key: "project", optional: true },
    ],
  );
}

export type DismissInput =
  | { id: string; reason: "answered"; outcome: AnswerOutcome; by: string }
  | { id: string; reason: "cancelled" | "timeout" | "released" };

export function buildDismiss(input: DismissInput): DismissMessage {
  assertRequestId(input.id);
  if (input.reason === "answered") {
    assertMember(input.outcome, ["allow", "deny"] as const, "outcome");
    return { type: "dismiss", id: input.id, reason: "answered", outcome: input.outcome, by: utf8Truncate(input.by, LIMITS.name) };
  }
  assertMember(input.reason, ["cancelled", "timeout", "released"] as const, "dismiss reason");
  return { type: "dismiss", id: input.id, reason: input.reason };
}

export function buildMode(mode: RoomMode): ModeMessage {
  assertMember(mode, ROOM_MODES, "mode");
  return { type: "mode", mode };
}

export function buildSetMode(mode: RoomMode): SetModeMessage {
  assertMember(mode, ROOM_MODES, "mode");
  return { type: "set_mode", mode };
}

/** `durationMs` is clamped to 1000..10000. */
export function buildIdentify(durationMs: number = DEFAULT_IDENTIFY_MS): IdentifyMessage {
  const value = Number.isFinite(durationMs) ? Math.round(durationMs) : DEFAULT_IDENTIFY_MS;
  return { type: "identify", durationMs: Math.min(IDENTIFY_MAX_MS, Math.max(IDENTIFY_MIN_MS, value)) };
}

/** Keeps well-formed ids only, without duplicates, at most 8. */
export function buildHolding(ids: readonly string[]): HoldingMessage {
  const wellFormed = [...new Set(ids.filter((id) => REQUEST_ID_RE.test(id)))];
  return { type: "holding", ids: wellFormed.slice(0, HOLDING_MAX_IDS) };
}

export function buildPermissionResponse(id: string, granted: boolean): PermissionResponseMessage {
  assertRequestId(id);
  return { type: "permission_response", id, granted: granted === true };
}

export function buildPermissionDefer(id: string): PermissionDeferMessage {
  assertRequestId(id);
  return { type: "permission_defer", id };
}

export function buildTelemetry(input: Omit<TelemetryMessage, "type">): TelemetryMessage {
  return compact({
    type: "telemetry" as const,
    battery: { state: input.battery.state, pct: input.battery.pct, mv: input.battery.mv },
    power: input.power,
    rssi: input.rssi,
    uptimeS: input.uptimeS,
    heapFree: input.heapFree,
    heapMin: input.heapMin,
    resetReason: utf8Truncate(input.resetReason, LIMITS.name),
    fw: utf8Truncate(input.fw, LIMITS.name),
    screen: cut(input.screen, LIMITS.name),
    stage: input.stage,
    alert: input.alert,
  });
}
