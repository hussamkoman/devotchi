// Producer WebSocket (protocol 3): one instance per process that emits pet
// state. Reconnects on its own; stops only for revocation (4003), a protocol
// mismatch (4002) or credentials that cannot be fixed by retrying.

import { EventEmitter } from "node:events";
import { basename } from "node:path";
import WebSocket, { type RawData } from "ws";
import {
  CLOSE_CODES,
  HARD_FRAME_BYTES,
  HEARTBEAT_DEAD_MS,
  PROTO_VERSION,
  REPLAY_MAX_AGE_MS,
  buildMood,
  buildPong,
  buildRegister,
  buildStatus,
  serialize,
  type ErrorMessage,
  type Mood,
  type MoodMessage,
  type NotifyMessage,
  type Peers,
  type ProducerKind,
  type RegisterAckMessage,
  type StatusMessage,
} from "@devotchi/shared";
import { serverWsUrl } from "./config.js";
import type { CredentialSource } from "./credentials.js";
import { CliError, errorMessage } from "./errors.js";

export type ContentMessage = StatusMessage | NotifyMessage | MoodMessage;

export type LinkState = "idle" | "connecting" | "registered" | "retrying" | "stopped";

export interface SendResult {
  /** Written to an open, registered socket. */
  sent: boolean;
  consumers: number;
  devices: number;
}

export interface ProducerOptions {
  serverUrl: string;
  credentials: CredentialSource;
  version: string;
  kind?: ProducerKind;
  /** Defaults to basename(cwd). */
  project?: string;
  log?: (line: string) => void;
  minBackoffMs?: number;
  maxBackoffMs?: number;
  /** No server frame for this long means the link is dead (protocol 3.4). */
  deadLinkMs?: number;
  handshakeTimeoutMs?: number;
}

export interface ProducerEvents {
  state: [LinkState];
  registered: [RegisterAckMessage];
  peers: [Peers];
  serverError: [ErrorMessage];
  stopped: [string];
}

const MIN_BACKOFF_MS = 1_000;
const MAX_BACKOFF_MS = 30_000;
const CLOSE_WAIT_MS = 1_000;

interface Remembered<T> {
  value: T;
  at: number;
}

export class ProducerConnection extends EventEmitter<ProducerEvents> {
  readonly serverUrl: string;
  private readonly opts: Required<Omit<ProducerOptions, "credentials" | "serverUrl">>;
  private readonly credentials: CredentialSource;
  private ws: WebSocket | null = null;
  private registered = false;
  private currentState: LinkState = "idle";
  private lastPeers: Peers | null = null;
  private error: string | null = null;
  private attempt = 0;
  private renewedSinceRegister = false;
  private retryTimer: NodeJS.Timeout | null = null;
  private deadTimer: NodeJS.Timeout | null = null;
  private closing = false;
  private lastStatus: Remembered<StatusMessage> | null = null;
  private lastMood: Remembered<{ mood: Mood; reason?: string }> | null = null;

  constructor(options: ProducerOptions) {
    super();
    this.serverUrl = options.serverUrl;
    this.credentials = options.credentials;
    this.opts = {
      version: options.version,
      kind: options.kind ?? "mcp",
      project: options.project ?? basename(process.cwd()),
      log: options.log ?? (() => undefined),
      minBackoffMs: options.minBackoffMs ?? MIN_BACKOFF_MS,
      maxBackoffMs: options.maxBackoffMs ?? MAX_BACKOFF_MS,
      deadLinkMs: options.deadLinkMs ?? HEARTBEAT_DEAD_MS,
      handshakeTimeoutMs: options.handshakeTimeoutMs ?? 10_000,
    };
  }

  get state(): LinkState {
    return this.currentState;
  }

  /** Counts from the last `register_ack` or `peers`; null before the first registration. */
  get peers(): Peers | null {
    return this.lastPeers;
  }

  /** Why the link is not registered right now (or why it stopped), for the user. */
  get lastError(): string | null {
    return this.error;
  }

  start(): void {
    if (this.currentState !== "idle") return;
    this.closing = false;
    void this.connect();
  }

  /**
   * Sends a content message if the socket is registered. Status and mood are
   * remembered either way and sent again after the next registration, so the
   * pet shows the latest state once the link returns.
   */
  send(msg: ContentMessage): SendResult {
    this.remember(msg);
    if (!this.registered || this.ws === null || this.ws.readyState !== WebSocket.OPEN) {
      return { sent: false, consumers: 0, devices: 0 };
    }
    if (!this.write(msg)) return { sent: false, consumers: 0, devices: 0 };
    return { sent: true, consumers: this.lastPeers?.consumers ?? 0, devices: this.lastPeers?.devices ?? 0 };
  }

