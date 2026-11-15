// View state of the dashboard as a pure reducer over protocol messages and UI
// actions. The request rules mirror the device (protocol 4.6-4.10): a request
// leaves the list only on dismiss, on an error naming it, or at its local
// deadline; status messages never hide requests.

import {
  EXTEND_MS,
  type DecideChoice,
  type Device,
  type DismissChoice,
  type Mood,
  type PermissionGrant,
  type Peers,
  type PetListEntry,
  type PetResultError,
  type PetsMessage,
  type PetsResult,
  type QuestionAnswer,
  type QuestionMessage,
  type Risk,
  type Room,
  type RoomDiagnostics,
  type RoomMode,
  type RoomSettings,
  type ServerMode,
  type ServerToConsumerMessage,
} from "@devotchi/shared";

export const LOG_MAX = 200;
/** A response without a dismiss after this long gets an error note and its buttons back. */
export const SEND_TIMEOUT_MS = 3000;
export const TOAST_MS = 6000;
/** Protocol 14.6: at most one `extend` per request in this many ms ... */
export const EXTEND_GAP_MS = 5000;
/** ... and only while less than this remains. */
export const EXTEND_BELOW_MS = EXTEND_MS - 5000;

export type ConnectionPhase = "connecting" | "open" | "reconnecting" | "stopped";

export interface Connection {
  phase: ConnectionPhase;
  /** Epoch ms of the next attempt while reconnecting. */
  retryAt: number | null;
  reason: string | null;
}

/** What a person chose on a card; main.ts turns it into the wire message. */
export type Reply =
  | { kind: "allow" }
  | { kind: "deny" }
  | { kind: "defer" }
  | { kind: "decide"; choice: DecideChoice }
  | { kind: "answers"; answers: QuestionAnswer[] };

/** The full request from `GET /api/room/requests/:id` (protocol 14.6a). */
export type FullText = { state: "idle" | "loading" | "failed" } | { state: "loaded"; toolInput: unknown };

interface CardBase {
  id: string;
  /** Name in receipts and the log: the tool, "Question" or "Plan". */
  label: string;
  project: string | null;
  sid: string | null;
  receivedAt: number;
  /** Local deadline: receipt time + the latest `timeout` or `deadline` the server sent. */
  deadline: number;
  sending: { reply: Reply; at: number } | null;
  error: string | null;
  /** When this dashboard last sent `extend` for the request. */
  extendedAt: number | null;
  full: FullText;
}

export interface PermissionCard extends CardBase {
  kind: "permission";
  tool: string;
  summary: string;
  details: string | null;
  risk: Risk;
  /** Protocol 14.2 card fields; null from a server without them. */
  cmd: string | null;
  cut: boolean;
  desc: string | null;
  reason: string | null;
  grant: PermissionGrant | null;
}

export interface QuestionCard extends CardBase {
  kind: "question";
  count: number;
  /** By `index`; null until that frame arrived. */
  questions: (QuestionMessage | null)[];
}

export interface PlanCard extends CardBase {
  kind: "plan";
  title: string;
  steps: string[];
  more: number;
  cut: boolean;
}

export type RequestCard = PermissionCard | QuestionCard | PlanCard;

/** A question is presented (and named in `holding`) once all `count` frames arrived (protocol 14.4). */
export function isPresentable(card: RequestCard): boolean {
  return card.kind !== "question" || card.questions.every((q) => q !== null);
}

/** Questions and plans, and permissions whose `cmd` was shortened, are shown from the full request. */
export function needsFullText(card: RequestCard): boolean {
  return card.kind !== "permission" || card.cut;
}

/** Whether a click or key on the card should send `extend` now (protocol 14.6). */
export function extendDue(card: RequestCard, now: number): boolean {
  if (card.kind === "permission" || !isPresentable(card)) return false;
  if (card.deadline - now >= EXTEND_BELOW_MS) return false;
  return card.extendedAt === null || now - card.extendedAt >= EXTEND_GAP_MS;
}

export interface LogEntry {
  at: string;
  kind: string;
  text: string;
}

export interface Toast {
  id: number;
  text: string;
  until: number;
}

export interface DashboardConfig {
  serverUrl: string;
  serverVersion: string;
  mode: ServerMode;
  local: boolean;
}

export interface State {
  connection: Connection;
  mode: RoomMode | null;
  settings: RoomSettings | null;
  peers: Peers | null;
  pet: { mood: Mood; status: string | null; project: string | null };
  requests: RequestCard[];
  log: LogEntry[];
  devices: Device[];
  devicesAt: number | null;
  toasts: Toast[];
  nextToastId: number;
  diagnostics: RoomDiagnostics | null;
  config: DashboardConfig | null;
  /** Protocol 14.8: the catalog and the room's uploads; null until loaded. */
  pets: PetListEntry[] | null;
  /** The last answer of each device to a pet command, by device id. */
  petResults: Record<string, { result: PetsResult; at: number }>;
}

