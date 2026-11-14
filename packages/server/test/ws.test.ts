import { afterEach, describe, expect, it } from "vitest";
import { LIMITS, utf8ByteLength } from "@devotchi/shared";
import {
  TestSocket,
  UpgradeRejected,
  consumer,
  delay,
  producer,
  startTestServer,
  type StartOptions,
  type TestServer,
} from "./helpers.js";

let ts: TestServer | undefined;
const sockets: TestSocket[] = [];

async function start(options: StartOptions = {}): Promise<TestServer> {
  ts = await startTestServer(options);
  return ts;
}

async function connect(...args: Parameters<typeof TestSocket.connect>): Promise<TestSocket> {
  const socket = await TestSocket.connect(...args);
  sockets.push(socket);
  return socket;
}

async function track(socket: Promise<TestSocket>): Promise<TestSocket> {
  const s = await socket;
  sockets.push(s);
  return s;
}

afterEach(async () => {
  for (const socket of sockets.splice(0)) socket.close();
  await ts?.close();
  ts = undefined;
});

describe("upgrade authentication", () => {
  it("rejects a missing or invalid token with a JSON 401", async () => {
    const t = await start();
    for (const options of [{}, { token: "not-a-token" }, { queryToken: "00000000-0000-4000-8000-000000000000" }]) {
      const err = await TestSocket.connect(t.wsUrl, options).catch((e: unknown) => e);
      expect(err).toBeInstanceOf(UpgradeRejected);
      expect((err as UpgradeRejected).status).toBe(401);
      expect(JSON.parse((err as UpgradeRejected).body)).toEqual({ error: "Invalid token", code: "unauthorized" });
    }
  });

  it("accepts the Bearer header and the ?token= query, on any path", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const viaHeader = await connect(t.wsUrl, { token });
    await viaHeader.register("producer", { kind: "mcp" });
    const viaQuery = await connect(t.wsUrl.replace(/\/ws$/, "/"), { queryToken: token });
    await viaQuery.register("consumer", { kind: "dashboard" });
  });

  it("rejects a duplicated ?token= with 400", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const err = await TestSocket.connect(`${t.wsUrl}?token=${token}&token=${token}`).catch((e: unknown) => e);
    expect((err as UpgradeRejected).status).toBe(400);
  });

  it("answers 503 when the socket limit is reached, counting unregistered sockets", async () => {
    const t = await start({ maxSockets: 2 });
    const { token } = await t.mintToken();
    await connect(t.wsUrl, { token });
    await connect(t.wsUrl, { token });
    const err = await TestSocket.connect(t.wsUrl, { token }).catch((e: unknown) => e);
    expect((err as UpgradeRejected).status).toBe(503);
  });
});

describe("upgrade failures", () => {
  it("answers 500 instead of crashing when the token lookup throws", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    t.server.internals.db.close();
    const err = await TestSocket.connect(t.wsUrl, { token }).catch((e: unknown) => e);
    expect(err).toBeInstanceOf(UpgradeRejected);
    expect((err as UpgradeRejected).status).toBe(500);
    expect(JSON.parse((err as UpgradeRejected).body)).toEqual({ error: "Internal server error", code: "internal" });
  });
});