  async close(): Promise<void> {
    this.closing = true;
    this.clearTimers();
    const ws = this.ws;
    this.ws = null;
    this.registered = false;
    this.setState("stopped");
    if (ws === null || ws.readyState === WebSocket.CLOSED) return;
    await new Promise<void>((resolve) => {
      const timer = setTimeout(() => {
        ws.terminate();
        resolve();
      }, CLOSE_WAIT_MS);
      ws.once("close", () => {
        clearTimeout(timer);
        resolve();
      });
      if (ws.readyState === WebSocket.CONNECTING) ws.terminate();
      else ws.close(CLOSE_CODES.NORMAL);
    });
  }

  private remember(msg: ContentMessage): void {
    const at = Date.now();
    if (msg.type === "status") {
      this.lastStatus = { value: msg, at };
      if (msg.mood !== undefined) this.lastMood = { value: { mood: msg.mood }, at };
    } else if (msg.type === "mood") {
      this.lastMood = { value: { mood: msg.mood, reason: msg.reason }, at };
    } else {
      this.lastMood = { value: { mood: msg.mood }, at };
    }
  }

  private write(msg: ContentMessage | ReturnType<typeof buildRegister> | ReturnType<typeof buildPong>): boolean {
    if (this.ws === null || this.ws.readyState !== WebSocket.OPEN) return false;
    try {
      this.ws.send(serialize(msg));
      return true;
    } catch (err) {
      this.opts.log(`not sent: ${errorMessage(err)}`);
      return false;
    }
  }

  private setState(state: LinkState): void {
    if (this.currentState === state) return;
    this.currentState = state;
    this.emit("state", state);
  }

  private clearTimers(): void {
    if (this.retryTimer !== null) clearTimeout(this.retryTimer);
    if (this.deadTimer !== null) clearTimeout(this.deadTimer);
    this.retryTimer = null;
    this.deadTimer = null;
  }

  private stop(reason: string): void {
    this.error = reason;
    this.opts.log(`link stopped: ${reason}`);
    this.clearTimers();
    this.registered = false;
    const ws = this.ws;
    this.ws = null;
    if (ws !== null && ws.readyState !== WebSocket.CLOSED) ws.terminate();
    this.setState("stopped");
    this.emit("stopped", reason);
  }

  private scheduleRetry(reason: string, immediate = false): void {
    if (this.closing || this.currentState === "stopped") return;
    this.error = reason;
    this.registered = false;
    this.setState("retrying");
    const base = Math.min(this.opts.maxBackoffMs, this.opts.minBackoffMs * 2 ** this.attempt);
    const delay = immediate ? 0 : Math.round(base / 2 + (Math.random() * base) / 2);
    this.attempt += 1;
    this.opts.log(`${reason}; retrying in ${(delay / 1000).toFixed(1)} s`);
    this.retryTimer = setTimeout(() => {
      this.retryTimer = null;
      void this.connect();
    }, delay);
  }

  private async connect(): Promise<void> {
    if (this.closing) return;
    this.setState("connecting");
    let token: string;
    try {
      token = (await this.credentials.get()).token;
    } catch (err) {
      this.onCredentialError(err);
      return;
    }
    if (this.closing) return;
    this.open(token);
  }

  private onCredentialError(err: unknown): void {
    if (this.closing) return;
    if (err instanceof CliError && !err.retryable) this.stop(err.message);
    else this.scheduleRetry(errorMessage(err));
  }

