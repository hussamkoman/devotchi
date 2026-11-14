// Hook events other than the permission lifecycle (protocol 8.2, 8.4, 8.5).

import { afterEach, describe, expect, it } from "vitest";
import { TestSocket, consumer, delay, producer, startTestServer, type Json, type StartOptions, type TestServer } from "./helpers.js";
import { ALLOW, hold, hook, hookInput, permissionInput, requestPermission, roomState, sidOf } from "./hook-helpers.js";

let ts: TestServer | undefined;
const sockets: TestSocket[] = [];

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

async function setup(options: StartOptions = {}) {
  ts = await startTestServer({ ...options, timing: { ackMs: 5000, aliveMs: 5000, holdMs: 8000, ...options.timing } });
  const t = ts;
  const { token, roomId } = await t.mintToken();
  const c = await track(consumer(t, token));
  return { t, token, roomId, c };
}

const SID = sidOf("session-1");
const session = { sid: SID, project: "devotchi" };
const bash = (command: string): Json => ({ tool_name: "Bash", tool_input: { command } });

/** A message without its server timestamp, after checking that it has one. */
function content(msg: Json): Json {
  const { timestamp, ...rest } = msg;
  expect(timestamp).toEqual(expect.stringMatching(/^\d{4}-\d{2}-\d{2}T[\d:.]+Z$/));
  return rest;
}

describe("sessions", () => {
  it("derives sid and an ASCII project of at most 32 bytes from the hook input", async () => {
    const { t, token, c } = await setup();
    await hook(t, token, hookInput("UserPromptSubmit", { cwd: "/home/dev/prøject-with-a-very-long-directory-name-indeed/" }));
    const status = await c.awaitMessage("status");
    expect(status.sid).toBe(SID);
    expect(status.project).toBe("pr?ject-with-a-very-long-directo");
    expect(status.project).toHaveLength(32);

    await hook(t, token, hookInput("UserPromptSubmit", { cwd: "C:\\Users\\dev\\win-proj", session_id: "other" }));
    expect(content(await c.awaitMessage("status"))).toEqual({
      type: "status",
      msg: "Thinking...",
      mood: "thinking",
      sid: sidOf("other"),
      project: "win-proj",
    });
  });

  it("omits sid and project when the hook input has neither", async () => {
    const { t, token, c } = await setup();
    await hook(t, token, { hook_event_name: "UserPromptSubmit" });
    expect(content(await c.awaitMessage("status"))).toEqual({ type: "status", msg: "Thinking...", mood: "thinking" });
  });
});

