import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { BACKOFF_MAX_MS, DEAD_LINK_MS, UPDATE_RETRY_MS, WsClient, backoffDelay, type LinkState, type SocketLike } from "../../web/src/ws-client.js";

class FakeSocket implements SocketLike {
  readyState = 0;
  sent: string[] = [];
  closedWith: number | undefined;
  onopen: SocketLike["onopen"] = null;
  onmessage: SocketLike["onmessage"] = null;
  onclose: SocketLike["onclose"] = null;
  onerror: SocketLike["onerror"] = null;
  constructor(readonly url: string) {}
  send(data: string) {
    this.sent.push(data);
  }
  close(code?: number) {
    this.closedWith = code;
    this.readyState = 3;
  }
  open() {
    this.readyState = 1;
    this.onopen?.({});
  }
  frame(data: string) {
    this.onmessage?.({ data });
  }
  drop(code = 1006, reason = "") {
    this.readyState = 3;
    this.onclose?.({ code, reason });
  }
}

describe("ws-client", () => {
  let sockets: FakeSocket[];
  let states: LinkState[];
  let frames: string[];
  let client: WsClient;

  beforeEach(() => {
    vi.useFakeTimers();
    sockets = [];
    states = [];
    frames = [];
    client = new WsClient({
      url: () => "ws://localhost:1/ws?s=x",
      onFrame: (raw) => frames.push(raw),
      onState: (s) => states.push(s),
      create: (url) => {
        const s = new FakeSocket(url);
        sockets.push(s);
        return s;
      },
      random: () => 0.5,
    });
  });
  afterEach(() => {
    client.stop();
    vi.useRealTimers();
  });

  it("backoff grows from 1 s to 30 s with jitter", () => {
    expect(backoffDelay(0, 0)).toBe(1000);
    expect(backoffDelay(0, 0.999)).toBe(1999);
    expect(backoffDelay(1, 0)).toBe(2000);
    expect(backoffDelay(4, 0)).toBe(16_000);
    expect(backoffDelay(5, 0.9)).toBe(BACKOFF_MAX_MS);
    expect(backoffDelay(50, 0.5)).toBe(BACKOFF_MAX_MS);
  });

  it("reconnects with growing delays and resets after a frame", () => {
    client.connect();
    sockets[0].drop();
    expect(states.at(-1)).toMatchObject({ phase: "reconnecting", reason: "connection lost", retryAt: Date.now() + 1500 });
    vi.advanceTimersByTime(1499);
    expect(sockets).toHaveLength(1);
    vi.advanceTimersByTime(1);
    expect(sockets).toHaveLength(2);
    sockets[1].drop();
    vi.advanceTimersByTime(2499);
    expect(sockets).toHaveLength(2);
    vi.advanceTimersByTime(1);
    expect(sockets).toHaveLength(3);
    sockets[2].open();
    sockets[2].frame('{"type":"register_ack"}');
    sockets[2].drop(1001, "server shutting down");
    expect(states.at(-1)).toMatchObject({ phase: "reconnecting", reason: "server shutting down", retryAt: Date.now() + 1500 });
  });

  it("ignores events from a replaced socket", () => {
    client.connect();
    const old = sockets[0];
    old.drop();
    vi.advanceTimersByTime(1500);
    old.frame('{"type":"status","msg":"stale"}');
    old.drop();
    expect(frames).toEqual([]);
    expect(sockets).toHaveLength(2);
    vi.advanceTimersByTime(60_000);
    expect(sockets).toHaveLength(2);
  });

  it("answers ping, sends only while open and stops on 4003", () => {
    client.connect();
    expect(client.send("x")).toBe(false);
    sockets[0].open();
    sockets[0].frame('{"type":"ping"}');
    expect(sockets[0].sent).toEqual(['{"type":"pong"}']);
    expect(client.send("y")).toBe(true);
    sockets[0].drop(4003, "token revoked");
    expect(states.at(-1)).toEqual({ phase: "stopped", reason: "token revoked" });
    vi.advanceTimersByTime(10 * 60_000);
    expect(sockets).toHaveLength(1);
  });

  it("waits 5 minutes after 4002 and treats 70 s of silence as a dead link", () => {
    client.connect();
    sockets[0].drop(4002, "");
    expect(states.at(-1)).toMatchObject({ phase: "reconnecting", retryAt: Date.now() + UPDATE_RETRY_MS });
    vi.advanceTimersByTime(UPDATE_RETRY_MS);
    sockets[1].open();
    vi.advanceTimersByTime(DEAD_LINK_MS - 1);
    expect(sockets[1].closedWith).toBeUndefined();
    vi.advanceTimersByTime(1);
    expect(sockets[1].closedWith).toBe(4000);
    expect(states.at(-1)).toMatchObject({ phase: "reconnecting" });
  });
});