export type Action =
  | { type: "frame"; msg: ServerToConsumerMessage; now: number }
  | { type: "connection"; phase: ConnectionPhase; retryAt?: number; reason?: string; now: number }
  | { type: "send"; id: string; reply: Reply; now: number }
  | { type: "extend"; id: string; now: number }
  | { type: "detail"; id: string; full: FullText }
  | { type: "tick"; now: number }
  | { type: "devices"; devices: Device[]; now: number }
  | { type: "room"; room: Room; now: number }
  | { type: "settings"; settings: RoomSettings }
  | { type: "config"; config: DashboardConfig }
  | { type: "pets"; pets: PetListEntry[] }
  | { type: "toast"; text: string; now: number };

export function initialState(): State {
  return {
    connection: { phase: "connecting", retryAt: null, reason: null },
    mode: null,
    settings: null,
    peers: null,
    pet: { mood: "sleeping", status: null, project: null },
    requests: [],
    log: [],
    devices: [],
    devicesAt: null,
    toasts: [],
    nextToastId: 1,
    diagnostics: null,
    config: null,
    pets: null,
    petResults: {},
  };
}

function logged(state: State, kind: string, text: string, now: number, timestamp?: string): State {
  const at = timestamp !== undefined && !Number.isNaN(Date.parse(timestamp)) ? timestamp : new Date(now).toISOString();
  return { ...state, log: [{ at, kind, text }, ...state.log].slice(0, LOG_MAX) };
}

function toasted(state: State, text: string, now: number): State {
  return {
    ...state,
    toasts: [...state.toasts, { id: state.nextToastId, text, until: now + TOAST_MS }],
    nextToastId: state.nextToastId + 1,
  };
}

/** Removes a request and leaves a short note in the toasts and the log. */
function removed(state: State, id: string, note: string, now: number): State {
  const card = state.requests.find((r) => r.id === id);
  if (card === undefined) return state;
  const next = { ...state, requests: state.requests.filter((r) => r.id !== id) };
  const text = `${card.label}: ${note}`;
  return logged(toasted(next, text, now), "request", text, now);
}

function patchRequest(state: State, id: string, patch: Partial<CardBase>): State {
  return { ...state, requests: state.requests.map((r) => (r.id === id ? { ...r, ...patch } : r)) };
}

export function deviceName(state: State, id: string): string {
  if (id === "dashboard") return "the dashboard";
  return state.devices.find((d) => d.id === id)?.name ?? id;
}

/** Receipt words per `dismiss.choice` (protocol 14.3, ux-flows 3.2). */
export const CHOICE_TEXT: Record<DismissChoice, string> = {
  allow_session: "allowed for this session",
  deny_ask_why: "denied, Claude was asked why",
  deny_stop: "denied, Claude stopped",
  approve: "plan approved, each edit still asks",
  approve_accept_edits: "plan approved, edits accepted",
  revise: "sent back, Claude keeps planning",
  decline: "declined to answer",
  answered: "answers sent",
};

function answerLabels(card: QuestionCard, answers: QuestionAnswer[]): string {
  return answers
    .map((answer, i) => {
      const q = card.questions[i];
      if (answer === null) return "no preference";
      if (answer.length === 0) return "none of these";
      return answer.map((n) => q?.options[n]?.label ?? `#${n + 1}`).join(", ");
    })
    .join("; ");
}

function answeredText(card: RequestCard | undefined, msg: Extract<ServerToConsumerMessage, { type: "dismiss"; reason: "answered" }>): string {
  if (msg.choice === undefined) return msg.outcome === "allow" ? "allowed" : "denied, Claude was told to try another way";
  const words = CHOICE_TEXT[msg.choice] ?? msg.choice;
  if (msg.choice === "allow_session" && card?.kind === "permission" && card.grant !== null) {
    return `${words}: ${card.grant.rule}${card.grant.match === "prefix" ? " …" : ""}`;
  }
  // Only this dashboard knows which labels it sent.
  if (msg.choice === "answered" && msg.by === "dashboard" && card?.kind === "question" && card.sending?.reply.kind === "answers") {
    return `${words}: ${answerLabels(card, card.sending.reply.answers)}`;
  }
  return words;
}

