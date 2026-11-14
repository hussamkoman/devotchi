// Environment parsing (architecture 2.6) and validation of the configuration
// handed to createServer. Every failure is a ConfigError with a one-line
// message; index.ts turns it into exit code 2.

import { isIP } from "node:net";
import { homedir } from "node:os";
import { join } from "node:path";
import {
  DEFAULT_SERVER_HOST,
  DEFAULT_SERVER_PORT,
  DEFAULT_SERVER_TIMING,
  HOLD_MAX_MS,
  HOLD_MIN_MS,
  LOG_LEVELS,
  REGISTRATION_MODES,
  type LogLevel,
  type RegistrationMode,
  type ServerConfig,
  type ServerMode,
  type ServerTiming,
} from "@devotchi/shared";
import type { LogSink } from "./log.js";

export class ConfigError extends Error {
  override name = "ConfigError";
}

export const SERVER_MODES: readonly ServerMode[] = ["single-user", "multi-user"];
export const DEFAULT_MAX_SOCKETS = 200;
/** bcrypt cost of multi-user passwords (architecture 2.5). */
export const DEFAULT_BCRYPT_COST = 12;
/** Bind address when the server is embedded rather than started from the environment. */
export const EMBEDDED_HOST = "127.0.0.1";

/**
 * What createServer accepts: a full ServerConfig, or only the fields an
 * embedding caller cares about. `maxSockets`, `logSink` and `bcryptCost`
 * have no environment variable; they exist for embedding and tests.
 */
export interface ServerConfigInput extends Partial<Omit<ServerConfig, "timing" | "dbPath">> {
  /** SQLite file, or ":memory:". */
  dbPath: string;
  timing?: Partial<ServerTiming>;
  /** Open WebSocket connections per process, registered or not. */
  maxSockets?: number;
  /** Receives every log line; defaults to stdout. */
  logSink?: LogSink;
  /** Tests lower it; production keeps DEFAULT_BCRYPT_COST. */
  bcryptCost?: number;
}

export interface ResolvedConfig extends ServerConfig {
  maxSockets: number;
  logSink?: LogSink;
  bcryptCost: number;
}

