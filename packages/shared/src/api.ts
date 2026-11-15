// REST request and response types (protocol section 7) and hook replies
// (section 8.3).

import type { ServerFeatures } from "./config.js";
import type {
  Cap,
  DismissChoice,
  Mood,
  Peers,
  PetOp,
  PetsMessage,
  PetsResult,
  RequestKind,
  Risk,
  RoomMode,
  TelemetryMessage,
  TokenKind,
} from "./protocol.js";

export type ServerMode = "single-user" | "multi-user";

// --- Errors ---

export const API_ERROR_CODES = [
  "invalid_request",
  "unauthorized",
  "forbidden",
  "not_found",
  "conflict",
  "expired",
  "already_used",
  "payload_too_large",
  "rate_limited",
  "internal",
] as const;
export type ApiErrorCode = (typeof API_ERROR_CODES)[number];

export interface ApiError {
  error: string;
  code: ApiErrorCode;
}

export const API_BODY_LIMIT_BYTES = 16 * 1024;
export const HOOK_BODY_LIMIT_BYTES = 2 * 1024 * 1024;

// --- Health and identity (7.2) ---

export interface HealthResponse {
  status: "ok";
  version: string;
  proto: number;
  mode: ServerMode;
  uptimeS: number;
}

export interface MeTokenResponse {
  kind: "token";
  roomId: string;
  tokenId: string;
  label: string | null;
  serverMode: ServerMode;
}

export interface MeSessionResponse {
  kind: "session";
  userId: string;
  email: string;
  roomId: string;
  serverMode: ServerMode;
}

export type MeResponse = MeTokenResponse | MeSessionResponse;

// --- Tokens and accounts (7.3) ---

export const SESSION_TOKEN_PREFIX = "dvs_";
export const TOKEN_LABEL_MAX = 64;
export const EMAIL_RE = /^[^@\s]+@[^@\s]+\.[^@\s]+$/;
export const PASSWORD_MIN_BYTES = 10;
export const PASSWORD_MAX_BYTES = 72;

export interface CreateTokenRequest {
  label?: string;
}

export interface CreateTokenResponse {
  token: string;
  id: string;
  roomId: string;
}

export interface TokenInfo {
  id: string;
  label: string | null;
  kind: TokenKind;
  createdAt: string;
  lastUsed: string | null;
}

export interface ListTokensResponse {
  tokens: TokenInfo[];
}

export interface RegisterRequest {
  email: string;
  password: string;
  inviteCode?: string;
}

export interface RegisterResponse {
  userId: string;
}

export interface LoginRequest {
  email: string;
  password: string;
}

export interface LoginResponse {
  sessionToken: string;
  userId: string;
  expiresAt: string;
}

// --- Pairing (7.4) ---

export interface DevicePairStartRequest {
  deviceId: string;
  model?: string;
  fw?: string;
}

export interface DevicePairStartResponse {
  code: string;
  deviceSecret: string;
  expiresInS: number;
  pollIntervalMs: number;
}

export interface DevicePairPollRequest {
  deviceSecret: string;
}

export interface DevicePairPollPending {
  status: "pending";
  expiresInS: number;
}

/** Credentials a device receives from flow A (`poll`) or flow B (`claim`). */
export interface PairedDeviceCredentials {
  token: string;
  roomId: string;
  deviceId: string;
  name: string;
}

export type DevicePairPollResponse = DevicePairPollPending | PairedDeviceCredentials;

export interface PairConfirmRequest {
  code: string;
  name?: string;
}

export interface PairConfirmResponse {
  deviceId: string;
  name: string;
}

export type CreatePairCodeRequest = Record<string, never>;

export interface CreatePairCodeResponse {
  code: string;
  expiresAt: string;
  expiresInS: number;
}

export interface PairClaimRequest {
  code: string;
  deviceId?: string;
  model?: string;
  fw?: string;
}

export type PairClaimResponse = PairedDeviceCredentials;

// --- Devices (7.5) ---

export type DeviceTelemetry = Omit<TelemetryMessage, "type">;

export interface Device {
  id: string;
  name: string;
  model: string | null;
  fw: string | null;
  createdAt: string;
  online: boolean;
  lastSeen: string | null;
  lastHoldAt: string | null;
  holding: number;
  telemetry: DeviceTelemetry | null;
  telemetryAt: string | null;
  /** Protocol 14.8: the last `pets` message of an online device, without `result`. */
  pets?: DevicePets;
  /** Protocol 14.8: the last `result` an online device sent, with the server time it arrived. */
  petsResult?: DevicePetsResult;
}

export type DevicePets = Omit<PetsMessage, "type" | "device" | "result">;

export interface DevicePetsResult extends PetsResult {
  /** ISO 8601 server time the `pets` message with this result arrived. */
  at: string;
}

export interface ListDevicesResponse {
  devices: Device[];
}

export interface RenameDeviceRequest {
  name: string;
}

export interface IdentifyDeviceRequest {
  durationMs?: number;
}