function dismissNote(state: State, msg: Extract<ServerToConsumerMessage, { type: "dismiss" }>): string {
  switch (msg.reason) {
    case "answered":
      return `${answeredText(state.requests.find((r) => r.id === msg.id), msg)}, answered on ${deviceName(state, msg.by)}`;
    case "cancelled":
      return "handled in terminal";
    case "timeout":
      return "time ran out, sent to terminal";
    case "released":
      return "handed to the terminal";
  }
}

function base(msg: { id: string; project?: string; sid?: string; timeout: number }, label: string, now: number): CardBase {
  return {
    id: msg.id,
    label,
    project: msg.project ?? null,
    sid: msg.sid ?? null,
    receivedAt: now,
    deadline: now + msg.timeout,
    sending: null,
    error: null,
    extendedAt: null,
    full: { state: "idle" },
  };
}

function added(state: State, card: RequestCard, text: string, now: number, timestamp?: string): State {
  return logged({ ...state, requests: [...state.requests, card] }, "request", text, now, timestamp);
}

function onQuestion(state: State, msg: QuestionMessage, now: number): State {
  const existing = state.requests.find((r) => r.id === msg.id);
  if (existing !== undefined && existing.kind !== "question") return state;
  const count = existing?.count ?? msg.count;
  if (msg.count !== count || msg.index < 0 || msg.index >= count) return state;
  const questions = existing?.questions.slice() ?? Array.from({ length: count }, (): QuestionMessage | null => null);
  questions[msg.index] = msg;
  if (existing !== undefined) {
    return { ...state, requests: state.requests.map((r) => (r.id === msg.id ? { ...existing, questions, deadline: now + msg.timeout } : r)) };
  }
  const card: QuestionCard = { ...base(msg, "Question", now), kind: "question", count, questions };
  return added(state, card, `Question: ${msg.text}`, now, msg.timestamp);
}

/** What a device's `pets.result.error` means (protocol 14.8). */
export const PET_ERROR_TEXT: Record<PetResultError, string> = {
  http: "the download from the server failed",
  size: "the pack size did not match",
  crc: "the pack was damaged on the way (CRC)",
  format: "the device cannot read this pack",
  mismatch: "the pack changed on the server; send it again",
  full: "all slots are full; remove a pet on the device first",
  unknown: "the device does not have that pet",
  builtin: "built-in pets cannot be replaced or removed",
  busy: "the device is busy with another download",
  storage: "the device could not store it",
};

export function petResultText(result: PetsResult): string {
  const verb = result.op === "install" ? "install" : result.op === "select" ? "switch to" : result.op === "remove" ? "remove" : result.op;
  const pet = result.pet === "" ? "the pet" : result.pet;
  if (result.ok) return result.op === "install" ? `${pet} installed` : result.op === "select" ? `now shows ${pet}` : `${pet} removed`;
  return `could not ${verb} ${pet}: ${result.error !== undefined ? PET_ERROR_TEXT[result.error] : "unknown error"}`;
}

function onPets(state: State, msg: PetsMessage, now: number): State {
  if (msg.device === undefined) return state;
  const { type: _type, device, result, ...pets } = msg;
  let next: State = { ...state, devices: state.devices.map((d) => (d.id === device ? { ...d, pets } : d)) };
  if (result !== undefined) {
    next = { ...next, petResults: { ...next.petResults, [device]: { result, at: now } } };
    const text = `${deviceName(state, device)}: ${petResultText(result)}`;
    next = logged(toasted(next, text, now), "pets", text, now);
  }
  return next;
}