const HOSTNAME_RE = /^(?=.{1,253}$)[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?(\.[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$/;

function fail(message: string): never {
  throw new ConfigError(message);
}

function checkInt(name: string, value: number, min: number, max: number): void {
  if (!Number.isInteger(value) || value < min || value > max) {
    fail(`${name} must be an integer in ${min}..${max} (got ${String(value)})`);
  }
}

function checkOrigin(origin: string): void {
  let url: URL;
  try {
    url = new URL(origin);
  } catch {
    fail(`DEVOTCHI_CORS_ORIGINS entry "${origin}" is not a URL`);
  }
  if ((url.protocol !== "http:" && url.protocol !== "https:") || url.origin !== origin) {
    fail(`DEVOTCHI_CORS_ORIGINS entry "${origin}" must be an origin like https://example.com (no path, no trailing slash)`);
  }
}

/**
 * Sanity ranges for programmatic timing. They are wider than the environment
 * ranges in loadConfig because tests shrink every timer to milliseconds.
 */
const TIMING_RANGES: Record<keyof ServerTiming, [number, number]> = {
  heartbeatIntervalMs: [1, 600_000],
  heartbeatDeadMs: [1, 3_600_000],
  registerDeadlineMs: [1, 600_000],
  ackMs: [1, 600_000],
  aliveMs: [1, 3_600_000],
  holdMs: [1, HOLD_MAX_MS],
  pairCodeTtlMs: [1, 86_400_000],
  rateWindowMs: [1, 86_400_000],
  holdingRefillMs: [1, 600_000],
  systemMessageIntervalMs: [0, 600_000],
  replayMaxAgeMs: [0, 86_400_000],
  lastUsedWriteMs: [0, 86_400_000],
  hookStatusIntervalMs: [0, 600_000],
  holdWriteMs: [0, 86_400_000],
  sessionTtlMs: [1, 86_400_000],
  pairDeliveryMs: [1, 3_600_000],
  deviceWriteMs: [0, 86_400_000],
  loginSessionTtlMs: [1, 366 * 86_400_000],
};

/** Fills defaults and validates. Throws ConfigError. */
export function resolveConfig(input: ServerConfigInput): ResolvedConfig {
  const timing: ServerTiming = { ...DEFAULT_SERVER_TIMING, ...input.timing };
  for (const [key, [min, max]] of Object.entries(TIMING_RANGES) as [keyof ServerTiming, [number, number]][]) {
    checkInt(`timing.${key}`, timing[key], min, max);
  }
  const config: ResolvedConfig = {
    host: input.host ?? EMBEDDED_HOST,
    port: input.port ?? DEFAULT_SERVER_PORT,
    mode: input.mode ?? "single-user",
    dbPath: input.dbPath,
    corsOrigins: input.corsOrigins ?? [],
    trustProxy: input.trustProxy ?? false,
    adminSecret: input.adminSecret,
    registration: input.registration ?? "open",
    inviteCode: input.inviteCode,
    logLevel: input.logLevel ?? "info",
    timing,
    maxSockets: input.maxSockets ?? DEFAULT_MAX_SOCKETS,
    logSink: input.logSink,
    bcryptCost: input.bcryptCost ?? DEFAULT_BCRYPT_COST,
  };
  if (typeof config.host !== "string" || (isIP(config.host) === 0 && !HOSTNAME_RE.test(config.host))) {
    fail(`host must be an IP address or a hostname (got "${String(config.host)}")`);
  }
  checkInt("port", config.port, 0, 65535);
  if (!SERVER_MODES.includes(config.mode)) fail(`mode must be one of ${SERVER_MODES.join(", ")}`);
  if (typeof config.dbPath !== "string" || config.dbPath === "") fail("dbPath must be a non-empty path");
  for (const origin of config.corsOrigins) checkOrigin(origin);
  if (typeof config.trustProxy !== "boolean") fail("trustProxy must be a boolean");
  if (config.adminSecret !== undefined && config.adminSecret === "") fail("adminSecret must not be empty");
  if (!REGISTRATION_MODES.includes(config.registration)) {
    fail(`registration must be one of ${REGISTRATION_MODES.join(", ")}`);
  }
  if (config.mode === "multi-user" && config.registration === "invite" && !config.inviteCode) {
    fail("DEVOTCHI_REGISTRATION=invite requires DEVOTCHI_INVITE_CODE");
  }
  if (!LOG_LEVELS.includes(config.logLevel)) fail(`logLevel must be one of ${LOG_LEVELS.join(", ")}`);
  checkInt("maxSockets", config.maxSockets, 1, 1_000_000);
  checkInt("bcryptCost", config.bcryptCost, 4, 31);
  return config;
}

export interface EnvConfig {
  config: ResolvedConfig;
  warnings: string[];
}

type Env = Record<string, string | undefined>;

function envValue(env: Env, name: string): string | undefined {
  const value = env[name];
  return value === undefined || value.trim() === "" ? undefined : value.trim();
}

function envInt(env: Env, name: string, fallback: number, min: number, max: number): number {
  const raw = envValue(env, name);
  if (raw === undefined) return fallback;
  if (!/^\d+$/.test(raw)) fail(`${name} must be an integer in ${min}..${max} (got "${raw}")`);
  const value = Number(raw);
  checkInt(name, value, min, max);
  return value;
}

function envEnum<T extends string>(env: Env, name: string, values: readonly T[], fallback: T): T {
  const raw = envValue(env, name);
  if (raw === undefined) return fallback;
  if (!(values as readonly string[]).includes(raw)) fail(`${name} must be one of ${values.join(", ")} (got "${raw}")`);
  return raw as T;
}

/** Reads the environment table of architecture 2.6. Throws ConfigError. */
export function loadConfig(env: Env = process.env): EnvConfig {
  const warnings: string[] = [];
  if (envValue(env, "DEVOTCHI_JWT_SECRET") !== undefined) {
    warnings.push("DEVOTCHI_JWT_SECRET is no longer used and is ignored");
  }

  const trustProxyRaw = envValue(env, "DEVOTCHI_TRUST_PROXY") ?? "0";
  if (trustProxyRaw !== "0" && trustProxyRaw !== "1") {
    fail(`DEVOTCHI_TRUST_PROXY must be 0 or 1 (got "${trustProxyRaw}")`);
  }
  const host = envValue(env, "DEVOTCHI_HOST") ?? DEFAULT_SERVER_HOST;
  if (isIP(host) === 0 && !HOSTNAME_RE.test(host)) {
    fail(`DEVOTCHI_HOST must be an IP address or a hostname (got "${host}")`);
  }
  const corsRaw = envValue(env, "DEVOTCHI_CORS_ORIGINS");
  const corsOrigins = corsRaw === undefined ? [] : corsRaw.split(",").map((s) => s.trim()).filter((s) => s !== "");

  const mode = envEnum<ServerMode>(env, "DEVOTCHI_MODE", SERVER_MODES, "single-user");
  const registration = envEnum<RegistrationMode>(env, "DEVOTCHI_REGISTRATION", REGISTRATION_MODES, "open");
  const inviteCode = envValue(env, "DEVOTCHI_INVITE_CODE");
  if (mode === "multi-user" && registration === "invite" && inviteCode === undefined) {
    fail("DEVOTCHI_REGISTRATION=invite requires DEVOTCHI_INVITE_CODE");
  }

  const timing: ServerTiming = {
    ...DEFAULT_SERVER_TIMING,
    holdMs: envInt(env, "DEVOTCHI_PERMISSION_HOLD_MS", DEFAULT_SERVER_TIMING.holdMs, HOLD_MIN_MS, HOLD_MAX_MS),
    ackMs: envInt(env, "DEVOTCHI_PERMISSION_ACK_MS", DEFAULT_SERVER_TIMING.ackMs, 1000, 60_000),
    // Below the 4 s holding interval every holder would be dropped between heartbeats.
    aliveMs: envInt(env, "DEVOTCHI_PERMISSION_ALIVE_MS", DEFAULT_SERVER_TIMING.aliveMs, 5000, HOLD_MAX_MS),
  };

  const config = resolveConfig({
    host,
    port: envInt(env, "DEVOTCHI_PORT", DEFAULT_SERVER_PORT, 0, 65535),
    mode,
    dbPath: envValue(env, "DEVOTCHI_DB_PATH") ?? join(homedir(), ".devotchi", "devotchi.db"),
    corsOrigins,
    trustProxy: trustProxyRaw === "1",
    adminSecret: envValue(env, "DEVOTCHI_ADMIN_SECRET"),
    registration,
    inviteCode,
    logLevel: envEnum<LogLevel>(env, "DEVOTCHI_LOG_LEVEL", LOG_LEVELS, "info"),
    timing,
  });
  return { config, warnings };
}
