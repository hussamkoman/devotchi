// The one validator for inbound frames and the builders for every message.
// The server parses each inbound frame with parseInbound and creates each
// frame it sends with a builder; clients use the builders before sending.

import {
  BATTERY_ALERTS,
  BATTERY_STATES,
  CLOSE_CODES,
  CONSUMER_KINDS,
  DECIDE_CHOICES,
  DEFAULT_IDENTIFY_MS,
  GRANT_MATCHES,
  HOLDING_MAX_IDS,
  IDENTIFY_MAX_MS,
  IDENTIFY_MIN_MS,
  INBOUND_TYPES,
  LIMITS,
  MAX_CAPS,
  MAX_FRAME_BYTES,
  MAX_OPTIONS,
  MAX_QUESTIONS,
  MAX_URGENCY,
  MIN_REPLAY_REMAINING_MS,
  MOODS,
  NOTIFY_EVENT_RE,
  PET_ID_RE,
  PET_RESULT_ERRORS,
  PETS_MAX_BYTES,
  PETS_MAX_INSTALLED,
  POWER_SOURCES,
  POWER_STAGES,
  PRODUCER_KINDS,
  PROTO_VERSION,
  REQUEST_ID_RE,
  RISK_LEVELS,
  ROLES,
  ROOM_MODES,
  SID_RE,
  UTC_OFFSET_MAX,
  UTC_OFFSET_MIN,
  isCap,
  isValidDeviceId,
  type AnswerOutcome,
  type Cap,
  type ClientKind,
  type CloseCode,
  type DeadlineMessage,
  type DecideChoice,
  type DecideMessage,
  type DismissChoice,
  type DismissMessage,
  type ErrorCode,
  type ErrorMessage,
  type ExtendMessage,
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
  type PermissionGrant,
  type PermissionMessage,
  type PermissionResponseMessage,
  type PetCmdMessage,
  type PetsEntry,
  type PetsMessage,
  type PetsResult,
  type PingMessage,
  type PongMessage,
  type ProtocolMessage,
  type QuestionAnswer,
  type QuestionResponseMessage,
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
  type TimeMessage,
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
    caps: role === "consumer" ? parseCaps(client.caps) : undefined,
  });
  return { type: "register", role, proto: PROTO_VERSION, client: out };
}

/** Section 14.1: at most 8 strings of at most 16 bytes; unknown names are dropped. */
function parseCaps(value: unknown): Cap[] | undefined {
  if (value === undefined) return undefined;
  if (!Array.isArray(value) || value.length > MAX_CAPS) {
    reject("INVALID_MESSAGE", `register.client.caps must be an array of at most ${MAX_CAPS} strings`);
  }
  if (!value.every((cap) => typeof cap === "string" && utf8ByteLength(cap) <= LIMITS.cap)) {
    reject("INVALID_MESSAGE", `register.client.caps entries must be strings of at most ${LIMITS.cap} bytes`);
  }
  return [...new Set((value as string[]).filter(isCap))];
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

function parseDecide(obj: Fields): DecideMessage {
  const id = reqRequestId(obj, "decide");
  const choice = obj.choice;
  if (typeof choice !== "string" || !(DECIDE_CHOICES as readonly string[]).includes(choice)) {
    reject("INVALID_MESSAGE", `decide.choice must be one of ${DECIDE_CHOICES.join(", ")}`, id);
  }
  return { type: "decide", id, choice: choice as DecideChoice };
}

/**
 * Shape only; whether the answers fit the request's questions is checked by
 * the server against the stored request.
 */
function parseQuestionResponse(obj: Fields): QuestionResponseMessage {
  const id = reqRequestId(obj, "question_response");
  const answers = obj.answers;
  const isIndex = (v: unknown) => typeof v === "number" && Number.isInteger(v) && v >= 0 && v < MAX_OPTIONS;
  const isAnswer = (v: unknown) => v === null || (Array.isArray(v) && v.length <= MAX_OPTIONS && v.every(isIndex));
  if (!Array.isArray(answers) || answers.length < 1 || answers.length > MAX_QUESTIONS || !answers.every(isAnswer)) {
    reject(
      "INVALID_MESSAGE",
      `question_response.answers must be 1..${MAX_QUESTIONS} entries, each null or an array of option indexes`,
      id,
    );
  }
  const copy: QuestionAnswer[] = (answers as (number[] | null)[]).map((a) => (a === null ? null : [...a]));
  return { type: "question_response", id, answers: copy };
}

function parseExtend(obj: Fields): ExtendMessage {
  return { type: "extend", id: reqRequestId(obj, "extend") };
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
    // A malformed pet id is dropped, not rejected: the rest of the sample is still good.
    pet: typeof obj.pet === "string" && PET_ID_RE.test(obj.pet) ? obj.pet : undefined,
  });
}

function reqPetId(obj: Fields, key: string, where: string): string {
  const value = obj[key];
  if (typeof value !== "string" || !PET_ID_RE.test(value)) reject("INVALID_MESSAGE", `${where}.${key} must match [a-z0-9]{1,8}`);
  return value;
}