describe("register", () => {
  it("closes with 4002 for a missing or wrong proto", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    for (const proto of [undefined, 1, 3]) {
      const s = await connect(t.wsUrl, { token });
      s.send({ type: "register", role: "producer", proto, client: { kind: "mcp" } });
      expect(await s.awaitMessage("error")).toMatchObject({ code: "UNSUPPORTED_PROTO" });
      expect((await s.closed).code).toBe(4002);
    }
  });

  it("closes with 1008 when register does not arrive in time", async () => {
    const t = await start({ timing: { registerDeadlineMs: 100 } });
    const { token } = await t.mintToken();
    const s = await connect(t.wsUrl, { token });
    expect(await s.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE" });
    expect((await s.closed).code).toBe(1008);
    await delay(20);
    expect(t.server.internals.rooms.sockets.size).toBe(0);
  });

  it("closes with 1008 when the first message is not register", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const s = await connect(t.wsUrl, { token });
    s.send({ type: "status", msg: "hi" });
    expect(await s.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE" });
    expect((await s.closed).code).toBe(1008);
  });

  it("enforces the token kind rules with NOT_ALLOWED and 1008", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const device = t.deviceToken(cli.roomId, "dv-a1b2c3");
    const cases: { token: string; role: string; client: Record<string, unknown> }[] = [
      { token: device.token, role: "producer", client: { kind: "mcp" } },
      { token: device.token, role: "consumer", client: { kind: "dashboard" } },
      { token: cli.token, role: "consumer", client: { kind: "device", deviceId: "dv-a1b2c3" } },
      { token: cli.token, role: "producer", client: { kind: "dashboard" } },
    ];
    for (const c of cases) {
      const s = await connect(t.wsUrl, { token: c.token });
      s.send({ type: "register", role: c.role, proto: 2, client: c.client });
      expect(await s.awaitMessage("error")).toMatchObject({ code: "NOT_ALLOWED" });
      expect((await s.closed).code).toBe(1008);
    }
  });

  it("rejects a missing or malformed deviceId with INVALID_MESSAGE and 1008", async () => {
    const t = await start();
    const cli = await t.mintToken();
    for (const deviceId of [undefined, "dv-XYZ", "sim-", "device-1", 42]) {
      const s = await connect(t.wsUrl, { token: cli.token });
      s.send({ type: "register", role: "consumer", proto: 2, client: { kind: "sim", deviceId } });
      expect(await s.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE" });
      expect((await s.closed).code).toBe(1008);
    }
  });

  it("answers ROOM_FULL beyond 8 producers or 8 consumers", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    for (let i = 0; i < 8; i++) await track(producer(t, token));
    for (let i = 0; i < 8; i++) await track(consumer(t, token, { kind: "sim", deviceId: `sim-${i}` }));
    for (const role of ["producer", "consumer"] as const) {
      const s = await connect(t.wsUrl, { token });
      s.send({ type: "register", role, proto: 2, client: role === "producer" ? { kind: "cli" } : { kind: "dashboard" } });
      expect(await s.awaitMessage("error")).toMatchObject({ code: "ROOM_FULL" });
      expect((await s.closed).code).toBe(1008);
    }
  });

  it("acknowledges with the protocol 3.3 shape", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const device = t.deviceToken(cli.roomId, "dv-e356e4", "desk");
    await track(producer(t, cli.token));
    const s = await connect(t.wsUrl, { token: device.token });
    const before = Date.now();
    const ack = await s.register("consumer", { kind: "device", deviceId: "dv-e356e4", model: "devotchi-v1", version: "0.2.0" });
    expect(ack).toEqual({
      type: "register_ack",
      role: "consumer",
      roomId: cli.roomId,
      proto: 2,
      serverVersion: expect.any(String),
      serverTime: expect.any(Number),
      mode: "ask",
      peers: { producers: 1, consumers: 1, devices: 1 },
      device: { id: "dv-e356e4", name: "desk" },
    });
    expect(ack.serverTime as number).toBeGreaterThanOrEqual(before);

    const p = await connect(t.wsUrl, { token: cli.token });
    const producerAck = await p.register("producer", { kind: "cli" });
    expect(producerAck).not.toHaveProperty("device");
    expect(producerAck.peers).toEqual({ producers: 2, consumers: 1, devices: 1 });
  });

  it("names a device socket by its registry row, not by client.deviceId", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const device = t.deviceToken(cli.roomId, "dv-111111", "desk");
    const s = await connect(t.wsUrl, { token: device.token });
    const ack = await s.register("consumer", { kind: "device", deviceId: "dv-222222" });
    expect(ack.device).toEqual({ id: "dv-111111", name: "desk" });
    const conn = [...t.server.internals.rooms.sockets][0];
    expect(conn.registration?.identity).toBe("dv-111111");
  });

  it("uses client.deviceId for cli-token sims and 'dashboard' for dashboards", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    await track(consumer(t, token, { kind: "sim", deviceId: "sim-bench" }));
    await track(consumer(t, token, { kind: "dashboard" }));
    const identities = [...t.server.internals.rooms.sockets].map((c) => c.registration?.identity).sort();
    expect(identities).toEqual(["dashboard", "sim-bench"]);
  });

  it("ignores a second register with INVALID_MESSAGE and keeps the socket", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const s = await track(producer(t, token));
    s.send({ type: "register", role: "producer", proto: 2, client: { kind: "mcp" } });
    expect(await s.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE" });
    s.send({ type: "ping" });
    await s.awaitMessage("pong");
    expect(s.isOpen).toBe(true);
  });

  it("lets two producers coexist and both stay connected", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const a = await track(producer(t, token, "alpha"));
    const b = await track(producer(t, token, "beta"));
    const c = await track(consumer(t, token));
    a.send({ type: "status", msg: "from a" });
    b.send({ type: "status", msg: "from b" });
    expect(await c.awaitMessage((m) => m.msg === "from a")).toMatchObject({ project: "alpha" });
    expect(await c.awaitMessage((m) => m.msg === "from b")).toMatchObject({ project: "beta" });
    await delay(50);
    expect(a.isOpen && b.isOpen).toBe(true);
  });
});