describe("event mapping", () => {
  it("UserPromptSubmit emits Thinking... and never the prompt text", async () => {
    const { t, token, c } = await setup();
    await hook(t, token, hookInput("UserPromptSubmit", { prompt: "my secret plan" }));
    expect(content(await c.awaitMessage("status"))).toEqual({ type: "status", msg: "Thinking...", mood: "thinking", ...session });
    expect(JSON.stringify(c.received)).not.toContain("secret");
  });

  it("PostToolUse emits the tool summary", async () => {
    const { t, token, c } = await setup();
    await hook(t, token, hookInput("PostToolUse", bash("pnpm test")));
    expect(content(await c.awaitMessage("status"))).toEqual({ type: "status", msg: "pnpm test", mood: "working", ...session });
  });

  it("coalesces PostToolUse statuses to two per second, the latest winning", async () => {
    const { t, token, c } = await setup();
    for (let i = 0; i < 10; i++) {
      await hook(t, token, hookInput("PostToolUse", bash(`step ${i}`)));
      await delay(8);
    }
    await delay(900);
    const statuses = c.received.filter((m) => m.type === "status");
    expect(statuses.map((m) => m.msg)).toEqual(["step 0", "step 9"]);
    expect(Date.parse(statuses[1].timestamp as string) - Date.parse(statuses[0].timestamp as string)).toBeGreaterThanOrEqual(490);

    await hook(t, token, hookInput("PostToolUse", bash("later")));
    expect(await c.awaitMessage((m) => m.type === "status" && m.msg === "later", 300)).toBeTruthy();
  });

  it("drops a waiting tool status when the turn ends", async () => {
    const { t, token, c } = await setup();
    await hook(t, token, hookInput("PostToolUse", bash("one")));
    await hook(t, token, hookInput("PostToolUse", bash("two")));
    await hook(t, token, hookInput("Stop"));
    await delay(700);
    expect(c.received.filter((m) => m.type === "status").map((m) => m.msg)).toEqual(["one"]);
  });

  it("PostToolUseFailure emits '<tool> failed' without a mood and notifies on the third in a row", async () => {
    const { t, token, c } = await setup();
    const fail = () => hook(t, token, hookInput("PostToolUseFailure", bash("make")));
    await fail();
    expect(content(await c.awaitMessage("status"))).toEqual({ type: "status", msg: "Bash failed", ...session });
    await fail();
    await hook(t, token, hookInput("PostToolUse", bash("ls")));
    await fail();
    await fail();
    await c.expectNoMessage("notify", 80);
    await fail();
    expect(content(await c.awaitMessage("notify"))).toEqual({
      type: "notify",
      event: "error",
      mood: "angry",
      detail: "3 tool failures in a row",
      urgency: 2,
      ...session,
    });
    await fail();
    await fail();
    await c.expectNoMessage("notify", 80);
    await fail();
    expect(await c.awaitMessage("notify")).toMatchObject({ event: "error", mood: "angry" });
    for (const status of c.received.filter((m) => m.type === "status" && m.msg === "Bash failed")) {
      expect(status).not.toHaveProperty("mood");
    }
  });

  it("emits no status for Devotchi's own MCP tools but still counts their failures", async () => {
    const { t, token, c } = await setup();
    const own = { tool_name: "mcp__devotchi__set_mood", tool_input: { mood: "happy" } };
    await hook(t, token, hookInput("PostToolUse", own));
    for (let i = 0; i < 3; i++) await hook(t, token, hookInput("PostToolUseFailure", own));
    expect(await c.awaitMessage("notify")).toMatchObject({ event: "error", detail: "3 tool failures in a row" });
    expect(c.received.filter((m) => m.type === "status")).toEqual([]);

    await hook(t, token, hookInput("PostToolUse", { tool_name: "mcp__github__create_issue", tool_input: { title: "x" } }));
    expect(await c.awaitMessage("status")).toMatchObject({ msg: 'title="x"' });
  });

  it("PostToolBatch and unknown events emit nothing", async () => {
    const { t, token, c } = await setup();
    for (const event of ["PostToolBatch", "SessionStart", "PreToolUse", "SubagentStop"]) {
      expect(await hook(t, token, hookInput(event))).toEqual({});
    }
    await delay(100);
    expect(c.received.filter((m) => !["register_ack", "peers", "ping"].includes(m.type as string))).toEqual([]);
  });

  it("Notification maps idle_prompt, agent_needs_input and permission_prompt, and ignores other types", async () => {
    const { t, token, c } = await setup();
    for (const type of ["idle_prompt", "agent_needs_input"]) {
      await hook(t, token, hookInput("Notification", { notification_type: type, message: "private text" }));
      expect(content(await c.awaitMessage("notify"))).toEqual({
        type: "notify",
        event: "needs_input",
        mood: "anxious",
        detail: "Claude is waiting",
        urgency: 3,
        ...session,
      });
    }
    await hook(t, token, hookInput("Notification", { notification_type: "permission_prompt" }));
    expect(content(await c.awaitMessage("notify"))).toEqual({
      type: "notify",
      event: "needs_input",
      mood: "anxious",
      detail: "Permission needed in terminal",
      urgency: 2,
      ...session,
    });
    await hook(t, token, hookInput("Notification", { notification_type: "auth_success" }));
    await hook(t, token, hookInput("Notification"));
    await c.expectNoMessage("notify", 100);
    expect(JSON.stringify(c.received)).not.toContain("private text");
  });

  it("Stop reports the duration since the last prompt, or just Done", async () => {
    const { t, token, roomId, c } = await setup();
    await hook(t, token, hookInput("Stop", { background_tasks: [] }));
    expect(content(await c.awaitMessage("notify"))).toEqual({
      type: "notify",
      event: "task_complete",
      mood: "sleeping",
      detail: "Done",
      urgency: 2,
      ...session,
    });

    await hook(t, token, hookInput("UserPromptSubmit"));
    const state = t.server.internals.rooms.get(roomId)?.sessions.get(SID);
    expect(state?.lastPromptAt).toEqual(expect.any(Number));
    if (state?.lastPromptAt) state.lastPromptAt -= 252_000;
    await hook(t, token, hookInput("Stop", { last_assistant_message: "All done.\nDetails follow" }));
    expect(await c.awaitMessage("notify")).toMatchObject({ event: "task_complete", mood: "sleeping", detail: "Done (4m12s)" });

    await hook(t, token, hookInput("UserPromptSubmit"));
    await hook(t, token, hookInput("Stop"));
    expect(await c.awaitMessage("notify")).toMatchObject({ detail: "Done (0m00s)" });
  });

  it("Stop includes the first line of the last message only when shareText is on", async () => {
    const { t, token, roomId, c } = await setup();
    const put = await t.fetch("/api/room/settings", { method: "PUT", json: { shareText: true }, token });
    expect(put.status).toBe(200);
    const message = `\n  ${"é".repeat(100)} tail\nsecond line`;
    await hook(t, token, hookInput("Stop", { last_assistant_message: message }));
    const first = await c.awaitMessage("notify");
    expect(first.detail).toBe("é".repeat(60));

    await hook(t, token, hookInput("UserPromptSubmit"));
    const state = t.server.internals.rooms.get(roomId)?.sessions.get(SID);
    if (state?.lastPromptAt) state.lastPromptAt -= 65_000;
    await hook(t, token, hookInput("Stop", { last_assistant_message: "Refactor finished.\nSummary: ..." }));
    expect(await c.awaitMessage("notify")).toMatchObject({ event: "task_complete", detail: "Refactor finished. (1m05s)" });

    await hook(t, token, hookInput("Stop", { last_assistant_message: "  \n" }));
    expect((await c.awaitMessage("notify")).detail).toMatch(/^Done \(/);
  });

  it("Stop with background tasks reports task_paused", async () => {
    const { t, token, c } = await setup();
    await hook(t, token, hookInput("Stop", { background_tasks: [{ id: "a" }, { id: "b" }] }));
    expect(content(await c.awaitMessage("notify"))).toEqual({
      type: "notify",
      event: "task_paused",
      mood: "idle",
      detail: "2 background tasks",
      urgency: 1,
      ...session,
    });
    await hook(t, token, hookInput("Stop", { background_tasks: [{ id: "a" }] }));
    expect(await c.awaitMessage("notify")).toMatchObject({ event: "task_paused", detail: "1 background task" });
  });

  it("StopFailure reports the error", async () => {
    const { t, token, c } = await setup();
    await hook(t, token, hookInput("StopFailure", { error: "rate_limit", last_assistant_message: "API Error: private" }));
    expect(content(await c.awaitMessage("notify"))).toEqual({
      type: "notify",
      event: "error",
      mood: "sad",
      detail: "rate_limit",
      urgency: 2,
      ...session,
    });
    await hook(t, token, hookInput("StopFailure", { error: "x".repeat(500) }));
    expect(((await c.awaitMessage("notify")).detail as string).length).toBe(120);
  });

  it("SessionEnd and 30 minutes of silence forget the session", async () => {
    const { t, token, roomId, c } = await setup();
    const rooms = t.server.internals.rooms;
    await hook(t, token, hookInput("UserPromptSubmit"));
    expect(rooms.get(roomId)?.sessions.has(SID)).toBe(true);
    await hook(t, token, hookInput("SessionEnd"));
    expect(rooms.get(roomId)?.sessions.has(SID)).toBe(false);
    await hook(t, token, hookInput("Stop"));
    expect(await c.awaitMessage("notify")).toMatchObject({ detail: "Done" });

    await hook(t, token, hookInput("UserPromptSubmit"));
    const { timing } = t.server.internals.config;
    rooms.prune(Date.now() + timing.sessionTtlMs - 60_000, timing.replayMaxAgeMs, timing.sessionTtlMs);
    expect(rooms.get(roomId)?.sessions.has(SID)).toBe(true);
    rooms.prune(Date.now() + timing.sessionTtlMs + 1000, timing.replayMaxAgeMs, timing.sessionTtlMs);
    expect(rooms.get(roomId)?.sessions.has(SID)).toBe(false);
  });

  it("serves the v1 routes as aliases for bodies without hook_event_name", async () => {
    const { t, token, c } = await setup();
    const post = async (path: string, body: Json) => {
      const res = await t.fetch(path, { method: "POST", json: body, token });
      expect(res.status).toBe(200);
      return res.json();
    };
    expect(await post("/hook/status", bash("ls -la"))).toEqual({});
    expect(await c.awaitMessage("status")).toMatchObject({ msg: "ls -la" });
    expect(await post("/hook/stop", {})).toEqual({});
    expect(await c.awaitMessage("notify")).toMatchObject({ event: "task_complete", detail: "Done" });

    const reply = post("/hook/permission", bash("rm -rf /tmp/x"));
    const permission = await c.awaitMessage("permission");
    c.send({ type: "permission_response", id: permission.id, granted: true });
    expect(await reply).toEqual(ALLOW);

    // A body that names its event is dispatched by that name on every route.
    expect(await post("/hook/stop", hookInput("UserPromptSubmit"))).toEqual({});
    expect(await c.awaitMessage("status")).toMatchObject({ msg: "Thinking..." });
    expect(await post("/hook/event", {})).toEqual({});
  });

  it("replies before a slow consumer could matter and keeps emitting to the others", async () => {
    const { t, token, c } = await setup();
    const started = Date.now();
    for (let i = 0; i < 20; i++) await hook(t, token, hookInput("Notification", { notification_type: "idle_prompt" }));
    expect(Date.now() - started).toBeLessThan(2000);
    await delay(50);
    expect(c.received.filter((m) => m.type === "notify")).toHaveLength(20);
  });
});

describe("mood rule for PostToolUse statuses", () => {
  it("adds working only while the room mood is thinking, idle or sleeping", async () => {
    const { t, token, roomId, c } = await setup({ timing: { hookStatusIntervalMs: 0 } });
    const p = await track(producer(t, token));
    const statusAfter = async (command: string) => {
      await hook(t, token, hookInput("PostToolUse", bash(command)));
      return c.awaitMessage((m) => m.type === "status" && m.msg === command);
    };

    await hook(t, token, hookInput("UserPromptSubmit"));
    expect(await c.awaitMessage("status")).toMatchObject({ msg: "Thinking...", mood: "thinking" });
    expect(await statusAfter("first")).toMatchObject({ mood: "working" });
    expect(await statusAfter("second")).not.toHaveProperty("mood");
    expect(await statusAfter("third")).not.toHaveProperty("mood");

    p.send({ type: "mood", mood: "happy" });
    await c.awaitMessage("mood");
    expect(await statusAfter("fourth")).not.toHaveProperty("mood");
    expect(await statusAfter("fifth")).not.toHaveProperty("mood");
    expect(t.server.internals.rooms.get(roomId)?.mood?.value).toBe("happy");

    await hook(t, token, hookInput("PostToolUseFailure", bash("sixth")));
    expect(await c.awaitMessage((m) => m.type === "status" && m.msg === "Bash failed")).not.toHaveProperty("mood");

    for (const mood of ["idle", "sleeping"]) {
      p.send({ type: "mood", mood });
      await c.awaitMessage("mood");
      expect(await statusAfter(`after ${mood}`)).toMatchObject({ mood: "working" });
      expect(await statusAfter(`again after ${mood}`)).not.toHaveProperty("mood");
    }

    await hook(t, token, hookInput("Stop"));
    expect(await c.awaitMessage("notify")).toMatchObject({ mood: "sleeping" });
    expect(await statusAfter("after stop")).toMatchObject({ mood: "working" });

    await t.fetch("/api/notify", { method: "POST", json: { event: "tests_failed" }, token });
    expect(await c.awaitMessage("notify")).toMatchObject({ mood: "sad" });
    expect(await statusAfter("after notify")).not.toHaveProperty("mood");
  });

  it("decides the mood when the coalesced status is sent", async () => {
    const { t, token, c } = await setup();
    const p = await track(producer(t, token));
    await hook(t, token, hookInput("UserPromptSubmit"));
    await hook(t, token, hookInput("PostToolUse", bash("one")));
    expect(await c.awaitMessage((m) => m.msg === "one")).toMatchObject({ mood: "working" });
    await hook(t, token, hookInput("PostToolUse", bash("two")));
    p.send({ type: "mood", mood: "sleeping" });
    await c.awaitMessage("mood");
    expect(await c.awaitMessage((m) => m.msg === "two")).toMatchObject({ mood: "working" });
  });
});

describe("dialog diagnosis", () => {
  const prompt = () => hookInput("Notification", { notification_type: "permission_prompt" });

  it("stays silent and counts when a dialog appears while a request of the session is pending", async () => {
    const { t, token, c } = await setup();
    expect((await roomState(t, token)).diagnostics).toEqual({ dialogWhilePending: { count: 0, lastSeenAt: null } });
    const held = await requestPermission(t, token, c);
    hold(c, held.id);

    await hook(t, token, prompt());
    await hook(t, token, prompt());
    await c.expectNoMessage("notify", 100);
    const { diagnostics } = await roomState(t, token);
    expect(diagnostics.dialogWhilePending.count).toBe(2);
    expect(diagnostics.dialogWhilePending.lastSeenAt).toMatch(/^\d{4}-\d{2}-\d{2}T[\d:.]+Z$/);

    // Another session's dialog is not evidence about this request.
    await hook(t, token, hookInput("Notification", { notification_type: "permission_prompt", session_id: "session-2" }));
    expect(await c.awaitMessage("notify")).toMatchObject({ detail: "Permission needed in terminal", sid: sidOf("session-2") });
    expect((await roomState(t, token)).diagnostics.dialogWhilePending.count).toBe(2);

    c.send({ type: "permission_response", id: held.id, granted: true });
    expect(await held.reply).toEqual(ALLOW);
  });

  it("does not count while a dialog of an earlier neutral reply may be open", async () => {
    const { t, token, c } = await setup({ timing: { ackMs: 80 } });
    const count = async () => (await roomState(t, token)).diagnostics.dialogWhilePending.count;
    const session = t.server.internals.rooms.getOrCreate((await roomState(t, token)).roomId).sessions;

    // The first call went back to the terminal (no_ack); its dialog may be open.
    expect(await (await requestPermission(t, token, c)).reply).toEqual({});
    expect(session.get(SID)?.dialogMaybeOpen).toBe(true);
    const second = await requestPermission(t, token, c, bash("second"));
    hold(c, second.id);
    await hook(t, token, prompt());
    await c.expectNoMessage("notify", 60);
    expect(await count()).toBe(0);

    // The first call ran; from now on a dialog can only belong to the pending call.
    await hook(t, token, hookInput("PostToolUse", bash("rm -rf dist")));
    expect(session.get(SID)?.dialogMaybeOpen).toBe(false);
    await hook(t, token, prompt());
    expect(await count()).toBe(1);

    c.send({ type: "permission_defer", id: second.id });
    expect(await second.reply).toEqual({});
    expect(session.get(SID)?.dialogMaybeOpen).toBe(true);
  });

  it("sets the flag on every neutral reply and clears it on each turn event", async () => {
    const { t, token, roomId } = await setup();
    const flag = () => t.server.internals.rooms.get(roomId)?.sessions.get(SID)?.dialogMaybeOpen;
    const neutral = async () => {
      expect(await hook(t, token, permissionInput({ tool_name: "ExitPlanMode" }))).toEqual({});
      expect(flag()).toBe(true);
    };
    for (const event of ["PostToolUse", "PostToolUseFailure", "PostToolBatch", "UserPromptSubmit", "Stop", "StopFailure"]) {
      await neutral();
      await hook(t, token, hookInput(event, bash("x")));
      expect(flag()).toBe(false);
    }
    await neutral();
    await hook(t, token, hookInput("Notification", { notification_type: "idle_prompt" }));
    expect(flag()).toBe(true);
  });

  it("leaves the flag clear when the cancelling event itself resolved the request", async () => {
    const { t, token, roomId, c } = await setup();
    const flag = () => t.server.internals.rooms.get(roomId)?.sessions.get(SID)?.dialogMaybeOpen;
    const held = await requestPermission(t, token, c, bash("make"));
    await hook(t, token, hookInput("PostToolUse", bash("make")));
    expect(await held.reply).toEqual({});
    await delay(20);
    expect(flag()).toBe(false);

    const answered = await requestPermission(t, token, c, bash("make"));
    c.send({ type: "permission_response", id: answered.id, granted: false });
    await answered.reply;
    expect(flag()).toBe(false);
  });
});
