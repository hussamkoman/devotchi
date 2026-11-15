// Runtime guard for frames a consumer receives from the server (section 4).
// Consumers that cannot trust the wire (the browser behind the dashboard
// proxy) use it to drop malformed frames instead of rendering them.

import {
  DISMISS_REASONS,
  ERROR_CODES,
  RISK_LEVELS,
  ROOM_MODES,
  isMood,
  type Mood,
  type ServerToConsumerMessage,
} from "./protocol.js";

type Obj = Record<string, unknown>;

const isObj = (v: unknown): v is Obj => typeof v === "object" && v !== null && !Array.isArray(v);
const isStr = (v: unknown): v is string => typeof v === "string";
const isInt = (v: unknown): v is number => typeof v === "number" && Number.isInteger(v);
const optStr = (v: unknown) => v === undefined || isStr(v);
const oneOf = <T extends string>(list: readonly T[], v: unknown): v is T => isStr(v) && (list as readonly string[]).includes(v);
const optBool = (v: unknown) => v === undefined || v === true;
const isStrArray = (v: unknown) => Array.isArray(v) && v.every(isStr);
const isOption = (v: unknown) => isObj(v) && isStr(v.label) && optStr(v.desc);
const isGrant = (v: unknown) => v === undefined || (isObj(v) && isStr(v.rule) && (v.match === "exact" || v.match === "prefix"));
const isPeers = (v: unknown) => isObj(v) && isInt(v.producers) && isInt(v.consumers) && isInt(v.devices);

/** Section 5.1: consumers map an unknown mood to `idle`. */
export function consumerMood(value: unknown): Mood {
  return isMood(value) ? value : "idle";
}

function valid(m: Obj): boolean {
  switch (m.type) {
    case "ping":
    case "pong":
      return true;
    case "register_ack":
      return isStr(m.role) && isStr(m.roomId) && isInt(m.proto) && oneOf(ROOM_MODES, m.mode) && isPeers(m.peers);
    case "error":
      return oneOf(ERROR_CODES, m.code) && isStr(m.message) && optStr(m.id);
    case "peers":
      return isPeers(m);
    case "status":
      return isStr(m.msg) && optStr(m.sid) && optStr(m.project) && optStr(m.timestamp);
    case "notify":
      return isStr(m.event) && optStr(m.detail) && (m.urgency === undefined || isInt(m.urgency)) && optStr(m.project);
    case "mood":
      return optStr(m.reason);
    case "permission":
      return (
        isStr(m.id) &&
        isStr(m.tool) &&
        isStr(m.summary) &&
        optStr(m.details) &&
        oneOf(RISK_LEVELS, m.risk) &&
        optStr(m.sid) &&
        optStr(m.project) &&
        isInt(m.timeout) &&
        m.timeout > 0 &&
        optStr(m.cmd) &&
        optBool(m.cut) &&
        optStr(m.desc) &&
        optStr(m.reason) &&
        isGrant(m.grant)
      );
    case "question":
      return (
        isStr(m.id) &&
        isInt(m.index) &&
        isInt(m.count) &&
        m.index >= 0 &&
        m.index < m.count &&
        optStr(m.header) &&
        isStr(m.text) &&
        typeof m.multi === "boolean" &&
        Array.isArray(m.options) &&
        m.options.every(isOption) &&
        isInt(m.timeout) &&
        m.timeout > 0
      );
    case "plan":
      return (
        isStr(m.id) &&
        isStr(m.title) &&
        isStrArray(m.steps) &&
        isInt(m.more) &&
        optBool(m.cut) &&
        isInt(m.timeout) &&
        m.timeout > 0
      );
    case "deadline":
      return isStr(m.id) && isInt(m.timeout) && m.timeout > 0;
    case "time":
      return isInt(m.serverTime) && isInt(m.utcOffsetMin);
    case "dismiss":
      if (!isStr(m.id) || !oneOf(DISMISS_REASONS, m.reason)) return false;
      return m.reason !== "answered" || ((m.outcome === "allow" || m.outcome === "deny") && isStr(m.by) && optStr(m.choice));
    case "mode":
      return oneOf(ROOM_MODES, m.mode);
    case "identify":
      return isInt(m.durationMs);
    case "pet_cmd":
      return isStr(m.op) && isStr(m.pet);
    case "pets":
      return (
        optStr(m.device) &&
        isStr(m.selected) &&
        isInt(m.slots) &&
        isInt(m.free) &&
        Array.isArray(m.installed) &&
        m.installed.every((p) => isObj(p) && isStr(p.id) && isInt(p.rev) && optBool(p.builtin)) &&
        (m.result === undefined || (isObj(m.result) && isStr(m.result.op) && isStr(m.result.pet) && typeof m.result.ok === "boolean" && optStr(m.result.error)))
      );
    default:
      return false;
  }
}

/**
 * Parses one text frame from the server. Returns null for anything that is
 * not a well-formed server-to-consumer message. Moods are not rejected but
 * normalised with `consumerMood`, as section 5.1 asks of consumers.
 */
export function parseServerMessage(raw: string): ServerToConsumerMessage | null {
  let m: unknown;
  try {
    m = JSON.parse(raw);
  } catch {
    return null;
  }
  if (!isObj(m) || !valid(m)) return null;
  if (m.type === "notify" || m.type === "mood") m.mood = consumerMood(m.mood);
  if (m.type === "status" && m.mood !== undefined) m.mood = consumerMood(m.mood);
  return m as unknown as ServerToConsumerMessage;
}