function onFrame(state: State, msg: ServerToConsumerMessage, now: number): State {
  switch (msg.type) {
    case "register_ack":
      // A new connection: the server replays pending requests right after this.
      return logged(
        { ...state, connection: { phase: "open", retryAt: null, reason: null }, mode: msg.mode, peers: msg.peers, requests: [] },
        "link",
        `connected to room ${msg.roomId}`,
        now,
      );
    case "peers":
      return { ...state, peers: { producers: msg.producers, consumers: msg.consumers, devices: msg.devices } };
    case "status": {
      const pet = {
        mood: msg.mood ?? state.pet.mood,
        status: msg.msg,
        project: msg.project ?? state.pet.project,
      };
      return logged({ ...state, pet }, "status", msg.msg, now, msg.timestamp);
    }
    case "mood":
      return logged(
        { ...state, pet: { ...state.pet, mood: msg.mood } },
        "mood",
        msg.reason !== undefined ? `${msg.mood}: ${msg.reason}` : msg.mood,
        now,
        msg.timestamp,
      );
    case "notify": {
      const text = msg.detail !== undefined ? `${msg.event}: ${msg.detail}` : msg.event;
      const next = { ...state, pet: { ...state.pet, mood: msg.mood } };
      return logged((msg.urgency ?? 1) >= 1 ? toasted(next, text, now) : next, "notify", text, now, msg.timestamp);
    }
    case "permission": {
      if (state.requests.some((r) => r.id === msg.id)) return patchRequest(state, msg.id, { deadline: now + msg.timeout });
      const card: PermissionCard = {
        ...base(msg, msg.tool, now),
        kind: "permission",
        tool: msg.tool,
        summary: msg.summary,
        details: msg.details ?? null,
        risk: msg.risk,
        cmd: msg.cmd ?? null,
        cut: msg.cut === true,
        desc: msg.desc ?? null,
        reason: msg.reason ?? null,
        grant: msg.grant ?? null,
      };
      return added(state, card, `${msg.tool}: ${msg.cmd ?? msg.summary}`, now, msg.timestamp);
    }
    case "question":
      return onQuestion(state, msg, now);
    case "plan": {
      if (state.requests.some((r) => r.id === msg.id)) return patchRequest(state, msg.id, { deadline: now + msg.timeout });
      const card: PlanCard = { ...base(msg, "Plan", now), kind: "plan", title: msg.title, steps: msg.steps, more: msg.more, cut: msg.cut === true };
      return added(state, card, `Plan: ${msg.title}`, now, msg.timestamp);
    }
    case "deadline":
      return patchRequest(state, msg.id, { deadline: now + msg.timeout });
    case "dismiss":
      return removed(state, msg.id, dismissNote(state, msg), now);
    case "error":
      if (msg.id !== undefined && msg.code === "UNKNOWN_REQUEST") return removed(state, msg.id, "no longer pending", now);
      if (msg.id !== undefined && (msg.code === "INVALID_MESSAGE" || msg.code === "NOT_ALLOWED")) {
        return patchRequest(state, msg.id, { sending: null, error: msg.message });
      }
      return logged(toasted(state, `server: ${msg.message}`, now), "error", `${msg.code}: ${msg.message}`, now);
    case "mode":
      return logged(
        { ...state, mode: msg.mode, settings: state.settings === null ? null : { ...state.settings, mode: msg.mode } },
        "mode",
        msg.mode === "desk" ? "at desk: requests go to the terminal" : "ask: requests go to devices",
        now,
      );
    case "pets":
      return onPets(state, msg, now);
    // The dashboard shows the PC's own clock; pet_cmd goes to devices only.
    case "pet_cmd":
    case "time":
    case "identify":
    case "ping":
    case "pong":
      return state;
  }
}

export function reduce(state: State, action: Action): State {
  switch (action.type) {
    case "frame":
      return onFrame(state, action.msg, action.now);
    case "connection": {
      const connection: Connection = { phase: action.phase, retryAt: action.retryAt ?? null, reason: action.reason ?? null };
      if (action.phase === "open" || action.phase === "connecting") return { ...state, connection };
      // The socket is gone: nothing can be answered from here and the server
      // replays what is still pending on the next connection.
      const next = { ...state, connection, requests: [], peers: null };
      return action.reason !== undefined ? logged(next, "link", action.reason, action.now) : next;
    }
    case "send":
      return patchRequest(state, action.id, { sending: { reply: action.reply, at: action.now }, error: null });
    case "extend":
      return patchRequest(state, action.id, { extendedAt: action.now });
    case "detail":
      return patchRequest(state, action.id, { full: action.full });
    case "tick": {
      let next = state;
      for (const r of state.requests) {
        if (r.deadline <= action.now) next = removed(next, r.id, "time ran out, sent to terminal", action.now);
        else if (r.sending !== null && action.now - r.sending.at >= SEND_TIMEOUT_MS) {
          next = patchRequest(next, r.id, { sending: null, error: "no confirmation from the server; try again" });
        }
      }
      const toasts = next.toasts.filter((t) => t.until > action.now);
      return toasts.length === next.toasts.length ? next : { ...next, toasts };
    }
    case "devices":
      return { ...state, devices: action.devices, devicesAt: action.now };
    case "room":
      return {
        ...state,
        settings: action.room.settings,
        mode: action.room.settings.mode,
        diagnostics: action.room.diagnostics,
        devices: action.room.devices,
        devicesAt: action.now,
      };
    case "settings":
      return { ...state, settings: action.settings, mode: action.settings.mode };
    case "config":
      return { ...state, config: action.config };
    case "pets":
      return { ...state, pets: action.pets };
    case "toast":
      return toasted(state, action.text, action.now);
  }
}
