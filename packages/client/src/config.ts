// Client configuration (architecture 3.1): <config dir>/config.json with
// credentials stored per server URL. The file is only ever replaced
// atomically, and a file this code cannot understand is never overwritten.

import { randomBytes } from "node:crypto";
import { chmod, mkdir, open, readFile, stat, unlink, writeFile } from "node:fs/promises";
import { homedir } from "node:os";
import { join } from "node:path";
import { z } from "zod";
import {
  DEFAULT_CLIENT_WEB_PORT,
  DEFAULT_SERVER_URL,
  defaultClientConfig,
  type ClientConfig,
  type ServerCredentials,
} from "@devotchi/shared";
import { ConfigError, UsageError, errorMessage } from "./errors.js";
import { isErrno, writeFileAtomic } from "./fsutil.js";

export type Env = Record<string, string | undefined>;

export const ENV_CONFIG_DIR = "DEVOTCHI_CONFIG_DIR";
export const ENV_SERVER_URL = "DEVOTCHI_SERVER_URL";
export const ENV_TOKEN = "DEVOTCHI_TOKEN";

export const CONFIG_FILE = "config.json";
const LOCK_FILE = "config.lock";
/** The v1 local broker handed out this fixed value; it is not a real token. */
const V1_FAKE_TOKEN = "local-token";

/** A lock older than this belongs to a process that hung or crashed mid-write. */
const LOCK_STALE_MS = 60_000;
/** Longer than a health check plus a token request (10 s fetch timeout each). */
const LOCK_WAIT_MS = 30_000;

/**
 * Canonical form of a server base URL: http or https, lower-case host, no
 * default port, no trailing slash, no query. ws/wss given by the user are
 * converted, and a bare host[:port] means http.
 */
export function normaliseServerUrl(input: string): string {
  const raw = input.trim();
  if (raw === "") throw new UsageError("server URL is empty");
  const withScheme = /^[a-z][a-z0-9+.-]*:\/\//i.test(raw) ? raw : `http://${raw}`;
  let url: URL;
  try {
    url = new URL(withScheme);
  } catch {
    throw new UsageError(`invalid server URL "${input}"`);
  }
  if (url.protocol === "ws:") url.protocol = "http:";
  else if (url.protocol === "wss:") url.protocol = "https:";
  if (url.protocol !== "http:" && url.protocol !== "https:") {
    throw new UsageError(`server URL "${input}" must use http, https, ws or wss`);
  }
  if (url.hostname === "") throw new UsageError(`server URL "${input}" has no host`);
  if (url.username !== "" || url.password !== "") throw new UsageError(`server URL "${input}" must not contain credentials`);
  if (url.search !== "" || url.hash !== "") throw new UsageError(`server URL "${input}" must not contain a query or fragment`);
  const path = url.pathname.replace(/\/+$/, "");
  return `${url.protocol}//${url.host}${path}`;
}

/** WebSocket endpoint of a server base URL (protocol 3.1). */
export function serverWsUrl(serverUrl: string): string {
  return serverUrl.replace(/^http/, "ws") + "/ws";
}

export function configDir(env: Env = process.env): string {
  const fromEnv = env[ENV_CONFIG_DIR];
  return fromEnv !== undefined && fromEnv !== "" ? fromEnv : join(homedir(), ".devotchi");
}

const credentialsSchema = z.object({ token: z.string().min(1), roomId: z.string() }).passthrough();

const v2Schema = z
  .object({
    version: z.literal(2),
    serverUrl: z.string().default(DEFAULT_SERVER_URL),
    webPort: z.number().int().min(1).max(65535).default(DEFAULT_CLIENT_WEB_PORT),
    servers: z.record(credentialsSchema).default({}),
  })
  .passthrough();

const v1Schema = z
  .object({
    serverUrl: z.unknown().optional(),
    token: z.unknown().optional(),
    roomId: z.unknown().optional(),
    webPort: z.unknown().optional(),
  })
  .passthrough();

function describeZodError(err: z.ZodError): string {
  const issue = err.issues[0];
  if (issue === undefined) return "invalid content";
  const where = issue.path.length > 0 ? issue.path.join(".") : "top level";
  return `${where}: ${issue.message}`;
}

function parseV2(data: unknown, path: string): ClientConfig {
  const parsed = v2Schema.safeParse(data);
  if (!parsed.success) throw new ConfigError(`${path}: ${describeZodError(parsed.error)}`);
  const config = parsed.data;
  let serverUrl: string;
  try {
    serverUrl = normaliseServerUrl(config.serverUrl);
  } catch (err) {
    throw new ConfigError(`${path}: serverUrl: ${errorMessage(err)}`);
  }
  const servers: Record<string, ServerCredentials> = {};
  for (const [key, creds] of Object.entries(config.servers)) {
    let url: string;
    try {
      url = normaliseServerUrl(key);
    } catch (err) {
      throw new ConfigError(`${path}: servers: ${errorMessage(err)}`);
    }
    servers[url] = creds;
  }
  return { ...config, version: 2, serverUrl, servers };
}