function parsePets(obj: Fields): PetsMessage {
  const slots = reqInt(obj, "slots", "pets", 0, 255);
  const free = reqInt(obj, "free", "pets", 0, slots);
  if (!Array.isArray(obj.installed) || obj.installed.length > PETS_MAX_INSTALLED) {
    reject("INVALID_MESSAGE", `pets.installed must be an array of at most ${PETS_MAX_INSTALLED} entries`);
  }
  const installed = obj.installed.map((raw): PetsEntry => {
    if (!isObject(raw)) reject("INVALID_MESSAGE", "pets.installed entries must be objects");
    const entry: PetsEntry = { id: reqPetId(raw, "id", "pets.installed[]"), rev: reqInt(raw, "rev", "pets.installed[]", 0, 255) };
    if (raw.builtin !== undefined && typeof raw.builtin !== "boolean") reject("INVALID_MESSAGE", "pets.installed[].builtin must be a boolean");
    if (raw.builtin === true) entry.builtin = true;
    return entry;
  });
  const msg: PetsMessage = { type: "pets", selected: reqPetId(obj, "selected", "pets"), slots, free, installed };
  if (obj.result !== undefined) {
    if (!isObject(obj.result)) reject("INVALID_MESSAGE", "pets.result must be an object");
    const r = obj.result;
    const op = reqString(r, "op", "pets.result");
    if (!/^[a-z_]{1,16}$/.test(op)) reject("INVALID_MESSAGE", "pets.result.op must be 1..16 of [a-z_]");
    const pet = reqString(r, "pet", "pets.result");
    if (pet !== "" && !PET_ID_RE.test(pet)) reject("INVALID_MESSAGE", 'pets.result.pet must be "" or match [a-z0-9]{1,8}');
    if (typeof r.ok !== "boolean") reject("INVALID_MESSAGE", "pets.result.ok must be a boolean");
    const result: PetsResult = { op, pet, ok: r.ok };
    const error = optEnum(r, "error", "pets.result", PET_RESULT_ERRORS);
    if (!r.ok && error === undefined) reject("INVALID_MESSAGE", "pets.result.error is required when ok is false");
    if (!r.ok) result.error = error;
    msg.result = result;
  }
  return msg;
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
    case "decide":
      return parseDecide(obj);
    case "question_response":
      return parseQuestionResponse(obj);
    case "extend":
      return parseExtend(obj);
    case "set_mode":
      return { type: "set_mode", mode: reqEnum(obj, "mode", "set_mode", ROOM_MODES) };
    case "telemetry":
      return parseTelemetry(obj);
    case "pets":
      return parsePets(obj);
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
  if (type === "pets" && bytes > PETS_MAX_BYTES) return failure("MESSAGE_TOO_LARGE", `pets message of ${bytes} bytes exceeds ${PETS_MAX_BYTES}`);

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
      caps: client.caps && [...new Set(client.caps.filter(isCap))].slice(0, MAX_CAPS),
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
    caps: input.caps && [...new Set(input.caps.filter(isCap))],
    utcOffsetMin: input.utcOffsetMin === undefined ? undefined : clampOffset(input.utcOffsetMin),
  });
}

function clampOffset(minutes: number): number {
  const value = Number.isFinite(minutes) ? Math.round(minutes) : 0;
  return Math.min(UTC_OFFSET_MAX, Math.max(UTC_OFFSET_MIN, value));
}

export function buildTime(serverTime: number, utcOffsetMin: number): TimeMessage {
  return { type: "time", serverTime: Math.round(serverTime), utcOffsetMin: clampOffset(utcOffsetMin) };
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
  /** Section 14.2 card fields; consumers with `decide` get these and no `details`. */
  cmd?: string;
  cut?: boolean;
  desc?: string;
  reason?: string;
  grant?: PermissionGrant;
  sid?: string;
  project?: string;
  /** Remaining hold time in ms; raised to 1000 if lower. */
  timeout: number;
  timestamp?: string;
}

/**
 * Device-facing text (tool, summary, details, cmd, desc, reason, project) is
 * restricted to printable ASCII plus newline and cut to its limit. When JSON
 * escaping would exceed 2048 bytes, `details`, `desc`, `cmd` and then
 * `summary` are shortened; a shortened `cmd` sets `cut` and drops `grant`,
 * because a rule is never offered for a command the consumer cannot show whole.
 */