describe("peers", () => {
  it("tells everyone else in the room on every join and leave", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const p = await track(producer(t, token));
    const c = await track(consumer(t, token, { kind: "sim", deviceId: "sim-a" }));
    expect(await p.awaitMessage("peers")).toEqual({ type: "peers", producers: 1, consumers: 1, devices: 1 });
    const d = await track(consumer(t, token, { kind: "dashboard" }));
    expect(await p.awaitMessage("peers")).toEqual({ type: "peers", producers: 1, consumers: 2, devices: 1 });
    expect(await c.awaitMessage("peers")).toEqual({ type: "peers", producers: 1, consumers: 2, devices: 1 });
    d.close();
    expect(await p.awaitMessage("peers")).toEqual({ type: "peers", producers: 1, consumers: 1, devices: 1 });
    expect(await c.awaitMessage("peers")).toEqual({ type: "peers", producers: 1, consumers: 1, devices: 1 });
  });

  it("does not leak peers between rooms", async () => {
    const t = await start();
    const a = await t.mintToken();
    const b = await t.mintToken();
    const p = await track(producer(t, a.token));
    await track(consumer(t, b.token));
    await p.expectNoMessage("peers");
  });
});

describe("content routing", () => {
  it("forwards with a server timestamp, the producer project and truncation", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const p = await track(producer(t, token, "devotchi"));
    const c = await track(consumer(t, token));
    const before = Date.now();
    p.send({ type: "status", msg: "x".repeat(500), timestamp: "1999-01-01T00:00:00.000Z", extra: 1 });
    const status = await c.awaitMessage("status");
    expect(status.msg).toBe("x".repeat(LIMITS.msg));
    expect(status.project).toBe("devotchi");
    expect(status).not.toHaveProperty("extra");
    expect(status).not.toHaveProperty("mood");
    expect(Date.parse(status.timestamp as string)).toBeGreaterThanOrEqual(before - 5);

    p.send({ type: "notify", event: "tests_failed", mood: "sad", project: "other" });
    expect(await c.awaitMessage("notify")).toMatchObject({ event: "tests_failed", mood: "sad", project: "other" });
    p.send({ type: "mood", mood: "anxious", reason: "Long build" });
    const mood = await c.awaitMessage("mood");
    expect(mood).toMatchObject({ mood: "anxious", reason: "Long build" });
    expect(typeof mood.timestamp).toBe("string");
  });

  it("drops 800 CJK characters with MESSAGE_TOO_LARGE and keeps the socket", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const p = await track(producer(t, token));
    const c = await track(consumer(t, token));
    p.send({ type: "status", msg: "漢".repeat(800) });
    expect(await p.awaitMessage("error")).toMatchObject({ code: "MESSAGE_TOO_LARGE" });
    p.send({ type: "status", msg: "漢".repeat(300) });
    const status = await c.awaitMessage("status");
    expect(utf8ByteLength(status.msg as string)).toBeLessThanOrEqual(LIMITS.msg);
    expect(p.isOpen).toBe(true);
  });

  it("answers frames of 2049..8192 bytes with MESSAGE_TOO_LARGE and closes above 8192 with 1009", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const p = await track(producer(t, token));
    p.send({ type: "status", msg: "a".repeat(4000) });
    expect(await p.awaitMessage("error")).toMatchObject({ code: "MESSAGE_TOO_LARGE" });
    p.send({ type: "ping" });
    await p.awaitMessage("pong");
    p.send({ type: "status", msg: "a".repeat(9000) });
    expect((await p.closed).code).toBe(1009);
  });

  it("answers binary frames with INVALID_MESSAGE", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const p = await track(producer(t, token));
    p.sendBinary(Buffer.from(JSON.stringify({ type: "status", msg: "hi" })));
    expect(await p.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE" });
    expect(p.isOpen).toBe(true);
  });

  it("rejects forbidden and unknown types with NOT_ALLOWED and forwards nothing", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const p = await track(producer(t, token));
    const c = await track(consumer(t, token));
    const forbidden = [
      { type: "register_ack", role: "producer", roomId: "x" },
      { type: "permission", id: "a".repeat(32), tool: "Bash", summary: "x", risk: "low", timeout: 1000 },
      { type: "dismiss", id: "a".repeat(32), reason: "cancelled" },
      { type: "holding", ids: ["a".repeat(32)] },
      { type: "approval" },
    ];
    for (const msg of forbidden) {
      p.send(msg);
      expect(await p.awaitMessage("error")).toMatchObject({ code: "NOT_ALLOWED" });
    }
    c.send({ type: "status", msg: "consumer status" });
    expect(await c.awaitMessage("error")).toMatchObject({ code: "NOT_ALLOWED" });
    await c.expectNoMessage((m) => ["permission", "dismiss", "register_ack"].includes(m.type as string));
    await p.expectNoMessage("status");
  });

  it("answers consumer system messages without a registered handler with NOT_ALLOWED", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const c = await track(consumer(t, token));
    c.send({
      type: "telemetry",
      battery: { state: "absent", pct: null, mv: 4100 },
      power: "usb",
      rssi: -50,
      uptimeS: 1,
      heapFree: 1,
      heapMin: 1,
      resetReason: "poweron",
      fw: "0.0.0",
    });
    expect(await c.awaitMessage("error")).toMatchObject({ code: "NOT_ALLOWED" });
  });

  it("validates content with INVALID_MESSAGE", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const p = await track(producer(t, token));
    for (const msg of [{ type: "status" }, { type: "mood", mood: "grumpy" }, { type: "notify", event: "Bad Event", mood: "happy" }, "not json"]) {
      p.send(msg);
      expect(await p.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE" });
    }
  });

  it("answers ping with pong", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const c = await track(consumer(t, token));
    c.send({ type: "ping" });
    expect(await c.awaitMessage("pong")).toEqual({ type: "pong" });
  });

  it("limits producer content to 60 messages per window", async () => {
    const t = await start({ timing: { rateWindowMs: 500 } });
    const { token } = await t.mintToken();
    const p = await track(producer(t, token));
    const c = await track(consumer(t, token));
    for (let i = 0; i < 61; i++) p.send({ type: "status", msg: `m${i}` });
    expect(await p.awaitMessage("error")).toMatchObject({ code: "RATE_LIMITED" });
    await c.awaitMessage((m) => m.msg === "m59");
    await delay(550);
    p.send({ type: "status", msg: "after" });
    await c.awaitMessage((m) => m.msg === "after");
  });
});

