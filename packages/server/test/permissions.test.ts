// The permission request lifecycle (protocol 8.3) over real HTTP and
// WebSocket connections, with shrunk timers.

import { afterEach, describe, expect, it } from "vitest";
import { TestSocket, consumer, delay, startTestServer, type Json, type StartOptions, type TestServer } from "./helpers.js";
import {
  ALLOW,
  DENY,
  followAckRule,
  heartbeat,
  hold,
  hook,
  hookInput,
  permissionInput,
  postHook,
  requestPermission,
  roomState,
  sidOf,
  until,
} from "./hook-helpers.js";

let ts: TestServer | undefined;
const sockets: TestSocket[] = [];
const cleanups: (() => void)[] = [];

/** No timer of the state machine fires on its own unless a test shrinks it. */
const PATIENT = { ackMs: 5000, aliveMs: 5000, holdMs: 8000 };

async function start(options: StartOptions = {}): Promise<TestServer> {
  ts = await startTestServer({ ...options, timing: { ...PATIENT, ...options.timing } });
  return ts;
}

async function track(socket: Promise<TestSocket>): Promise<TestSocket> {
  const s = await socket;
  sockets.push(s);
  return s;
}

afterEach(async () => {
  for (const cleanup of cleanups.splice(0)) cleanup();
  for (const socket of sockets.splice(0)) socket.close();
  await ts?.close();
  ts = undefined;
});

async function setup(options: StartOptions = {}) {
  const t = await start(options);
  const { token, roomId } = await t.mintToken();
  const c = await track(consumer(t, token));
  return { t, token, roomId, c };
}

function answer(c: TestSocket, id: string, granted: unknown): void {
  c.send({ type: "permission_response", id, granted } as Json);
}