export function buildPermission(input: PermissionInput): PermissionMessage {
  assertRequestId(input.id);
  assertMember(input.risk, RISK_LEVELS, "risk");
  if (!Number.isFinite(input.timeout)) throw new TypeError("permission timeout must be a finite number");
  const ascii = (value: string | undefined, limit: number) =>
    value === undefined ? undefined : utf8Truncate(sanitizeAscii(value), limit);
  const nonEmpty = (value: string | undefined) => (value === "" ? undefined : value);
  const oneLine = (value: string | undefined) => value?.replace(/\n/g, " ");
  const cmd = ascii(input.cmd, LIMITS.cmd);
  let cutFlag = input.cut === true || (input.cmd !== undefined && cmd !== input.cmd);
  let grant: PermissionGrant | undefined;
  if (input.grant !== undefined && !cutFlag) {
    assertMember(input.grant.match, GRANT_MATCHES, "grant match");
    grant = { rule: utf8Truncate(sanitizeAscii(input.grant.rule), LIMITS.grantRule), match: input.grant.match };
    if (grant.rule !== input.grant.rule) throw new TypeError("grant rule must be printable ASCII of at most 120 bytes");
  }
  const msg = fitFrame(
    compact({
      type: "permission" as const,
      id: input.id,
      tool: ascii(input.tool, LIMITS.tool) as string,
      summary: ascii(input.summary, LIMITS.summary) as string,
      details: nonEmpty(ascii(input.details, LIMITS.details)),
      risk: input.risk,
      cmd,
      cut: cutFlag ? (true as const) : undefined,
      desc: nonEmpty(ascii(oneLine(input.desc), LIMITS.desc)),
      reason: nonEmpty(ascii(oneLine(input.reason), LIMITS.riskReason)),
      grant,
      sid: sid(input.sid),
      project: nonEmpty(ascii(input.project, LIMITS.project)),
      timeout: Math.max(MIN_REPLAY_REMAINING_MS, Math.round(input.timeout)),
      timestamp: input.timestamp,
    }),
    [
      { key: "details", optional: true },
      { key: "desc", optional: true },
      { key: "cmd", optional: false },
      { key: "summary", optional: false },
      { key: "project", optional: true },
    ],
  );
  if (msg.cmd !== cmd) cutFlag = true;
  if (cutFlag && (msg.cut !== true || msg.grant !== undefined)) {
    // Rebuilt rather than patched so that the key order stays the same.
    return buildPermission({ ...input, cmd: msg.cmd, cut: true, grant: undefined });
  }
  return msg;
}

export type DismissInput =
  | { id: string; reason: "answered"; outcome: AnswerOutcome; by: string; choice?: DismissChoice }
  | { id: string; reason: "cancelled" | "timeout" | "released" };

export function buildDismiss(input: DismissInput): DismissMessage {
  assertRequestId(input.id);
  if (input.reason === "answered") {
    assertMember(input.outcome, ["allow", "deny"] as const, "outcome");
    const by = utf8Truncate(input.by, LIMITS.name);
    if (input.choice === undefined) return { type: "dismiss", id: input.id, reason: "answered", outcome: input.outcome, by };
    assertMember(input.choice, [...DECIDE_CHOICES, "answered"] as const, "dismiss choice");
    return { type: "dismiss", id: input.id, reason: "answered", outcome: input.outcome, by, choice: input.choice };
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

export function buildDecide(id: string, choice: DecideChoice): DecideMessage {
  assertRequestId(id);
  assertMember(choice, DECIDE_CHOICES, "decide choice");
  return { type: "decide", id, choice };
}

export function buildQuestionResponse(id: string, answers: readonly QuestionAnswer[]): QuestionResponseMessage {
  assertRequestId(id);
  return { type: "question_response", id, answers: answers.map((a) => (a === null ? null : [...a])) };
}

export function buildExtend(id: string): ExtendMessage {
  assertRequestId(id);
  return { type: "extend", id };
}

export function buildDeadline(id: string, timeout: number): DeadlineMessage {
  assertRequestId(id);
  return { type: "deadline", id, timeout: Math.max(MIN_REPLAY_REMAINING_MS, Math.round(timeout)) };
}

export function buildPetCmd(
  input:
    | { op: "install"; pet: string; rev: number; size: number; crc32: string; select?: boolean }
    | { op: "select" | "remove"; pet: string },
): PetCmdMessage {
  if (!PET_ID_RE.test(input.pet)) throw new RangeError("pet_cmd.pet must match [a-z0-9]{1,8}");
  if (input.op !== "install") return { type: "pet_cmd", op: input.op, pet: input.pet };
  if (!Number.isInteger(input.rev) || input.rev < 1 || input.rev > 255) throw new RangeError("pet_cmd.rev must be 1..255");
  if (!Number.isInteger(input.size) || input.size < 36 || input.size > MAX_FRAME_BYTES) throw new RangeError("pet_cmd.size must be 36..2048");
  if (!/^[0-9a-f]{8}$/.test(input.crc32)) throw new RangeError("pet_cmd.crc32 must be 8 lowercase hex digits");
  return { type: "pet_cmd", op: "install", pet: input.pet, rev: input.rev, size: input.size, crc32: input.crc32, select: input.select === true };
}

/** A device's `pets` message; `device` is added by the server for dashboards. */
export function buildPets(input: Omit<PetsMessage, "type">): PetsMessage {
  return compact({ type: "pets" as const, ...input });
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
    pet: input.pet !== undefined && PET_ID_RE.test(input.pet) ? input.pet : undefined,
  });
}
