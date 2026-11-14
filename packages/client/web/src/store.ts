// View state of the dashboard as a pure reducer over protocol messages and UI
// actions. The request rules mirror the device (protocol 4.6-4.10): a request
// leaves the list only on dismiss, on an error naming it, or at its local
// deadline; status messages never hide requests.

import type {
  Device,
  Mood,
  Peers,
  Risk,
  Room,
  RoomDiagnostics,
  RoomMode,
  RoomSettings,
  ServerMode,
  ServerToConsumerMessage,
} from "@devotchi/shared";

export const LOG_MAX = 200;
/** A response without a dismiss after this long gets an error note and its buttons back. */
export const SEND_TIMEOUT_MS = 3000;
export const TOAST_MS = 6000;

export type ConnectionPhase = "connecting" | "open" | "reconnecting" | "stopped";

export interface Connection {
  phase: ConnectionPhase;
  /** Epoch ms of the next attempt while reconnecting. */
  retryAt: number | null;
  reason: string | null;
}

export type Answer = "allow" | "deny" | "defer";

export interface RequestCard {
  id: string;
  tool: string;
  summary: string;
  details: string | null;
  risk: Risk;
  project: string | null;
  sid: string | null;
  receivedAt: number;
  /** Local deadline: receipt time + the `timeout` the server sent. */
  deadline: number;
  sending: { answer: Answer; at: number } | null;
  error: string | null;
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
}

export type Action =
  | { type: "frame"; msg: ServerToConsumerMessage; now: number }
  | { type: "connection"; phase: ConnectionPhase; retryAt?: number; reason?: string; now: number }
  | { type: "send"; id: string; answer: Answer; now: number }
  | { type: "tick"; now: number }
  | { type: "devices"; devices: Device[]; now: number }
  | { type: "room"; room: Room; now: number }
  | { type: "settings"; settings: RoomSettings }
  | { type: "config"; config: DashboardConfig }
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
  const text = `${card.tool}: ${note}`;
  return logged(toasted(next, text, now), "request", text, now);
}

function patchRequest(state: State, id: string, patch: Partial<RequestCard>): State {
  return { ...state, requests: state.requests.map((r) => (r.id === id ? { ...r, ...patch } : r)) };
}

export function deviceName(state: State, id: string): string {
  if (id === "dashboard") return "the dashboard";
  return state.devices.find((d) => d.id === id)?.name ?? id;
}

function dismissNote(state: State, msg: Extract<ServerToConsumerMessage, { type: "dismiss" }>): string {
  switch (msg.reason) {
    case "answered":
      return `${msg.outcome === "allow" ? "approved" : "rejected"}, answered on ${deviceName(state, msg.by)}`;
    case "cancelled":
      return "handled in terminal";
    case "timeout":
      return "timed out";
    case "released":
      return "handed to the terminal";
  }
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
      const deadline = now + msg.timeout;
      if (state.requests.some((r) => r.id === msg.id)) return patchRequest(state, msg.id, { deadline });
      const card: RequestCard = {
        id: msg.id,
        tool: msg.tool,
        summary: msg.summary,
        details: msg.details ?? null,
        risk: msg.risk,
        project: msg.project ?? null,
        sid: msg.sid ?? null,
        receivedAt: now,
        deadline,
        sending: null,
        error: null,
      };
      return logged({ ...state, requests: [...state.requests, card] }, "request", `${msg.tool}: ${msg.summary}`, now, msg.timestamp);
    }
    case "dismiss":
      return removed(state, msg.id, dismissNote(state, msg), now);
    case "error":
      if (msg.id !== undefined && msg.code === "UNKNOWN_REQUEST") return removed(state, msg.id, "no longer pending", now);
      if (msg.id !== undefined && msg.code === "INVALID_MESSAGE") {
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
      return patchRequest(state, action.id, { sending: { answer: action.answer, at: action.now }, error: null });
    case "tick": {
      let next = state;
      for (const r of state.requests) {
        if (r.deadline <= action.now) next = removed(next, r.id, "timed out", action.now);
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
    case "toast":
      return toasted(state, action.text, action.now);
  }
}