/** v1 was a flat object; its loader silently replaced bad values with defaults, so migration does too. */
function migrateV1(data: unknown, path: string): ClientConfig {
  const parsed = v1Schema.safeParse(data);
  if (!parsed.success) throw new ConfigError(`${path}: ${describeZodError(parsed.error)}`);
  const v1 = parsed.data;
  let serverUrl = DEFAULT_SERVER_URL;
  if (typeof v1.serverUrl === "string") {
    try {
      serverUrl = normaliseServerUrl(v1.serverUrl);
    } catch {
      serverUrl = DEFAULT_SERVER_URL;
    }
  }
  const webPort =
    typeof v1.webPort === "number" && Number.isInteger(v1.webPort) && v1.webPort >= 1 && v1.webPort <= 65535
      ? v1.webPort
      : DEFAULT_CLIENT_WEB_PORT;
  const servers: Record<string, ServerCredentials> = {};
  if (typeof v1.token === "string" && v1.token !== "" && v1.token !== V1_FAKE_TOKEN) {
    servers[serverUrl] = { token: v1.token, roomId: typeof v1.roomId === "string" ? v1.roomId : "" };
  }
  return { version: 2, serverUrl, webPort, servers };
}

function parseConfig(text: string, path: string): { config: ClientConfig; migrated: boolean } {
  let data: unknown;
  try {
    data = JSON.parse(text);
  } catch (err) {
    throw new ConfigError(`${path} is not valid JSON (${errorMessage(err)}); fix or remove it`);
  }
  if (typeof data !== "object" || data === null || Array.isArray(data)) {
    throw new ConfigError(`${path} must contain a JSON object; fix or remove it`);
  }
  const version = (data as { version?: unknown }).version;
  if (version === undefined || version === 1) return { config: migrateV1(data, path), migrated: true };
  if (version === 2) return { config: parseV2(data, path), migrated: false };
  throw new ConfigError(`${path} has version ${JSON.stringify(version)}, which this devotchi does not understand`);
}

function pidAlive(pid: number): boolean {
  try {
    process.kill(pid, 0);
    return true;
  } catch (err) {
    return !isErrno(err, "ESRCH");
  }
}

const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

/** Read and write access to the config file while the lock is held. */
export interface ConfigTx {
  read(): Promise<ClientConfig>;
  write(config: ClientConfig): Promise<void>;
}

export interface ResolvedServer {
  url: string;
  source: "flag" | "env" | "config";
}

export class ConfigStore {
  readonly dir: string;
  readonly path: string;
  private readonly lockPath: string;

  constructor(private readonly env: Env = process.env) {
    this.dir = configDir(env);
    this.path = join(this.dir, CONFIG_FILE);
    this.lockPath = join(this.dir, LOCK_FILE);
  }

  /** The token from DEVOTCHI_TOKEN, which wins over any stored token and is never written. */
  get envToken(): string | undefined {
    const token = this.env[ENV_TOKEN];
    return token !== undefined && token !== "" ? token : undefined;
  }

  /** Reads the file without changing it. A missing file is the default configuration. */
  async read(): Promise<{ config: ClientConfig; exists: boolean; migrated: boolean; text: string | null }> {
    let text: string;
    try {
      text = await readFile(this.path, "utf8");
    } catch (err) {
      if (isErrno(err, "ENOENT")) return { config: defaultClientConfig(), exists: false, migrated: false, text: null };
      throw new ConfigError(`cannot read ${this.path}: ${errorMessage(err)}`);
    }
    return { ...parseConfig(text, this.path), exists: true, text };
  }

  /** The configuration, with a v1 file migrated on disk (its original kept as config.json.v1.bak). */
  async load(): Promise<ClientConfig> {
    const first = await this.read();
    if (!first.migrated) return first.config;
    return this.update(() => undefined);
  }

  async update(change: (config: ClientConfig) => ClientConfig | void): Promise<ClientConfig> {
    return this.withLock(async (tx) => {
      const config = await tx.read();
      const next = change(config) ?? config;
      await tx.write(next);
      return next;
    });
  }