describe("replay and room mood", () => {
  it("replays the last status without its mood, then the room mood", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const p = await track(producer(t, token));
    p.send({ type: "mood", mood: "happy" });
    p.send({ type: "status", msg: "Edit src/app.c" });
    p.send({ type: "ping" });
    await p.awaitMessage("pong");

    const s = await connect(t.wsUrl, { token });
    s.send({ type: "register", role: "consumer", proto: 2, client: { kind: "dashboard" } });
    await s.awaitMessage("register_ack");
    await delay(50);
    const [first, second, ...rest] = s.received.slice(1);
    expect(first).toMatchObject({ type: "status", msg: "Edit src/app.c" });
    expect(first).not.toHaveProperty("mood");
    expect(second).toMatchObject({ type: "mood", mood: "happy" });
    expect(rest).toEqual([]);
  });

  it("takes the room mood from status and notify too, and strips the replayed status mood", async () => {
    const t = await start();
    const { token, roomId } = await t.mintToken();
    const p = await track(producer(t, token));
    p.send({ type: "status", msg: "working on it", mood: "focused" });
    p.send({ type: "notify", event: "tests_failed", mood: "sad" });
    p.send({ type: "ping" });
    await p.awaitMessage("pong");
    const c = await track(consumer(t, token));
    expect(await c.awaitMessage("status")).not.toHaveProperty("mood");
    expect(await c.awaitMessage("mood")).toMatchObject({ mood: "sad" });
    expect(t.server.internals.rooms.get(roomId)?.mood?.value).toBe("sad");
  });

  it("does not replay state older than the replay age", async () => {
    const t = await start({ timing: { replayMaxAgeMs: 50 } });
    const { token } = await t.mintToken();
    const p = await track(producer(t, token));
    p.send({ type: "status", msg: "old", mood: "happy" });
    p.send({ type: "ping" });
    await p.awaitMessage("pong");
    await delay(80);
    const c = await track(consumer(t, token));
    await c.expectNoMessage((m) => m.type === "status" || m.type === "mood");
  });

  it("runs registered consumer hooks after the replay", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const seen: string[] = [];
    t.server.internals.ws.onConsumerRegistered((conn) => seen.push(conn.registration?.identity ?? "?"));
    await track(consumer(t, token, { kind: "sim", deviceId: "sim-hook" }));
    await track(producer(t, token));
    expect(seen).toEqual(["sim-hook"]);
  });
});

