// Configuration shapes shared by the server, the CLI and the embedded
// (--local) server. Environment parsing lives in the packages themselves.

import type { ServerMode } from "./api.js";
import {
  DEFAULT_ACK_MS,
  DEFAULT_ALIVE_MS,
  DEFAULT_HOLD_MS,
  HEARTBEAT_DEAD_MS,
  HEARTBEAT_INTERVAL_MS,
  HOLDING_REFILL_PER_S,
  PAIRING_CODE_TTL_MS,
  REGISTER_DEADLINE_MS,
  REPLAY_MAX_AGE_MS,
} from "./protocol.js";

export const DEFAULT_SERVER_HOST = "0.0.0.0";
export const DEFAULT_SERVER_PORT = 8080;
export const DEFAULT_CLIENT_WEB_PORT = 3456;
export const DEFAULT_SERVER_URL = "http://localhost:8080";

export const LOG_LEVELS = ["debug", "info", "warn", "error"] as const;
export type LogLevel = (typeof LOG_LEVELS)[number];

export const REGISTRATION_MODES = ["open", "invite", "closed"] as const;
export type RegistrationMode = (typeof REGISTRATION_MODES)[number];

/**
 * Timers of one server instance. Production uses the protocol defaults;
 * tests shrink them.
 */
export interface ServerTiming {
  heartbeatIntervalMs: number;
  heartbeatDeadMs: number;
  registerDeadlineMs: number;
  ackMs: number;
  aliveMs: number;
  /** Default room `holdMs` (DEVOTCHI_PERMISSION_HOLD_MS). */
  holdMs: number;
  pairCodeTtlMs: number;
  /** Length of the "per minute" windows of protocol 3.5 and 7.8. */
  rateWindowMs: number;
  /** One `holding` token is added to a socket's bucket per interval (2/s). */
  holdingRefillMs: number;
  /** Minimum spacing of `telemetry` and of `set_mode` on one socket (1/s). */
  systemMessageIntervalMs: number;
  /** Maximum age of the last status and room mood replayed on register. */
  replayMaxAgeMs: number;
  /** A token's `last_used` column is written at most this often. */
  lastUsedWriteMs: number;
}

export const DEFAULT_SERVER_TIMING: Readonly<ServerTiming> = {
  heartbeatIntervalMs: HEARTBEAT_INTERVAL_MS,
  heartbeatDeadMs: HEARTBEAT_DEAD_MS,
  registerDeadlineMs: REGISTER_DEADLINE_MS,
  ackMs: DEFAULT_ACK_MS,
  aliveMs: DEFAULT_ALIVE_MS,
  holdMs: DEFAULT_HOLD_MS,
  pairCodeTtlMs: PAIRING_CODE_TTL_MS,
  rateWindowMs: 60_000,
  holdingRefillMs: 1000 / HOLDING_REFILL_PER_S,
  systemMessageIntervalMs: 1000,
  replayMaxAgeMs: REPLAY_MAX_AGE_MS,
  lastUsedWriteMs: 60_000,
};

/** Architecture section 2.6. */
export interface ServerConfig {
  host: string;
  /** 0 picks a free port. */
  port: number;
  mode: ServerMode;
  dbPath: string;
  /** Browser origins allowed cross-origin access; empty means none. */
  corsOrigins: string[];
  trustProxy: boolean;
  adminSecret?: string;
  registration: RegistrationMode;
  inviteCode?: string;
  logLevel: LogLevel;
  timing: ServerTiming;
}

/** Credentials for one server, keyed by its normalised base URL. */
export interface ServerCredentials {
  token: string;
  roomId: string;
}

/** `<config dir>/config.json`, architecture section 3.1. */
export interface ClientConfig {
  version: 2;
  serverUrl: string;
  webPort: number;
  servers: Record<string, ServerCredentials>;
}

export function defaultClientConfig(): ClientConfig {
  return { version: 2, serverUrl: DEFAULT_SERVER_URL, webPort: DEFAULT_CLIENT_WEB_PORT, servers: {} };
}