  /**
   * Server URL to use: --server, then DEVOTCHI_SERVER_URL, then the file.
   * `config` skips loading, which would migrate a v1 file on disk.
   */
  async resolveServer(flag?: string, config?: ClientConfig): Promise<ResolvedServer> {
    if (flag !== undefined) return { url: normaliseServerUrl(flag), source: "flag" };
    const fromEnv = this.env[ENV_SERVER_URL];
    if (fromEnv !== undefined && fromEnv !== "") {
      try {
        return { url: normaliseServerUrl(fromEnv), source: "env" };
      } catch (err) {
        throw new ConfigError(`${ENV_SERVER_URL}: ${errorMessage(err)}`);
      }
    }
    return { url: (config ?? (await this.load())).serverUrl, source: "config" };
  }

  async getCredentials(serverUrl: string): Promise<ServerCredentials | null> {
    const config = await this.load();
    return config.servers[normaliseServerUrl(serverUrl)] ?? null;
  }

  async setCredentials(serverUrl: string, creds: ServerCredentials): Promise<void> {
    const url = normaliseServerUrl(serverUrl);
    await this.update((config) => {
      config.servers[url] = { token: creds.token, roomId: creds.roomId };
    });
  }

  async removeCredentials(serverUrl: string): Promise<boolean> {
    const url = normaliseServerUrl(serverUrl);
    let removed = false;
    await this.update((config) => {
      removed = url in config.servers;
      delete config.servers[url];
    });
    return removed;
  }

  async ensureDir(): Promise<void> {
    await mkdir(this.dir, { recursive: true, mode: 0o700 });
    try {
      const info = await stat(this.dir);
      if ((info.mode & 0o077) !== 0) await chmod(this.dir, 0o700);
    } catch {
      // A directory owned by someone else keeps its mode; the files inside are still 0600.
    }
  }

  /**
   * Runs `fn` holding <config dir>/config.lock, so that concurrent devotchi
   * processes (two MCP servers starting at once) do not both provision a
   * token or overwrite each other's changes.
   */
  async withLock<T>(fn: (tx: ConfigTx) => Promise<T>): Promise<T> {
    await this.ensureDir();
    const release = await this.acquireLock();
    try {
      return await fn({
        read: async () => {
          const current = await this.read();
          if (current.migrated && current.text !== null) await this.backupV1(current.text);
          return current.config;
        },
        write: (config) => writeFileAtomic(this.path, JSON.stringify(serialisable(config), null, 2) + "\n", 0o600),
      });
    } finally {
      await release();
    }
  }

  private async backupV1(text: string): Promise<void> {
    try {
      await writeFile(`${this.path}.v1.bak`, text, { mode: 0o600, flag: "wx" });
    } catch (err) {
      if (!isErrno(err, "EEXIST")) throw new ConfigError(`cannot back up ${this.path}: ${errorMessage(err)}`);
    }
  }

  private async acquireLock(): Promise<() => Promise<void>> {
    const started = Date.now();
    const content = `${process.pid} ${Date.now()} ${randomBytes(4).toString("hex")}`;
    for (;;) {
      try {
        const handle = await open(this.lockPath, "wx", 0o600);
        try {
          await handle.writeFile(content, "utf8");
        } finally {
          await handle.close();
        }
        return async () => {
          const current = await readFile(this.lockPath, "utf8").catch(() => null);
          if (current === content) await unlink(this.lockPath).catch(() => undefined);
        };
      } catch (err) {
        if (!isErrno(err, "EEXIST")) throw new ConfigError(`cannot create ${this.lockPath}: ${errorMessage(err)}`);
      }
      const holder = await readFile(this.lockPath, "utf8").catch(() => null);
      if (holder === null) continue;
      const [pidText, timeText] = holder.split(" ");
      const pid = Number(pidText);
      const time = Number(timeText);
      const stale =
        !Number.isInteger(pid) || !Number.isFinite(time) || Date.now() - time > LOCK_STALE_MS || !pidAlive(pid);
      if (stale) {
        const again = await readFile(this.lockPath, "utf8").catch(() => null);
        if (again === holder) await unlink(this.lockPath).catch(() => undefined);
        continue;
      }
      if (Date.now() - started > LOCK_WAIT_MS) {
        throw new ConfigError(
          `${this.lockPath} is held by process ${pid}; if no devotchi command is running, delete the file`,
        );
      }
      await sleep(25 + Math.random() * 50);
    }
  }
}

/** Field order of the written file: the known keys first, then anything a newer client added. */
function serialisable(config: ClientConfig): ClientConfig {
  const { version: _v, serverUrl, webPort, servers, ...rest } = config as ClientConfig & Record<string, unknown>;
  return { version: 2, serverUrl, webPort, servers, ...rest };
}

/** Enough of a token to tell two apart in output, not enough to use it. */
export function maskToken(token: string): string {
  return token.length <= 8 ? "****" : `****${token.slice(-6)}`;
}