export interface IdentifyDeviceResponse {
  sent: true;
}

// --- Pets (14.8) ---

export type PetSourceKind = "catalog" | "room";

export interface PetListEntry {
  id: string;
  name: string;
  rev: number;
  size: number;
  /** 8 lowercase hex digits: the pack's CRC-32 trailer. */
  crc32: string;
  source: PetSourceKind;
  /**
   * A catalog pet that firmware built from the same repository compiles in
   * (pets/builtin.txt). Informational: what a device really has is the
   * `builtin` flag of its own `pets` message.
   */
  builtin?: true;
}

export interface ListPetsResponse {
  pets: PetListEntry[];
}

export type DevicePetCommandRequest =
  | { op: "install"; pet: string; select?: boolean }
  | { op: Exclude<PetOp, "install">; pet: string };

export interface DevicePetCommandResponse {
  sent: true;
  /** ISO 8601 server time the `pet_cmd` was sent; its answer is a `petsResult` at or after it. */
  at: string;
}

// --- Room (7.6) ---

export interface RoomSettings {
  mode: RoomMode;
  holdMs: number;
  shareText: boolean;
  /** Protocol 14.9: an IANA zone name, or "" for the zone of the server process. */
  timeZone: string;
}

export type UpdateRoomSettingsRequest = Partial<RoomSettings>;

export interface PendingRequestInfo {
  id: string;
  kind: RequestKind;
  tool: string;
  summary: string;
  risk: Risk;
  project?: string;
  sid?: string;
  ageMs: number;
  remainingMs: number;
  holders: string[];
}

export const REQUEST_OUTCOMES = ["allow", "deny", "neutral"] as const;
export type RequestOutcome = (typeof REQUEST_OUTCOMES)[number];

export const REQUEST_DETAILS = [
  "answered",
  "no_consumer",
  "no_ack",
  "holder_lost",
  "deferred",
  "timeout",
  "cancelled",
  "desk",
  "skipped_tool",
  "room_busy",
  "shutdown",
] as const;
export type RequestDetail = (typeof REQUEST_DETAILS)[number];

export interface LastRequest {
  at: string;
  tool: string;
  outcome: RequestOutcome;
  detail: RequestDetail;
  /** Identity of the answering consumer, for `answered`. */
  by?: string;
  /** The `dismiss.choice` of an answer (protocol 14.3). */
  choice?: DismissChoice;
}

/** `GET /api/room/requests/:id` (protocol 14.6a). */
export interface RoomRequestDetail {
  id: string;
  kind: RequestKind;
  /** The hook's `tool_name`. */
  tool: string;
  /** The hook's `tool_input`, unchanged. */
  toolInput: unknown;
  remainingMs: number;
}

export interface RoomDiagnostics {
  dialogWhilePending: { count: number; lastSeenAt: string | null };
}

export interface Room {
  roomId: string;
  settings: RoomSettings;
  peers: Peers;
  pending: PendingRequestInfo[];
  lastRequest: LastRequest | null;
  diagnostics: RoomDiagnostics;
  devices: Device[];
  /** The server's switches of protocol 14.11; absent from servers without phase 2. */
  features?: ServerFeatures;
  /** Connected consumers per cap they declared and the server offers. */
  capableConsumers?: Partial<Record<Cap, number>>;
}

// --- Notifications from scripts (7.7) ---

export interface NotifyRequest {
  event: string;
  mood?: Mood;
  detail?: string;
  source?: string;
  urgency?: number;
}

export interface NotifyResponse {
  delivered: number;
}

// --- Hook replies (8.3) ---

export type NeutralHookReply = Record<string, never>;

/** Protocol 14.3: the only permission updates a reply may carry. */
export type SessionPermissionUpdate =
  | {
      type: "addRules";
      rules: [{ toolName: "Bash"; ruleContent: string }];
      behavior: "allow";
      destination: "session";
    }
  | { type: "setMode"; mode: "default" | "acceptEdits"; destination: "session" };

/** The hook's own `tool_input` object, echoed unchanged in `updatedInput`. */
export type HookToolInput = { [key: string]: unknown };

/** Every `decision` of protocol 14.3 and 14.4, and nothing else. */
export type PermissionDecision =
  | { behavior: "allow" }
  | { behavior: "allow"; updatedPermissions: [SessionPermissionUpdate & { type: "addRules" }] }
  | {
      behavior: "allow";
      updatedInput: HookToolInput;
      updatedPermissions: [SessionPermissionUpdate & { type: "setMode" }];
    }
  | { behavior: "allow"; updatedInput: HookToolInput & { answers: { [question: string]: string } } }
  | { behavior: "deny"; message: string }
  | { behavior: "deny"; message: string; interrupt: true };

export interface PermissionDecisionReply {
  hookSpecificOutput: {
    hookEventName: "PermissionRequest";
    decision: PermissionDecision;
  };
}

export type HookReply = NeutralHookReply | PermissionDecisionReply;