describe("answers", () => {
  it("allows when a consumer grants, and tells every consumer who answered", async () => {
    const { t, token, c } = await setup();
    const other = await track(consumer(t, token, { kind: "dashboard" }));
    const held = await requestPermission(t, token, c);
    expect(held.permission).toMatchObject({
      type: "permission",
      id: expect.stringMatching(/^[0-9a-f]{32}$/),
      tool: "Bash",
      summary: "rm -rf dist",
      risk: "high",
      sid: sidOf("session-1"),
      project: "devotchi",
      timeout: 8000,
      timestamp: expect.any(String),
    });
    expect(await other.awaitMessage("permission")).toEqual(held.permission);

    answer(c, held.id, true);
    expect(await held.reply).toEqual(ALLOW);
    const dismiss = { type: "dismiss", id: held.id, reason: "answered", outcome: "allow", by: "sim-test" };
    expect(await c.awaitMessage("dismiss")).toEqual(dismiss);
    expect(await other.awaitMessage("dismiss")).toEqual(dismiss);
    const room = await roomState(t, token);
    expect(room.pending).toEqual([]);
    expect(room.lastRequest).toEqual({
      at: expect.any(String),
      tool: "Bash",
      outcome: "allow",
      detail: "answered",
      by: "sim-test",
    });
  });

  it("denies when a consumer refuses", async () => {
    const { t, token, c } = await setup();
    const dashboard = await track(consumer(t, token, { kind: "dashboard" }));
    const held = await requestPermission(t, token, c);
    answer(dashboard, held.id, false);
    expect(await held.reply).toEqual(DENY);
    expect(await c.awaitMessage("dismiss")).toEqual({
      type: "dismiss",
      id: held.id,
      reason: "answered",
      outcome: "deny",
      by: "dashboard",
    });
    expect((await roomState(t, token)).lastRequest).toMatchObject({ outcome: "deny", detail: "answered", by: "dashboard" });
  });

  it("names a paired device by its registry id and records its last holding", async () => {
    const t = await start();
    const { token, roomId } = await t.mintToken();
    const device = t.deviceToken(roomId, "dv-0a1b2c", "desk");
    const d = await track(consumer(t, device.token, { kind: "device", deviceId: "dv-ffffff" }));
    const held = await requestPermission(t, token, d);
    hold(d, held.id);
    await until(async () => (await roomState(t, token)).pending[0]?.holders.length === 1);
    expect((await roomState(t, token)).pending[0].holders).toEqual(["dv-0a1b2c"]);
    const row = t.server.internals.db.get<{ last_hold_at: string | null }>(
      "SELECT last_hold_at FROM devices WHERE token_id = ?",
      device.id,
    );
    expect(row?.last_hold_at).toMatch(/^\d{4}-\d{2}-\d{2}T/);

    answer(d, held.id, true);
    expect(await held.reply).toEqual(ALLOW);
    expect(await d.awaitMessage("dismiss")).toMatchObject({ reason: "answered", by: "dv-0a1b2c" });
  });

  it("lets one consumer hold while another answers", async () => {
    const { t, token, c } = await setup({ timing: { ackMs: 150 } });
    const other = await track(consumer(t, token, { kind: "sim", deviceId: "sim-other" }));
    const held = await requestPermission(t, token, c);
    cleanups.push(heartbeat(c, [held.id], 50));
    await delay(300);
    expect((await roomState(t, token)).pending[0].holders).toEqual(["sim-test"]);
    answer(other, held.id, true);
    expect(await held.reply).toEqual(ALLOW);
    expect(await c.awaitMessage("dismiss")).toMatchObject({ reason: "answered", outcome: "allow", by: "sim-other" });
  });

  it("accepts an answer during CREATED from a consumer that never sent holding", async () => {
    const { t, token, c } = await setup({ timing: { ackMs: 400 } });
    const held = await requestPermission(t, token, c);
    await delay(100);
    answer(c, held.id, true);
    expect(await held.reply).toEqual(ALLOW);
  });

  it("rejects a non-boolean granted with INVALID_MESSAGE and keeps the request pending", async () => {
    const { t, token, c } = await setup();
    const held = await requestPermission(t, token, c);
    for (const granted of ["false", 1, null]) {
      answer(c, held.id, granted);
      expect(await c.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE", id: held.id });
    }
    c.send({ type: "permission_response", id: held.id });
    expect(await c.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE", id: held.id });
    expect((await roomState(t, token)).pending.map((p) => p.id)).toEqual([held.id]);
    await c.expectNoMessage("dismiss", 50);

    answer(c, held.id, false);
    expect(await held.reply).toEqual(DENY);
  });

  it("answers a second response and an unknown id with UNKNOWN_REQUEST", async () => {
    const { t, token, c } = await setup();
    const held = await requestPermission(t, token, c);
    answer(c, held.id, true);
    answer(c, held.id, false);
    expect(await held.reply).toEqual(ALLOW);
    expect(await c.awaitMessage("error")).toEqual({
      type: "error",
      code: "UNKNOWN_REQUEST",
      message: expect.any(String),
      id: held.id,
    });
    const unknown = "f".repeat(32);
    c.send({ type: "permission_defer", id: unknown });
    expect(await c.awaitMessage("error")).toMatchObject({ code: "UNKNOWN_REQUEST", id: unknown });
    expect((await roomState(t, token)).lastRequest).toMatchObject({ outcome: "allow" });
  });

  it("refuses a response from another room and leaves that room's hook alone", async () => {
    const t = await start();
    const a = await t.mintToken("a");
    const b = await t.mintToken("b");
    const ca = await track(consumer(t, a.token));
    const cb = await track(consumer(t, b.token));
    const heldB = await requestPermission(t, b.token, cb);

    answer(ca, heldB.id, true);
    expect(await ca.awaitMessage("error")).toMatchObject({ code: "UNKNOWN_REQUEST", id: heldB.id });
    hold(ca, heldB.id);
    ca.send({ type: "permission_defer", id: heldB.id });
    expect(await ca.awaitMessage("error")).toMatchObject({ code: "UNKNOWN_REQUEST", id: heldB.id });
    await cb.expectNoMessage("dismiss", 80);
    const roomB = await roomState(t, b.token);
    expect(roomB.pending.map((p) => p.id)).toEqual([heldB.id]);
    expect(roomB.pending[0].holders).toEqual([]);
    expect((await roomState(t, a.token)).lastRequest).toBeNull();

    answer(cb, heldB.id, false);
    expect(await heldB.reply).toEqual(DENY);
  });
});

describe("neutral replies without a request", () => {
  it("replies {} at once in desk mode", async () => {
    const { t, token, c } = await setup();
    await t.fetch("/api/room/settings", { method: "PUT", json: { mode: "desk" }, token });
    await c.awaitMessage("mode");
    expect(await hook(t, token, permissionInput())).toEqual({});
    await c.expectNoMessage("permission", 80);
    expect((await roomState(t, token)).lastRequest).toEqual({
      at: expect.any(String),
      tool: "Bash",
      outcome: "neutral",
      detail: "desk",
    });
  });

  it("replies {} at once when no consumer is registered", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const started = Date.now();
    expect(await hook(t, token, permissionInput())).toEqual({});
    expect(Date.now() - started).toBeLessThan(1000);
    expect((await roomState(t, token)).lastRequest).toMatchObject({ outcome: "neutral", detail: "no_consumer" });
  });

  it("leaves AskUserQuestion and ExitPlanMode to the terminal for a phase-1 consumer", async () => {
    const { t, token, c } = await setup();
    for (const tool of ["AskUserQuestion", "ExitPlanMode"]) {
      expect(await hook(t, token, permissionInput({ tool_name: tool, tool_input: {} }))).toEqual({});
      expect((await roomState(t, token)).lastRequest).toMatchObject({ outcome: "neutral", detail: "skipped_tool" });
    }
    await c.expectNoMessage("permission", 80);
  });

  it("answers the ninth concurrent request with room_busy", async () => {
    const { t, token, c } = await setup();
    const replies = Array.from({ length: 8 }, (_, i) =>
      hook(t, token, permissionInput({ tool_input: { command: `echo ${i}` } })),
    );
    await until(() => c.received.filter((m) => m.type === "permission").length === 8);
    expect(await hook(t, token, permissionInput({ tool_input: { command: "echo 9" } }))).toEqual({});
    const room = await roomState(t, token);
    expect(room.lastRequest).toMatchObject({ outcome: "neutral", detail: "room_busy" });
    expect(room.pending).toHaveLength(8);
    expect(c.received.filter((m) => m.type === "permission")).toHaveLength(8);

    for (const p of room.pending) answer(c, p.id, true);
    expect(await Promise.all(replies)).toEqual(Array(8).fill(ALLOW));
  });
});

describe("neutral resolutions", () => {
  it("gives up with no_ack when nobody acknowledges, even though a consumer is connected", async () => {
    const { t, token, c } = await setup({ timing: { ackMs: 120 } });
    const started = Date.now();
    const held = await requestPermission(t, token, c);
    expect(await held.reply).toEqual({});
    expect(Date.now() - started).toBeGreaterThanOrEqual(110);
    expect(await c.awaitMessage("dismiss")).toEqual({ type: "dismiss", id: held.id, reason: "released" });
    expect((await roomState(t, token)).lastRequest).toEqual({
      at: expect.any(String),
      tool: "Bash",
      outcome: "neutral",
      detail: "no_ack",
    });
  });

  it("releases with holder_lost when the holder falls silent", async () => {
    const { t, token, c } = await setup({ timing: { ackMs: 100, aliveMs: 250 } });
    const watcher = await track(consumer(t, token, { kind: "dashboard" }));
    const held = await requestPermission(t, token, c);
    hold(c, held.id);
    await delay(160);
    expect((await roomState(t, token)).pending).toHaveLength(1);
    expect(await held.reply).toEqual({});
    for (const socket of [c, watcher]) {
      expect(await socket.awaitMessage("dismiss")).toEqual({ type: "dismiss", id: held.id, reason: "released" });
    }
    expect((await roomState(t, token)).lastRequest).toMatchObject({ outcome: "neutral", detail: "holder_lost" });
  });

  it("stays held while heartbeats keep arriving", async () => {
    const { t, token, c } = await setup({ timing: { ackMs: 100, aliveMs: 200 } });
    const held = await requestPermission(t, token, c);
    cleanups.push(heartbeat(c, [held.id], 60));
    await delay(600);
    expect((await roomState(t, token)).pending.map((p) => p.id)).toEqual([held.id]);
    answer(c, held.id, true);
    expect(await held.reply).toEqual(ALLOW);
  });

  it("releases with holder_lost as soon as the holder's socket closes", async () => {
    const { t, token, c } = await setup();
    const watcher = await track(consumer(t, token, { kind: "dashboard" }));
    const held = await requestPermission(t, token, c);
    hold(c, held.id);
    await until(async () => (await roomState(t, token)).pending[0]?.holders.length === 1);
    const started = Date.now();
    c.close();
    expect(await held.reply).toEqual({});
    expect(Date.now() - started).toBeLessThan(1000);
    expect(await watcher.awaitMessage("dismiss")).toEqual({ type: "dismiss", id: held.id, reason: "released" });
    expect((await roomState(t, token)).lastRequest).toMatchObject({ outcome: "neutral", detail: "holder_lost" });
  });

  it("keeps a request when a consumer that was not holding it disconnects", async () => {
    const { t, token, c } = await setup();
    const bystander = await track(consumer(t, token, { kind: "dashboard" }));
    const held = await requestPermission(t, token, c);
    hold(c, held.id);
    bystander.close();
    await bystander.closed;
    await delay(50);
    expect((await roomState(t, token)).pending.map((p) => p.id)).toEqual([held.id]);
    answer(c, held.id, true);
    expect(await held.reply).toEqual(ALLOW);
  });

  it("hands the request to the terminal on permission_defer", async () => {
    const { t, token, c } = await setup();
    const held = await requestPermission(t, token, c);
    hold(c, held.id);
    c.send({ type: "permission_defer", id: held.id });
    expect(await held.reply).toEqual({});
    expect(await c.awaitMessage("dismiss")).toEqual({ type: "dismiss", id: held.id, reason: "released" });
    expect((await roomState(t, token)).lastRequest).toMatchObject({ outcome: "neutral", detail: "deferred" });
  });

  it("times out neutrally when the hold time ends with a live holder", async () => {
    const { t, token, c } = await setup({ timing: { ackMs: 100, holdMs: 350 } });
    const held = await requestPermission(t, token, c);
    expect(held.permission.timeout).toBe(1000);
    cleanups.push(heartbeat(c, [held.id], 50));
    expect(await held.reply).toEqual({});
    expect(await c.awaitMessage("dismiss")).toEqual({ type: "dismiss", id: held.id, reason: "timeout" });
    expect((await roomState(t, token)).lastRequest).toMatchObject({ outcome: "neutral", detail: "timeout" });
  });

  it("uses the room's holdMs for the hold time", async () => {
    const { t, token, c } = await setup({ timing: { holdMs: 300 } });
    const res = await t.fetch("/api/room/settings", { method: "PUT", json: { holdMs: 12000 }, token });
    expect(res.status).toBe(200);
    const held = await requestPermission(t, token, c);
    expect(held.permission.timeout).toBe(12000);
    await delay(400);
    expect((await roomState(t, token)).pending[0].remainingMs).toBeGreaterThan(11000);
    answer(c, held.id, true);
    expect(await held.reply).toEqual(ALLOW);
  });

  it("answers held hooks with {} on shutdown and dismisses them as released", async () => {
    const { t, token, c } = await setup();
    const first = await requestPermission(t, token, c);
    const second = await requestPermission(t, token, c, { tool_input: { command: "ls" } });
    hold(c, first.id);
    const rooms = t.server.internals.rooms;
    const room = rooms.get((await roomState(t, token)).roomId);
    const closing = t.server.close();
    expect(await first.reply).toEqual({});
    expect(await second.reply).toEqual({});
    await closing;
    const dismissed = c.received.filter((m) => m.type === "dismiss");
    expect(dismissed).toEqual([
      { type: "dismiss", id: first.id, reason: "released" },
      { type: "dismiss", id: second.id, reason: "released" },
    ]);
    expect(room?.lastRequest).toMatchObject({ outcome: "neutral", detail: "shutdown" });
    expect(room?.pending).toEqual([]);
  });
});

describe("hook client disconnects", () => {
  it("keeps a held request pending after the request body was sent, until a consumer answers", async () => {
    const { t, token, c } = await setup();
    let settled = false;
    const reply = hook(t, token, permissionInput()).finally(() => (settled = true));
    const permission = await c.awaitMessage("permission");
    await delay(600);
    expect(settled).toBe(false);
    await c.expectNoMessage("dismiss", 0);
    const room = await roomState(t, token);
    expect(room.pending.map((p) => p.id)).toEqual([permission.id]);
    expect(room.pending[0].ageMs).toBeGreaterThanOrEqual(500);

    answer(c, permission.id as string, true);
    expect(await reply).toEqual(ALLOW);
  });

  it("cancels the request when the hook request is aborted", async () => {
    const { t, token, c } = await setup();
    const abort = new AbortController();
    const reply = postHook(t, token, permissionInput(), { signal: abort.signal });
    const outcome = reply.then(
      () => "answered",
      (err: Error) => err.name,
    );
    const permission = await c.awaitMessage("permission");
    hold(c, permission.id as string);
    await delay(100);
    abort.abort();
    expect(await outcome).toBe("AbortError");
    expect(await c.awaitMessage("dismiss")).toEqual({ type: "dismiss", id: permission.id, reason: "cancelled" });
    const room = await roomState(t, token);
    expect(room.pending).toEqual([]);
    expect(room.lastRequest).toMatchObject({ outcome: "neutral", detail: "cancelled" });

    answer(c, permission.id as string, true);
    expect(await c.awaitMessage("error")).toMatchObject({ code: "UNKNOWN_REQUEST", id: permission.id });
    expect((await roomState(t, token)).lastRequest).toMatchObject({ outcome: "neutral", detail: "cancelled" });
  });
});

describe("bursts", () => {
  for (const count of [3, 8]) {
    it(`holds all ${count} requests of a burst for a consumer that follows the debounced ack rule`, async () => {
      const { t, token, c } = await setup({ timing: { ackMs: 700 } });
      const acks = followAckRule(c);
      cleanups.push(acks.stop);
      const started = Date.now();
      const replies = Array.from({ length: count }, (_, i) =>
        hook(t, token, permissionInput({ tool_input: { command: `echo ${i}` } })),
      );
      await until(() => c.received.filter((m) => m.type === "permission").length === count);
      expect(Date.now() - started).toBeLessThan(700);
      await delay(1000);

      const room = await roomState(t, token);
      expect(room.pending).toHaveLength(count);
      for (const p of room.pending) expect(p.holders).toEqual(["sim-test"]);
      expect(acks.frames[0]).toHaveLength(count);
      expect(c.received.filter((m) => m.type === "error" || m.type === "dismiss")).toEqual([]);

      for (const p of room.pending) answer(c, p.id, true);
      expect(await Promise.all(replies)).toEqual(Array(count).fill(ALLOW));
    });

    it(`holds all ${count} requests of a burst for a consumer that sends one holding per request`, async () => {
      const { t, token, c } = await setup({ timing: { ackMs: 700 } });
      c.ws.on("message", (data) => {
        const msg = JSON.parse(data.toString()) as Json;
        if (msg.type === "permission") hold(c, msg.id as string);
      });
      const replies = Array.from({ length: count }, (_, i) =>
        hook(t, token, permissionInput({ tool_input: { command: `echo ${i}` } })),
      );
      await until(() => c.received.filter((m) => m.type === "permission").length === count);
      await delay(1000);

      const room = await roomState(t, token);
      expect(room.pending).toHaveLength(count);
      for (const p of room.pending) expect(p.holders).toEqual(["sim-test"]);
      expect(c.received.filter((m) => m.type === "error" || m.type === "dismiss")).toEqual([]);

      for (const p of room.pending) answer(c, p.id, false);
      expect(await Promise.all(replies)).toEqual(Array(count).fill(DENY));
    });
  }
});

describe("replay on register", () => {
  it("sends pending requests with the remaining time, once", async () => {
    const { t, token, c } = await setup({ timing: { holdMs: 3000 } });
    const held = await requestPermission(t, token, c);
    hold(c, held.id);
    await delay(300);

    const late = await track(TestSocket.connect(t.wsUrl, { token }));
    await late.register("consumer", { kind: "sim", deviceId: "sim-late" });
    const replayed = await late.awaitMessage("permission");
    expect(replayed).toMatchObject({ id: held.id, tool: "Bash", summary: "rm -rf dist", risk: "high" });
    expect(replayed.timeout).toBeGreaterThanOrEqual(1000);
    expect(replayed.timeout).toBeLessThanOrEqual(2700);

    late.send({ type: "register", role: "consumer", proto: 2, client: { kind: "sim", deviceId: "sim-late" } });
    expect(await late.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE" });
    await late.expectNoMessage("permission", 100);

    answer(late, held.id, true);
    expect(await held.reply).toEqual(ALLOW);
    expect(await late.awaitMessage("dismiss")).toMatchObject({ by: "sim-late" });
  });

  it("does not replay a request with less than 1000 ms left", async () => {
    const { t, token, c } = await setup({ timing: { holdMs: 1400 } });
    const held = await requestPermission(t, token, c);
    hold(c, held.id);
    await delay(500);
    const late = await track(consumer(t, token, { kind: "sim", deviceId: "sim-late" }));
    await late.expectNoMessage("permission", 150);
    expect((await roomState(t, token)).pending).toHaveLength(1);
    expect(await held.reply).toEqual({});
    expect(await late.awaitMessage("dismiss")).toEqual({ type: "dismiss", id: held.id, reason: "timeout" });
  });

  it("does not replay a request that was already resolved", async () => {
    const { t, token, c } = await setup();
    const held = await requestPermission(t, token, c);
    answer(c, held.id, true);
    await held.reply;
    const late = await track(consumer(t, token, { kind: "sim", deviceId: "sim-late" }));
    await late.expectNoMessage("permission", 100);
  });
});

describe("cancellation by later hook events", () => {
  const tool = { tool_name: "Bash", tool_input: { command: "make", description: "build" } };

  async function expectCancelled(t: TestServer, token: string, c: TestSocket, held: { id: string; reply: Promise<unknown> }) {
    expect(await held.reply).toEqual({});
    expect(await c.awaitMessage((m) => m.type === "dismiss" && m.id === held.id)).toEqual({
      type: "dismiss",
      id: held.id,
      reason: "cancelled",
    });
    expect((await roomState(t, token)).lastRequest).toMatchObject({ outcome: "neutral", detail: "cancelled" });
    answer(c, held.id, true);
    expect(await c.awaitMessage("error")).toMatchObject({ code: "UNKNOWN_REQUEST", id: held.id });
  }

  for (const event of ["PostToolUse", "PostToolUseFailure"]) {
    it(`${event} with the same session, tool and input cancels the oldest match only`, async () => {
      const { t, token, c } = await setup();
      const first = await requestPermission(t, token, c, tool);
      const second = await requestPermission(t, token, c, tool);
      const other = await requestPermission(t, token, c, { tool_name: "Bash", tool_input: { command: "make test" } });

      // Same input with another key order is the same call.
      await hook(t, token, hookInput(event, { tool_name: "Bash", tool_input: { description: "build", command: "make" } }));
      await expectCancelled(t, token, c, first);
      expect((await roomState(t, token)).pending.map((p) => p.id)).toEqual([second.id, other.id]);

      await hook(t, token, hookInput(event, { tool_name: "Bash", tool_input: { command: "make install" } }));
      await hook(t, token, hookInput(event, { tool_name: "Write", tool_input: tool.tool_input }));
      await hook(t, token, hookInput(event, { ...tool, session_id: "session-2" }));
      await delay(50);
      expect((await roomState(t, token)).pending.map((p) => p.id)).toEqual([second.id, other.id]);

      answer(c, second.id, true);
      answer(c, other.id, false);
      expect(await second.reply).toEqual(ALLOW);
      expect(await other.reply).toEqual(DENY);
    });
  }

  it("PostToolBatch cancels the requests of the same agent only", async () => {
    const { t, token, c } = await setup();
    const main = await requestPermission(t, token, c, tool);
    const sub1 = await requestPermission(t, token, c, { ...tool, agent_id: "a1" });
    const sub2 = await requestPermission(t, token, c, { ...tool, agent_id: "a1", tool_input: { command: "ls" } });
    const otherAgent = await requestPermission(t, token, c, { ...tool, agent_id: "a2" });

    await hook(t, token, hookInput("PostToolBatch", { agent_id: "a1" }));
    expect(await sub1.reply).toEqual({});
    expect(await sub2.reply).toEqual({});
    expect((await roomState(t, token)).pending.map((p) => p.id)).toEqual([main.id, otherAgent.id]);

    await hook(t, token, hookInput("PostToolBatch"));
    await expectCancelled(t, token, c, main);
    expect((await roomState(t, token)).pending.map((p) => p.id)).toEqual([otherAgent.id]);
    answer(c, otherAgent.id, true);
    expect(await otherAgent.reply).toEqual(ALLOW);
  });

  for (const event of ["Stop", "StopFailure", "UserPromptSubmit"]) {
    it(`${event} cancels main-conversation requests and leaves a subagent's pending`, async () => {
      const { t, token, c } = await setup();
      const main1 = await requestPermission(t, token, c, tool);
      const main2 = await requestPermission(t, token, c, { tool_name: "Write", tool_input: { file_path: "/tmp/x" } });
      const sub = await requestPermission(t, token, c, { ...tool, agent_id: "a1" });
      const otherSession = await requestPermission(t, token, c, { ...tool, session_id: "session-2" });

      await hook(t, token, hookInput(event));
      expect(await main2.reply).toEqual({});
      await expectCancelled(t, token, c, main1);
      expect((await roomState(t, token)).pending.map((p) => p.id)).toEqual([sub.id, otherSession.id]);

      answer(c, sub.id, true);
      answer(c, otherSession.id, true);
      expect(await sub.reply).toEqual(ALLOW);
      expect(await otherSession.reply).toEqual(ALLOW);
    });
  }

  it("SessionEnd cancels every request of the session", async () => {
    const { t, token, c } = await setup();
    const main = await requestPermission(t, token, c, tool);
    const sub = await requestPermission(t, token, c, { ...tool, agent_id: "a1" });
    const otherSession = await requestPermission(t, token, c, { ...tool, session_id: "session-2" });

    await hook(t, token, hookInput("SessionEnd"));
    expect(await sub.reply).toEqual({});
    await expectCancelled(t, token, c, main);
    expect((await roomState(t, token)).pending.map((p) => p.id)).toEqual([otherSession.id]);
    answer(c, otherSession.id, false);
    expect(await otherSession.reply).toEqual(DENY);
  });

  it("an own MCP tool's PostToolUse cancels its request without emitting a status", async () => {
    const { t, token, c } = await setup();
    const call = { tool_name: "mcp__devotchi__show_status", tool_input: { msg: "Building" } };
    const held = await requestPermission(t, token, c, call);
    expect(held.permission.tool).toBe("devotchi: show_status");
    await hook(t, token, hookInput("PostToolUse", call));
    expect(await held.reply).toEqual({});
    expect(await c.awaitMessage("dismiss")).toMatchObject({ id: held.id, reason: "cancelled" });
    await c.expectNoMessage("status", 100);
  });
});

describe("frames and odd input", () => {
  it("keeps the permission frame within 2048 bytes for a 2 MB tool_input", async () => {
    const { t, token, c } = await setup();
    const sizes: number[] = [];
    c.ws.on("message", (data) => sizes.push((data as Buffer).length));
    const content = '"\\é\n'.repeat(245_000);
    const body = permissionInput({ tool_name: "Write", tool_input: { file_path: "/tmp/big.txt", content } });
    expect(Buffer.byteLength(JSON.stringify(body))).toBeGreaterThan(1_900_000);
    const reply = hook(t, token, body);
    const permission = await c.awaitMessage("permission");
    expect(Math.max(...sizes)).toBeLessThanOrEqual(2048);
    expect(JSON.stringify(permission)).toMatch(/^[\x20-\x7e]*$/);
    answer(c, permission.id as string, true);
    expect(await reply).toEqual(ALLOW);
  });

  it("never answers 500 to missing, odd or malformed hook input", async () => {
    const { t, token, c } = await setup({ timing: { ackMs: 60 } });
    const bodies: unknown[] = [
      {},
      [],
      "text",
      42,
      null,
      { hook_event_name: 7 },
      { hook_event_name: "NoSuchEvent" },
      { hook_event_name: "PermissionRequest" },
      { hook_event_name: "PermissionRequest", tool_name: 42, tool_input: "x", session_id: {}, cwd: ["a"], agent_id: 3 },
      { hook_event_name: "PermissionRequest", tool_name: null, tool_input: null, session_id: "", cwd: "" },
      { hook_event_name: "PostToolUse", tool_name: { a: 1 }, tool_input: [1, 2], session_id: 5 },
      { hook_event_name: "PostToolUseFailure", tool_name: 1 },
      { hook_event_name: "Notification", notification_type: 12 },
      { hook_event_name: "Stop", background_tasks: "many", last_assistant_message: { text: 1 } },
      { hook_event_name: "StopFailure", error: { code: 1 } },
      { hook_event_name: "UserPromptSubmit", prompt: 9 },
      { hook_event_name: "SessionEnd", session_id: null },
    ];
    for (const body of bodies) {
      const res = await postHook(t, token, body);
      expect([res.status, await res.json()]).toEqual([200, {}]);
    }
    const empty = await t.fetch("/hook/event", { method: "POST", token });
    expect([empty.status, await empty.json()]).toEqual([200, {}]);
    const garbage = await t.fetch("/hook/event", {
      method: "POST",
      token,
      body: "{not json",
      headers: { "Content-Type": "application/json" },
    });
    expect(garbage.status).toBe(400);
    const text = await t.fetch("/hook/event", { method: "POST", token, body: "hello", headers: { "Content-Type": "text/plain" } });
    expect(text.status).toBe(400);
    expect(c.received.filter((m) => m.type === "permission").length).toBe(3);
    expect(t.logs.some((line) => line.includes('"level":"error"'))).toBe(false);
  });

  it("requires a cli token, with or without the X-Devotchi-Hook header", async () => {
    const { t, token, roomId } = await setup();
    const device = t.deviceToken(roomId);
    expect((await postHook(t, "00000000-0000-4000-8000-000000000000", permissionInput())).status).toBe(401);
    expect((await t.fetch("/hook/event", { method: "POST", json: permissionInput() })).status).toBe(401);
    expect((await postHook(t, device.token, permissionInput())).status).toBe(403);

    const withHeader = await postHook(t, token, hookInput("PostToolBatch"), { headers: { "X-Devotchi-Hook": "1" } });
    expect([withHeader.status, await withHeader.json()]).toEqual([200, {}]);
    const queryToken = await t.fetch(`/hook/event?token=${token}`, { method: "POST", json: hookInput("PostToolBatch") });
    expect([queryToken.status, await queryToken.json()]).toEqual([200, {}]);
  });
});
