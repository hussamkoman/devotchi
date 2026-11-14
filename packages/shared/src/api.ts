// REST request and response types (protocol section 7) and hook replies
// (section 8.3).

import type { Mood, Peers, Risk, RoomMode, TelemetryMessage, TokenKind } from "./protocol.js";

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

// --- Room (7.6) ---

export interface RoomSettings {
  mode: RoomMode;
  holdMs: number;
  shareText: boolean;
}

export type UpdateRoomSettingsRequest = Partial<RoomSettings>;

export interface PendingRequestInfo {
  id: string;
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

export interface PermissionDecisionReply {
  hookSpecificOutput: {
    hookEventName: "PermissionRequest";
    decision: { behavior: "allow" } | { behavior: "deny"; message: string };
  };
}

export type HookReply = NeutralHookReply | PermissionDecisionReply;
