// Room mode and settings (protocol 6, 4.11, 7.6) and POST /api/notify (7.7).

import { rm } from "node:fs/promises";
import { afterEach, describe, expect, it } from "vitest";
import { TestSocket, consumer, delay, makeTempDir, producer, startTestServer, type StartOptions, type TestServer } from "./helpers.js";
import { hold, hook, permissionInput, requestPermission, roomState } from "./hook-helpers.js";

let ts: TestServer | undefined;
const sockets: TestSocket[] = [];

async function start(options: StartOptions = {}): Promise<TestServer> {
  ts = await startTestServer(options);
  return ts;
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

describe("room mode", () => {
  it("set_mode persists, is broadcast to every consumer and survives a restart", async () => {
    const dir = await makeTempDir();
    try {
      let t = await start({ dir });
      const { token } = await t.mintToken();
      const a = await track(TestSocket.connect(t.wsUrl, { token }));
      expect(await a.register("consumer", { kind: "sim", deviceId: "sim-a" })).toMatchObject({ mode: "ask" });
      const b = await track(consumer(t, token, { kind: "dashboard" }));
      const p = await track(producer(t, token));

      a.send({ type: "set_mode", mode: "desk" });
      expect(await a.awaitMessage("mode")).toEqual({ type: "mode", mode: "desk" });
      expect(await b.awaitMessage("mode")).toEqual({ type: "mode", mode: "desk" });
      await p.expectNoMessage("mode", 80);
      expect((await roomState(t, token)).settings).toEqual({ mode: "desk", holdMs: 30000, shareText: false });
      expect(await hook(t, token, permissionInput())).toEqual({});
      expect((await roomState(t, token)).lastRequest).toMatchObject({ detail: "desk" });

      for (const socket of sockets.splice(0)) socket.close();
      await t.close();
      t = await start({ dir });
      const again = await track(TestSocket.connect(t.wsUrl, { token }));
      expect(await again.register("consumer", { kind: "sim", deviceId: "sim-a" })).toMatchObject({ mode: "desk" });
      expect((await roomState(t, token)).settings.mode).toBe("desk");

      again.send({ type: "set_mode", mode: "ask" });
      expect(await again.awaitMessage("mode")).toEqual({ type: "mode", mode: "ask" });
      const held = await requestPermission(t, token, again);
      again.send({ type: "permission_defer", id: held.id });
      expect(await held.reply).toEqual({});
    } finally {
      await ts?.close();
      ts = undefined;
      await rm(dir, { recursive: true, force: true });
    }
  });

  it("rejects an invalid set_mode and a producer's set_mode", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const c = await track(consumer(t, token));
    const p = await track(producer(t, token));
    c.send({ type: "set_mode", mode: "auto" });
    expect(await c.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE" });
    p.send({ type: "set_mode", mode: "desk" });
    expect(await p.awaitMessage("error")).toMatchObject({ code: "NOT_ALLOWED" });
    expect((await roomState(t, token)).settings.mode).toBe("ask");
  });

  it("keeps rooms apart", async () => {
    const t = await start();
    const a = await t.mintToken("a");
    const b = await t.mintToken("b");
    const ca = await track(consumer(t, a.token));
    const cb = await track(consumer(t, b.token));
    ca.send({ type: "set_mode", mode: "desk" });
    await ca.awaitMessage("mode");
    await cb.expectNoMessage("mode", 80);
    expect((await roomState(t, b.token)).settings.mode).toBe("ask");
  });
});

describe("PUT /api/room/settings", () => {
  it("applies any subset, validates, and broadcasts a mode change", async () => {
    const t = await start({ timing: { holdMs: 20000 } });
    const { token } = await t.mintToken();
    const c = await track(consumer(t, token));
    const put = (json: unknown) => t.fetch("/api/room/settings", { method: "PUT", json, token });

    let res = await put({});
    expect([res.status, await res.json()]).toEqual([200, { mode: "ask", holdMs: 20000, shareText: false }]);
    res = await put({ shareText: true });
    expect(await res.json()).toEqual({ mode: "ask", holdMs: 20000, shareText: true });
    await c.expectNoMessage("mode", 60);
    res = await put({ mode: "desk", holdMs: 10000 });
    expect(await res.json()).toEqual({ mode: "desk", holdMs: 10000, shareText: true });
    expect(await c.awaitMessage("mode")).toEqual({ type: "mode", mode: "desk" });
    res = await put({ mode: "desk", holdMs: 300000, somethingElse: 1 });
    expect(await res.json()).toEqual({ mode: "desk", holdMs: 300000, shareText: true });
    await c.expectNoMessage("mode", 60);

    for (const bad of [
      { holdMs: 9999 },
      { holdMs: 300001 },
      { holdMs: 15000.5 },
      { holdMs: "15000" },
      { holdMs: null },
      { mode: "auto" },
      { mode: 1 },
      { shareText: "yes" },
      { shareText: 1 },
      [],
      "desk",
    ]) {
      res = await put(bad);
      expect([res.status, ((await res.json()) as { code: string }).code]).toEqual([400, "invalid_request"]);
    }
    expect((await roomState(t, token)).settings).toEqual({ mode: "desk", holdMs: 300000, shareText: true });
  });

  it("requires a cli token", async () => {
    const t = await start();
    const { token, roomId } = await t.mintToken();
    const device = t.deviceToken(roomId);
    expect((await t.fetch("/api/room/settings", { method: "PUT", json: {} })).status).toBe(401);
    expect((await t.fetch("/api/room/settings", { method: "PUT", json: {}, token: device.token })).status).toBe(403);
    expect((await t.fetch("/api/room", { token: device.token })).status).toBe(403);
    expect((await t.fetch("/api/room")).status).toBe(401);
    expect((await t.fetch("/api/room", { token })).status).toBe(200);
  });
});

describe("GET /api/room", () => {
  it("has the documented shape for an idle room", async () => {
    const t = await start();
    const { token, roomId } = await t.mintToken();
    expect(await roomState(t, token)).toEqual({
      roomId,
      settings: { mode: "ask", holdMs: 30000, shareText: false },
      peers: { producers: 0, consumers: 0, devices: 0 },
      pending: [],
      lastRequest: null,
      diagnostics: { dialogWhilePending: { count: 0, lastSeenAt: null } },
      devices: [],
    });
  });

  it("lists peers and pending requests with their holders", async () => {
    const t = await start({ timing: { ackMs: 5000, aliveMs: 5000 } });
    const { token, roomId } = await t.mintToken();
    const c = await track(consumer(t, token));
    const dashboard = await track(consumer(t, token, { kind: "dashboard" }));
    await track(producer(t, token));
    const held = await requestPermission(t, token, c);
    await dashboard.awaitMessage("permission");
    hold(c, held.id);
    hold(dashboard, held.id);
    await delay(60);

    const room = await roomState(t, token);
    expect(room).toEqual({
      roomId,
      settings: { mode: "ask", holdMs: 30000, shareText: false },
      peers: { producers: 1, consumers: 2, devices: 1 },
      pending: [
        {
          id: held.id,
          tool: "Bash",
          summary: "rm -rf dist",
          risk: "high",
          project: "devotchi",
          sid: held.permission.sid,
          ageMs: expect.any(Number),
          remainingMs: expect.any(Number),
          holders: ["sim-test", "dashboard"],
        },
      ],
      lastRequest: null,
      diagnostics: { dialogWhilePending: { count: 0, lastSeenAt: null } },
      devices: [],
    });
    expect(room.pending[0].ageMs + room.pending[0].remainingMs).toBe(30000);
    c.send({ type: "permission_defer", id: held.id });
    await held.reply;
  });

  it("keeps a room with a pending request or recent history through pruning", async () => {
    const t = await start({ timing: { ackMs: 5000 } });
    const { token, roomId } = await t.mintToken();
    const { rooms, config } = t.server.internals;
    const prune = (aheadMs: number) =>
      rooms.prune(Date.now() + aheadMs, config.timing.replayMaxAgeMs, config.timing.sessionTtlMs);
    const c = await track(consumer(t, token));
    const held = await requestPermission(t, token, c);
    prune(2 * 60 * 60_000);
    expect(rooms.get(roomId)?.pending).toHaveLength(1);
    c.send({ type: "permission_response", id: held.id, granted: true });
    await held.reply;
    c.close();
    await c.closed;
    await delay(30);

    prune(2 * 60 * 60_000);
    expect((await roomState(t, token)).lastRequest).toMatchObject({ outcome: "allow" });
    prune(25 * 60 * 60_000);
    expect(rooms.get(roomId)).toBeUndefined();
    expect((await roomState(t, token)).lastRequest).toBeNull();
  });
});

describe("POST /api/notify", () => {
  it("delivers to consumers with a default mood by event class and source as project", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const notify = (json: unknown) => t.fetch("/api/notify", { method: "POST", json, token });

    let res = await notify({ event: "deploy_complete" });
    expect([res.status, await res.json()]).toEqual([202, { delivered: 0 }]);

    const a = await track(consumer(t, token));
    expect(await a.awaitMessage("mood")).toMatchObject({ mood: "happy" });
    const b = await track(consumer(t, token, { kind: "dashboard" }));
    const p = await track(producer(t, token));

    res = await notify({ event: "tests_failed", detail: "3 of 47 failing", source: "ci", urgency: 2 });
    expect([res.status, await res.json()]).toEqual([202, { delivered: 2 }]);
    const expected = {
      type: "notify",
      event: "tests_failed",
      mood: "sad",
      detail: "3 of 47 failing",
      urgency: 2,
      source: "ci",
      project: "ci",
      timestamp: expect.any(String),
    };
    expect(await a.awaitMessage("notify")).toEqual(expected);
    expect(await b.awaitMessage("notify")).toEqual(expected);
    await p.expectNoMessage("notify", 60);

    await notify({ event: "backup_done" });
    expect(await a.awaitMessage("notify")).toEqual({ type: "notify", event: "backup_done", mood: "idle", timestamp: expect.any(String) });
    await notify({ event: "lint_passed", mood: "proud", detail: "x".repeat(400), source: "s".repeat(50), urgency: 0 });
    const cut = await a.awaitMessage("notify");
    expect(cut).toMatchObject({ mood: "proud", urgency: 0 });
    expect(cut.detail).toHaveLength(300);
    expect(cut.source).toHaveLength(32);
    expect(cut.project).toHaveLength(32);
  });

  it("validates the body", async () => {
    const t = await start();
    const { token, roomId } = await t.mintToken();
    const device = t.deviceToken(roomId);
    const c = await track(consumer(t, token));
    for (const bad of [
      {},
      { event: "Tests Passed" },
      { event: "" },
      { event: 5 },
      { event: "a".repeat(41) },
      { event: "ok", mood: "furious" },
      { event: "ok", mood: 3 },
      { event: "ok", urgency: 4 },
      { event: "ok", urgency: -1 },
      { event: "ok", urgency: 1.5 },
      { event: "ok", urgency: "2" },
      { event: "ok", detail: 5 },
      { event: "ok", source: {} },
      ["ok"],
    ]) {
      const res = await t.fetch("/api/notify", { method: "POST", json: bad, token });
      expect([res.status, ((await res.json()) as { code: string }).code]).toEqual([400, "invalid_request"]);
    }
    expect((await t.fetch("/api/notify", { method: "POST", token })).status).toBe(400);
    expect((await t.fetch("/api/notify", { method: "POST", json: { event: "ok" } })).status).toBe(401);
    expect((await t.fetch("/api/notify", { method: "POST", json: { event: "ok" }, token: device.token })).status).toBe(403);
    const big = await t.fetch("/api/notify", { method: "POST", json: { event: "ok", detail: "x".repeat(17_000) }, token });
    expect(big.status).toBe(413);
    await c.expectNoMessage("notify", 60);
  });

  it("allows 30 requests per minute per token", async () => {
    const t = await start();
    const a = await t.mintToken("a");
    const b = await t.mintToken("b");
    for (let i = 0; i < 30; i++) {
      expect((await t.fetch("/api/notify", { method: "POST", json: { event: "tick" }, token: a.token })).status).toBe(202);
    }
    const limited = await t.fetch("/api/notify", { method: "POST", json: { event: "tick" }, token: a.token });
    expect(limited.status).toBe(429);
    expect(await limited.json()).toMatchObject({ code: "rate_limited" });
    expect(Number(limited.headers.get("retry-after"))).toBeGreaterThan(0);
    expect((await t.fetch("/api/notify", { method: "POST", json: { event: "tick" }, token: b.token })).status).toBe(202);
  });
});
