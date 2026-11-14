// WebSocket link from the browser to the dashboard proxy, with reconnect.
// Every handler is bound to the socket instance it was created for, so a late
// event from a replaced socket can never touch the current one.

export const BACKOFF_MIN_MS = 1000;
export const BACKOFF_MAX_MS = 30_000;
const JITTER_MS = 1000;
/** Protocol 3.4: a client treats 70 s without any server frame as a dead link. */
export const DEAD_LINK_MS = 70_000;
/** Protocol 3.6: after 4002 retry no faster than every 5 minutes. */
export const UPDATE_RETRY_MS = 5 * 60_000;
const CLOSE_UNSUPPORTED_PROTO = 4002;
const CLOSE_REVOKED = 4003;

export interface SocketLike {
  readonly readyState: number;
  onopen: ((ev: unknown) => void) | null;
  onmessage: ((ev: { data: unknown }) => void) | null;
  onclose: ((ev: { code: number; reason: string }) => void) | null;
  onerror: ((ev: unknown) => void) | null;
  send(data: string): void;
  close(code?: number, reason?: string): void;
}

export type LinkState =
  | { phase: "connecting" }
  | { phase: "reconnecting"; retryAt: number; reason: string }
  | { phase: "stopped"; reason: string };

export interface WsClientOptions {
  url(): string;
  onFrame(raw: string): void;
  onState(state: LinkState): void;
  create?: (url: string) => SocketLike;
  random?: () => number;
  now?: () => number;
  setTimer?: (fn: () => void, ms: number) => unknown;
  clearTimer?: (handle: unknown) => void;
}

/** Delay before reconnect attempt `attempt` (0-based): 1, 2, 4 ... 30 s plus up to 1 s of jitter, capped at 30 s. */
export function backoffDelay(attempt: number, random: number): number {
  const base = Math.min(BACKOFF_MAX_MS, BACKOFF_MIN_MS * 2 ** Math.min(attempt, 10));
  return Math.min(BACKOFF_MAX_MS, base + Math.floor(random * JITTER_MS));
}

const OPEN = 1;

export class WsClient {
  private ws: SocketLike | null = null;
  private attempt = 0;
  private retryTimer: unknown = null;
  private deadTimer: unknown = null;
  private stopped = false;
  private readonly create: (url: string) => SocketLike;
  private readonly random: () => number;
  private readonly now: () => number;
  private readonly setTimer: (fn: () => void, ms: number) => unknown;
  private readonly clearTimer: (handle: unknown) => void;

  constructor(private readonly opts: WsClientOptions) {
    this.create = opts.create ?? ((url) => new WebSocket(url) as unknown as SocketLike);
    this.random = opts.random ?? Math.random;
    this.now = opts.now ?? Date.now;
    this.setTimer = opts.setTimer ?? ((fn, ms) => setTimeout(fn, ms));
    this.clearTimer = opts.clearTimer ?? ((h) => clearTimeout(h as ReturnType<typeof setTimeout>));
  }

  get isOpen(): boolean {
    return this.ws !== null && this.ws.readyState === OPEN;
  }

  connect(): void {
    if (this.stopped) return;
    this.cancel("retryTimer");
    this.opts.onState({ phase: "connecting" });
    const ws = this.create(this.opts.url());
    this.ws = ws;
    ws.onopen = () => {
      if (this.ws === ws) this.watchdog(ws);
    };
    ws.onmessage = (ev) => {
      if (this.ws !== ws || typeof ev.data !== "string") return;
      this.attempt = 0;
      this.watchdog(ws);
      if (ev.data === '{"type":"ping"}') ws.send('{"type":"pong"}');
      this.opts.onFrame(ev.data);
    };
    ws.onerror = () => {
      // onclose follows and schedules the reconnect.
    };
    ws.onclose = (ev) => {
      if (this.ws !== ws) return;
      this.ws = null;
      this.cancel("deadTimer");
      this.closed(ev.code, ev.reason);
    };
  }

  /** Sends one frame; false when the socket is not open (nothing is queued). */
  send(text: string): boolean {
    if (!this.isOpen) return false;
    this.ws!.send(text);
    return true;
  }

  stop(): void {
    this.stopped = true;
    this.cancel("retryTimer");
    this.cancel("deadTimer");
    const ws = this.ws;
    this.ws = null;
    ws?.close(1000);
  }

  private closed(code: number, reason: string): void {
    if (code === CLOSE_REVOKED) {
      this.stopped = true;
      this.opts.onState({ phase: "stopped", reason: reason || "the server revoked the dashboard's token" });
      return;
    }
    const delay = code === CLOSE_UNSUPPORTED_PROTO ? UPDATE_RETRY_MS : backoffDelay(this.attempt, this.random());
    this.attempt += 1;
    const why = code === CLOSE_UNSUPPORTED_PROTO ? "server version mismatch: update required" : reason || "connection lost";
    this.opts.onState({ phase: "reconnecting", retryAt: this.now() + delay, reason: why });
    this.retryTimer = this.setTimer(() => {
      this.retryTimer = null;
      this.connect();
    }, delay);
  }

  private watchdog(ws: SocketLike): void {
    this.cancel("deadTimer");
    this.deadTimer = this.setTimer(() => {
      this.deadTimer = null;
      if (this.ws !== ws) return;
      this.ws = null;
      ws.close(4000, "no frame for 70 s");
      this.closed(1006, "no frame from the server for 70 s");
    }, DEAD_LINK_MS);
  }

  private cancel(which: "retryTimer" | "deadTimer"): void {
    if (this[which] !== null) this.clearTimer(this[which]);
    this[which] = null;
  }
}
