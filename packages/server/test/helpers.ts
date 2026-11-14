// Test helpers shared by the server tests and reusable by later packages:
// a real server on an ephemeral port with a temporary database, and a
// WebSocket client that queues messages for awaiting.

import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { WebSocket, type RawData } from "ws";
import type { ClientKind, RegisterClient, Role } from "@devotchi/shared";
import { createServer, type DevotchiServer, type ServerConfigInput } from "../src/app.js";
import { allocateDeviceId, insertDevice } from "../src/db/devices.js";
import { insertToken, type MintedToken } from "../src/db/tokens.js";

export type Json = Record<string, unknown>;

export interface TestServer {
  server: DevotchiServer;
  url: string;
  wsUrl: string;
  dir: string;
  dbPath: string;
  /** Every log line the server wrote. */
  logs: string[];
  /** POST /api/auth/token; returns the minted cli token. */
  mintToken(label?: string): Promise<MintedToken>;
  /** A device token plus registry row in the room, inserted directly. */
  deviceToken(roomId: string, deviceId?: string, name?: string): MintedToken & { deviceId: string };
  fetch(path: string, init?: RequestInit & { json?: unknown; token?: string }): Promise<Response>;
  close(): Promise<void>;
}

export interface StartOptions extends Partial<ServerConfigInput> {
  /** Reuse an existing directory (and database) instead of a new temp dir. */
  dir?: string;
  /** Keep the directory on close (for reopen tests). */
  keepDir?: boolean;
}

export async function makeTempDir(prefix = "devotchi-test-"): Promise<string> {
  return mkdtemp(join(tmpdir(), prefix));
}

export async function startTestServer(options: StartOptions = {}): Promise<TestServer> {
  const { dir: givenDir, keepDir, ...config } = options;
  const dir = givenDir ?? (await makeTempDir());
  const dbPath = config.dbPath ?? join(dir, "devotchi.db");
  const logs: string[] = [];
  const server = await createServer({
    host: "127.0.0.1",
    port: 0,
    logLevel: "debug",
    logSink: (line) => logs.push(line),
    ...config,
    dbPath,
  });
  const url = server.url;
  const wsUrl = url.replace(/^http/, "ws") + "/ws";

  const doFetch: TestServer["fetch"] = (path, init = {}) => {
    const { json, token, ...rest } = init;
    const headers = new Headers(rest.headers);
    if (token !== undefined) headers.set("Authorization", `Bearer ${token}`);
    if (json !== undefined) headers.set("Content-Type", "application/json");
    return fetch(url + path, { ...rest, headers, body: json !== undefined ? JSON.stringify(json) : rest.body });
  };

  return {
    server,
    url,
    wsUrl,
    dir,
    dbPath,
    logs,
    fetch: doFetch,
    async mintToken(label = "cli") {
      const res = await doFetch("/api/auth/token", { method: "POST", json: { label } });
      if (res.status !== 201) throw new Error(`mint failed: ${res.status} ${await res.text()}`);
      return (await res.json()) as MintedToken;
    },
    deviceToken(roomId, deviceId, name = "device") {
      const { db } = server.internals;
      return db.transaction(() => {
        const minted = insertToken(db, { roomId, kind: "device", label: "device" });
        const id = deviceId ?? allocateDeviceId(db, roomId);
        insertDevice(db, { roomId, id, tokenId: minted.id, name });
        return { ...minted, deviceId: id };
      });
    },
    async close() {
      await server.close();
      if (!keepDir && !givenDir) await rm(dir, { recursive: true, force: true });
    },
  };
}

export interface CloseInfo {
  code: number;
  reason: string;
}

type Predicate = string | ((msg: Json) => boolean);

function matches(msg: Json, predicate: Predicate): boolean {
  return typeof predicate === "string" ? msg.type === predicate : predicate(msg);
}

/** A WebSocket client that records every message for later awaiting. */
export class TestSocket {
  /** Messages not yet taken by awaitMessage, in arrival order. */
  readonly inbox: Json[] = [];
  /** Every message ever received. */
  readonly received: Json[] = [];
  readonly closed: Promise<CloseInfo>;
  private waiters: { predicate: Predicate; resolve: (msg: Json) => void }[] = [];