  private open(token: string): void {
    const ws = new WebSocket(serverWsUrl(this.serverUrl), {
      headers: { Authorization: `Bearer ${token}` },
      maxPayload: HARD_FRAME_BYTES,
      handshakeTimeout: this.opts.handshakeTimeoutMs,
    });
    this.ws = ws;
    let httpStatus: number | null = null;
    let socketError: string | null = null;

    ws.on("unexpected-response", (_req, res) => {
      httpStatus = res.statusCode ?? 0;
      res.resume();
      ws.terminate();
    });

    ws.on("open", () => {
      this.armDeadTimer();
      this.write(
        buildRegister({
          role: "producer",
          client: { kind: this.opts.kind, version: this.opts.version, project: this.opts.project },
        }),
      );
    });

    ws.on("message", (data: RawData, isBinary: boolean) => {
      if (this.ws !== ws) return;
      this.armDeadTimer();
      if (isBinary) return;
      this.onFrame(data.toString());
    });

    ws.on("error", (err) => {
      socketError = err.message;
    });

    ws.on("close", (code: number) => {
      if (this.ws !== ws) return;
      this.ws = null;
      this.registered = false;
      if (this.deadTimer !== null) clearTimeout(this.deadTimer);
      this.deadTimer = null;
      if (this.closing || this.currentState === "stopped") return;
      if (httpStatus === 401) {
        this.onUnauthorized(token);
        return;
      }
      if (code === CLOSE_CODES.REVOKED) {
        this.stop("token revoked by the server (close 4003); run `devotchi setup` or `devotchi login` again");
        return;
      }
      if (code === CLOSE_CODES.UNSUPPORTED_PROTO) {
        this.stop(`server version mismatch (close 4002): this client speaks protocol ${PROTO_VERSION}`);
        return;
      }
      const reason =
        httpStatus !== null
          ? `server answered the WebSocket upgrade with HTTP ${httpStatus}`
          : socketError !== null && code === 1006
            ? `cannot connect to ${this.serverUrl} (${socketError})`
            : `disconnected (close ${code})`;
      this.scheduleRetry(reason);
    });
  }

  private onUnauthorized(token: string): void {
    if (this.renewedSinceRegister) {
      this.scheduleRetry("the server rejected the token again (HTTP 401)");
      return;
    }
    this.renewedSinceRegister = true;
    this.setState("connecting");
    this.credentials.renew(token).then(
      () => {
        if (!this.closing && this.currentState !== "stopped") this.scheduleRetry("token renewed", true);
      },
      (err: unknown) => this.onCredentialError(err),
    );
  }

  private armDeadTimer(): void {
    if (this.deadTimer !== null) clearTimeout(this.deadTimer);
    const ws = this.ws;
    this.deadTimer = setTimeout(() => {
      this.deadTimer = null;
      if (ws !== null && this.ws === ws) {
        this.opts.log(`no frame from the server for ${this.opts.deadLinkMs / 1000} s; reconnecting`);
        ws.terminate();
      }
    }, this.opts.deadLinkMs);
  }

  private onFrame(text: string): void {
    let msg: { type?: unknown } & Record<string, unknown>;
    try {
      msg = JSON.parse(text) as typeof msg;
    } catch {
      return;
    }
    if (typeof msg !== "object" || msg === null) return;
    switch (msg.type) {
      case "ping":
        this.write(buildPong());
        break;
      case "register_ack":
        this.onRegistered(msg as unknown as RegisterAckMessage);
        break;
      case "peers":
        this.setPeers(msg as unknown as Peers);
        break;
      case "error": {
        const error = msg as unknown as ErrorMessage;
        this.opts.log(`server error ${String(error.code)}: ${String(error.message)}`);
        this.emit("serverError", error);
        break;
      }
      default:
        break;
    }
  }

  private setPeers(peers: Peers): void {
    this.lastPeers = {
      producers: Number(peers.producers) || 0,
      consumers: Number(peers.consumers) || 0,
      devices: Number(peers.devices) || 0,
    };
    this.emit("peers", this.lastPeers);
  }

  private onRegistered(ack: RegisterAckMessage): void {
    if (ack.proto !== PROTO_VERSION) {
      this.stop(`server version mismatch: it speaks protocol ${String(ack.proto)}, this client ${PROTO_VERSION}`);
      return;
    }
    this.registered = true;
    this.attempt = 0;
    this.renewedSinceRegister = false;
    this.error = null;
    if (ack.peers !== undefined) this.setPeers(ack.peers);
    this.setState("registered");
    this.opts.log(`registered with ${this.serverUrl} as producer (${this.lastPeers?.consumers ?? 0} consumer(s))`);
    this.resend();
    this.emit("registered", ack);
  }

  /** Status first (without its mood), then the mood: the pet ends in the latest mood either way. */
  private resend(): void {
    const now = Date.now();
    if (this.lastStatus !== null && now - this.lastStatus.at < REPLAY_MAX_AGE_MS) {
      const { mood: _mood, ...status } = this.lastStatus.value;
      this.write(buildStatus(status));
    }
    if (this.lastMood !== null && now - this.lastMood.at < REPLAY_MAX_AGE_MS) {
      this.write(buildMood(this.lastMood.value));
    }
  }
}