describe("holding bucket", () => {
  it("accepts a burst of 8, limits the 9th and refills at 2 per second", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const handled: unknown[] = [];
    t.server.internals.ws.handle("holding", (_conn, msg) => handled.push(msg.ids));
    const c = await track(consumer(t, token));
    const id = "0123456789abcdef0123456789abcdef";
    for (let i = 0; i < 9; i++) c.send({ type: "holding", ids: [id] });
    expect(await c.awaitMessage("error")).toMatchObject({ code: "RATE_LIMITED" });
    await delay(30);
    expect(handled).toHaveLength(8);
    await delay(1000);
    c.send({ type: "holding", ids: [id] });
    c.send({ type: "holding", ids: [id] });
    await delay(50);
    expect(handled).toHaveLength(10);
    expect(c.inbox.filter((m) => m.type === "error")).toHaveLength(0);
  });

  it("limits telemetry and set_mode to one per interval", async () => {
    const t = await start({ timing: { systemMessageIntervalMs: 200 } });
    const { token } = await t.mintToken();
    const modes: string[] = [];
    t.server.internals.ws.handle("set_mode", (_conn, msg) => modes.push(msg.mode));
    const c = await track(consumer(t, token));
    c.send({ type: "set_mode", mode: "desk" });
    c.send({ type: "set_mode", mode: "ask" });
    expect(await c.awaitMessage("error")).toMatchObject({ code: "RATE_LIMITED" });
    await delay(250);
    c.send({ type: "set_mode", mode: "ask" });
    await delay(50);
    expect(modes).toEqual(["desk", "ask"]);
  });
});

describe("heartbeat", () => {
  it("pings registered sockets and terminates silent ones", async () => {
    const t = await start({ timing: { heartbeatIntervalMs: 40, heartbeatDeadMs: 200 } });
    const { token } = await t.mintToken();
    const silent = await track(producer(t, token));
    const lively = await track(consumer(t, token));
    lively.ws.on("message", (data) => {
      if (JSON.parse(data.toString()).type === "ping") lively.send({ type: "pong" });
    });
    await silent.awaitMessage("ping");
    const closed = await silent.closed;
    expect(closed.code).toBe(1006);
    await delay(150);
    expect(lively.isOpen).toBe(true);
  });
});

describe("shutdown", () => {
  it("closes sockets with 1001 and finishes quickly", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const p = await track(producer(t, token));
    const c = await track(consumer(t, token));
    const started = Date.now();
    await t.close();
    ts = undefined;
    expect(Date.now() - started).toBeLessThan(1000);
    expect((await p.closed).code).toBe(1001);
    expect((await c.closed).code).toBe(1001);
  });

  it("finishes in well under a second when idle", async () => {
    const t = await start();
    await t.fetch("/health");
    const started = Date.now();
    await t.close();
    ts = undefined;
    expect(Date.now() - started).toBeLessThan(300);
  });
});