  private constructor(readonly ws: WebSocket) {
    this.closed = new Promise((resolve) => {
      ws.on("close", (code, reason) => resolve({ code, reason: reason.toString() }));
    });
    ws.on("message", (data: RawData, isBinary) => {
      if (isBinary) return;
      const msg = JSON.parse(data.toString()) as Json;
      this.received.push(msg);
      const index = this.waiters.findIndex((w) => matches(msg, w.predicate));
      if (index >= 0) {
        const [waiter] = this.waiters.splice(index, 1);
        waiter.resolve(msg);
      } else {
        this.inbox.push(msg);
      }
    });
    ws.on("error", () => undefined);
  }

  /** Opens a socket; rejects with the HTTP status when the upgrade is refused. */
  static connect(
    url: string,
    options: { token?: string; queryToken?: string; headers?: Record<string, string> } = {},
  ): Promise<TestSocket> {
    const target = options.queryToken === undefined ? url : `${url}?token=${encodeURIComponent(options.queryToken)}`;
    const headers: Record<string, string> = { ...options.headers };
    if (options.token !== undefined) headers.Authorization = `Bearer ${options.token}`;
    const ws = new WebSocket(target, { headers });
    const socket = new TestSocket(ws);
    return new Promise((resolve, reject) => {
      ws.once("open", () => resolve(socket));
      ws.once("unexpected-response", (_req, res) => {
        const chunks: Buffer[] = [];
        res.on("data", (chunk: Buffer) => chunks.push(chunk));
        res.on("end", () => reject(new UpgradeRejected(res.statusCode ?? 0, Buffer.concat(chunks).toString())));
      });
      ws.once("error", (err) => reject(err));
    });
  }

  send(msg: Json | string): void {
    this.ws.send(typeof msg === "string" ? msg : JSON.stringify(msg));
  }

  sendBinary(data: Buffer): void {
    this.ws.send(data, { binary: true });
  }

  /** Resolves with the first queued or future message matching the predicate. */
  awaitMessage(predicate: Predicate, timeoutMs = 2000): Promise<Json> {
    const index = this.inbox.findIndex((msg) => matches(msg, predicate));
    if (index >= 0) return Promise.resolve(this.inbox.splice(index, 1)[0]);
    return new Promise((resolve, reject) => {
      const waiter = {
        predicate,
        resolve: (msg: Json) => {
          clearTimeout(timer);
          resolve(msg);
        },
      };
      const timer = setTimeout(() => {
        this.waiters = this.waiters.filter((w) => w !== waiter);
        reject(new Error(`no message matching ${typeof predicate === "string" ? predicate : "predicate"} within ${timeoutMs} ms`));
      }, timeoutMs);
      this.waiters.push(waiter);
    });
  }

  /** Fails if a matching message arrives (or is queued) within the time. */
  async expectNoMessage(predicate: Predicate, ms = 150): Promise<void> {
    await delay(ms);
    const found = this.inbox.find((msg) => matches(msg, predicate));
    if (found) throw new Error(`unexpected message ${JSON.stringify(found)}`);
  }

  /** Sends register and waits for register_ack (or rejects with the error message). */
  async register(role: Role, client: Partial<RegisterClient> & { kind: ClientKind }, extra: Json = {}): Promise<Json> {
    this.send({ type: "register", role, proto: 2, client, ...extra });
    const reply = await this.awaitMessage((m) => m.type === "register_ack" || m.type === "error");
    if (reply.type === "error") throw new Error(`register rejected: ${String(reply.code)} ${String(reply.message)}`);
    return reply;
  }

  close(code?: number): void {
    this.ws.close(code);
  }

  get isOpen(): boolean {
    return this.ws.readyState === WebSocket.OPEN;
  }
}

export class UpgradeRejected extends Error {
  constructor(
    readonly status: number,
    readonly body: string,
  ) {
    super(`upgrade rejected with ${status}`);
  }
}

export function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

/** Connects and registers a producer (kind mcp). */
export async function producer(ts: TestServer, token: string, project?: string): Promise<TestSocket> {
  const socket = await TestSocket.connect(ts.wsUrl, { token });
  await socket.register("producer", { kind: "mcp", version: "0.0.0-test", ...(project ? { project } : {}) });
  return socket;
}

/** Connects and registers a consumer; cli tokens default to kind sim. */
export async function consumer(
  ts: TestServer,
  token: string,
  client: Partial<RegisterClient> & { kind: ClientKind } = { kind: "sim", deviceId: "sim-test" },
): Promise<TestSocket> {
  const socket = await TestSocket.connect(ts.wsUrl, { token });
  await socket.register("consumer", client);
  return socket;
}
