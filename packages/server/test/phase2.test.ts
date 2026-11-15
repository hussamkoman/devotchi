// Protocol 14 on the server: caps, per-consumer frames, decide, questions,
// plans, extend, enforcement of what a device may answer, full text for
// dashboards, time and the feature switches.

import { afterEach, describe, expect, it } from "vitest";
import { MAX_FRAME_BYTES, utf8ByteLength } from "@devotchi/shared";
import { TestSocket, consumer, startTestServer, type Json, type StartOptions, type TestServer } from "./helpers.js";
import { ALLOW, hold, hook, hookInput, permissionInput, postHook, roomState, sidOf, until } from "./hook-helpers.js";

let ts: TestServer | undefined;
const sockets: TestSocket[] = [];

const PATIENT = { ackMs: 5000, aliveMs: 5000, holdMs: 8000 };
const ALL_CAPS = ["decide", "ask", "plan"] as const;

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
  for (const socket of sockets.splice(0)) socket.close();
  await ts?.close();
  ts = undefined;
});

type Kind = "sim" | "dashboard";

async function setup(options: StartOptions = {}) {
  const t = await start(options);
  const { token, roomId } = await t.mintToken();
  const join = (caps: readonly string[] | undefined, kind: Kind = "sim", name = "a") =>
    track(
      consumer(t, token, {
        kind,
        ...(kind === "sim" ? { deviceId: `sim-${name}` } : {}),
        ...(caps === undefined ? {} : { caps: [...caps] as never }),
      }),
    );
  return { t, token, roomId, join };
}

const QUESTION = {
  question: "Which framework should the dashboard use?",
  header: "Framework",
  options: [
    { label: "React", description: "Component library" },
    { label: "Vue", description: "Progressive framework" },
  ],
  multiSelect: false,
};
const MULTI = { question: "Which sections?", options: [{ label: "A" }, { label: "B" }, { label: "C" }], multiSelect: true };

const questionInput = (questions: unknown[] = [QUESTION], extra: Json = {}) =>
  hookInput("PermissionRequest", { tool_name: "AskUserQuestion", tool_input: { questions, ...extra } });
const planInput = (plan: string) => hookInput("PermissionRequest", { tool_name: "ExitPlanMode", tool_input: { plan } });
const COMPLETE_PLAN = "# Add pet packs\n\n- Define the pack format\n- Add the pets partition\n- Port Devo";
const CUT_PLAN = `${COMPLETE_PLAN}\n\nSome prose the device does not show.`;
const LONG_PLAN = `# Many steps\n\n${Array.from({ length: 13 }, (_, i) => `- Step ${i + 1}`).join("\n")}`;

function suggestion(ruleContent: unknown): Json {
  return {
    permission_suggestions: [
      { type: "addRules", rules: [{ toolName: "Bash", ruleContent }], behavior: "allow", destination: "localSettings" },
    ],
  };
}

async function pending(t: TestServer, token: string): Promise<Json[]> {
  return (await roomState(t, token)).pending as unknown as Json[];
}

describe("caps and register_ack", () => {
  it("lists the server's caps; a switched-off feature is missing", async () => {
    const { join } = await setup({ features: { questions: false, sessionGrants: false } });
    const c = await join(ALL_CAPS);
    const ack = c.received.find((m) => m.type === "register_ack")!;
    expect(ack.caps).toEqual(["decide", "plan"]);
    expect(typeof ack.utcOffsetMin).toBe("number");
  });

  it("utcOffsetMin follows the room's time zone", async () => {
    const { t, token, join } = await setup();
    const res = await t.fetch("/api/room/settings", { method: "PUT", token, json: { timeZone: "Asia/Kolkata" } });
    expect(await res.json()).toMatchObject({ timeZone: "Asia/Kolkata" });
    const c = await join(ALL_CAPS);
    expect(c.received.find((m) => m.type === "register_ack")).toMatchObject({ utcOffsetMin: 330 });
  });
});

describe("permission frames per consumer", () => {
  it("a phase-1 consumer gets details, a decide consumer gets the card fields; replays keep the shape", async () => {
    const { t, token, join } = await setup();
    const old = await join(undefined, "sim", "old");
    const now = await join(ALL_CAPS, "sim", "new");
    const reply = hook(t, token, permissionInput({ tool_input: { command: "git push --force", description: "Push it" } }));
    const p1 = await old.awaitMessage("permission");
    const p2 = await now.awaitMessage("permission");
    expect(p1.details).toBeDefined();
    expect(p1.cmd).toBeUndefined();
    expect(p2).toMatchObject({ cmd: "git push --force", desc: "Push it", risk: "high", reason: "Force push: rewrites the remote branch" });
    expect(p2.details).toBeUndefined();

    const late = await join(ALL_CAPS, "sim", "late");
    const replay = await late.awaitMessage("permission");
    expect(replay).toMatchObject({ id: p2.id, cmd: "git push --force" });
    expect(replay.details).toBeUndefined();
    old.send({ type: "permission_response", id: p1.id, granted: true });
    expect(await reply).toEqual(ALLOW);
  });

  it("offers a grant only from a usable suggestion, and allow_session adds exactly that rule", async () => {
    const { t, token, join } = await setup();
    const c = await join(ALL_CAPS);
    const reply = hook(t, token, permissionInput({ tool_input: { command: "pnpm test --run" }, ...suggestion("pnpm test:*") }));
    const p = await c.awaitMessage("permission");
    expect(p.grant).toEqual({ rule: "pnpm test", match: "prefix" });
    c.send({ type: "decide", id: p.id, choice: "allow_session" });
    expect(await reply).toEqual({
      hookSpecificOutput: {
        hookEventName: "PermissionRequest",
        decision: {
          behavior: "allow",
          updatedPermissions: [
            { type: "addRules", rules: [{ toolName: "Bash", ruleContent: "pnpm test:*" }], behavior: "allow", destination: "session" },
          ],
        },
      },
    });
    expect((await roomState(t, token)).lastRequest).toMatchObject({ outcome: "allow", choice: "allow_session", by: "sim-a" });
  });

  it("allow_session without a grant is INVALID_MESSAGE and the request stays pending", async () => {
    const { t, token, join } = await setup();
    const c = await join(ALL_CAPS);
    const reply = hook(t, token, permissionInput({ tool_input: { command: "pnpm test" } }));
    const p = await c.awaitMessage("permission");
    expect(p.grant).toBeUndefined();
    c.send({ type: "decide", id: p.id, choice: "allow_session" });
    expect(await c.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE", id: p.id });
    expect((await pending(t, token)).map((r) => r.id)).toEqual([p.id]);
    c.send({ type: "decide", id: p.id, choice: "deny_ask_why" });
    expect(await reply).toMatchObject({ hookSpecificOutput: { decision: { behavior: "deny" } } });
    expect(await c.awaitMessage("dismiss")).toMatchObject({ outcome: "deny", choice: "deny_ask_why" });
  });

  it("session grants switched off: no grant", async () => {
    const { t, token, join } = await setup({ features: { sessionGrants: false } });
    const c = await join(ALL_CAPS);
    void hook(t, token, permissionInput({ tool_input: { command: "pnpm test" }, ...suggestion("pnpm test") }));
    expect((await c.awaitMessage("permission")).grant).toBeUndefined();
  });

  it("plan choices do not answer a permission; a decide from a consumer without decide is NOT_ALLOWED", async () => {
    const { t, token, join } = await setup();
    const c = await join(ALL_CAPS);
    const old = await join(undefined, "sim", "old");
    void hook(t, token, permissionInput());
    const p = await c.awaitMessage("permission");
    c.send({ type: "decide", id: p.id, choice: "approve" });
    expect(await c.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE", id: p.id });
    old.send({ type: "decide", id: p.id, choice: "deny_stop" });
    expect(await old.awaitMessage("error")).toMatchObject({ code: "NOT_ALLOWED", id: p.id });
    expect(await pending(t, token)).toHaveLength(1);
  });
});

describe("cut permissions", () => {
  const long = { tool_input: { command: `echo ${"a".repeat(700)}` }, ...suggestion(`echo ${"a".repeat(700)}`) };

  it("a decide sim's allow is NOT_ALLOWED and the request stays pending", async () => {
    const { t, token, join } = await setup();
    const c = await join(ALL_CAPS);
    const reply = hook(t, token, permissionInput(long));
    const p = await c.awaitMessage("permission");
    expect(p.cut).toBe(true);
    expect(p.grant).toBeUndefined();
    c.send({ type: "permission_response", id: p.id, granted: true });
    expect(await c.awaitMessage("error")).toMatchObject({ code: "NOT_ALLOWED", id: p.id });
    c.send({ type: "decide", id: p.id, choice: "allow_session" });
    expect(await c.awaitMessage("error")).toMatchObject({ code: "NOT_ALLOWED", id: p.id });
    expect(await pending(t, token)).toHaveLength(1);
    c.send({ type: "permission_response", id: p.id, granted: false });
    expect(await reply).toMatchObject({ hookSpecificOutput: { decision: { behavior: "deny" } } });
  });

  it("the same request is allowed by a phase-1 sim", async () => {
    const { t, token, join } = await setup();
    await join(ALL_CAPS, "sim", "new");
    const old = await join(undefined, "sim", "old");
    const reply = hook(t, token, permissionInput(long));
    const p = await old.awaitMessage("permission");
    expect(p.cut).toBeUndefined();
    old.send({ type: "permission_response", id: p.id, granted: true });
    expect(await reply).toEqual(ALLOW);
  });

  it("the same request is allowed by a dashboard", async () => {
    const { t, token, join } = await setup();
    await join(ALL_CAPS, "sim", "new");
    const dash = await join(["decide"], "dashboard");
    const reply = hook(t, token, permissionInput(long));
    const p = await dash.awaitMessage("permission");
    expect(p.cut).toBe(true);
    dash.send({ type: "permission_response", id: p.id, granted: true });
    expect(await reply).toEqual(ALLOW);
  });
});

describe("questions", () => {
  it("go only to a consumer with ask; the answer is built from the original input", async () => {
    const { t, token, join } = await setup();
    const old = await join(undefined, "sim", "old");
    const c = await join(ALL_CAPS);
    const reply = hook(t, token, questionInput([QUESTION, MULTI], { extra: { kept: 1 } }));
    const q0 = await c.awaitMessage("question");
    const q1 = await c.awaitMessage("question");
    expect(q0).toMatchObject({
      index: 0,
      count: 2,
      header: "Framework",
      text: QUESTION.question,
      multi: false,
      options: [
        { label: "React", desc: "Component library" },
        { label: "Vue", desc: "Progressive framework" },
      ],
      sid: sidOf("session-1"),
      timeout: 8000,
    });
    expect(q1).toMatchObject({ index: 1, count: 2, multi: true });
    expect(q1.header).toBeUndefined();
    await old.expectNoMessage("question");
    expect((await pending(t, token))[0]).toMatchObject({ kind: "question", tool: "AskUserQuestion" });

    hold(c, q0.id as string);
    // Extra fields (labels, strings) are ignored: only the indexes count.
    c.send({ type: "question_response", id: q0.id, answers: [[1], [2, 0]], labels: ["Evil"], answersText: { x: "y" } } as Json);
    expect(await reply).toEqual({
      hookSpecificOutput: {
        hookEventName: "PermissionRequest",
        decision: {
          behavior: "allow",
          updatedInput: {
            questions: [QUESTION, MULTI],
            extra: { kept: 1 },
            answers: { [QUESTION.question]: "Vue", "Which sections?": "A, C" },
          },
        },
      },
    });
    expect(await old.awaitMessage("dismiss")).toMatchObject({ reason: "answered", outcome: "allow", choice: "answered" });
    expect((await roomState(t, token)).lastRequest).toMatchObject({ outcome: "allow", choice: "answered" });
  });

  it("with only a phase-1 consumer the hook is neutral at once", async () => {
    const { t, token, join } = await setup();
    await join(undefined);
    expect(await hook(t, token, questionInput())).toEqual({});
    expect((await roomState(t, token)).lastRequest).toMatchObject({ outcome: "neutral", detail: "skipped_tool" });
  });

  it("questions switched off: neutral at once, even with an ask consumer", async () => {
    const { t, token, join } = await setup({ features: { questions: false } });
    await join(ALL_CAPS);
    expect(await hook(t, token, questionInput())).toEqual({});
    expect((await roomState(t, token)).lastRequest).toMatchObject({ detail: "skipped_tool" });
  });

  it.each([
    ["5 options", [{ ...QUESTION, options: ["a", "b", "c", "d", "e"].map((label) => ({ label })) }]],
    ["duplicate labels", [{ ...QUESTION, options: [{ label: "A" }, { label: "A" }] }]],
    ["a 500-byte question", [{ ...QUESTION, question: `${"q".repeat(499)}?` }]],
    ["CJK labels", [{ ...QUESTION, options: [{ label: "反应框架" }, { label: "视图框架" }] }]],
    ["no questions", []],
  ])("not eligible: %s -> neutral skipped_tool", async (_name, questions) => {
    const { t, token, join } = await setup();
    const c = await join(ALL_CAPS);
    expect(await hook(t, token, questionInput(questions))).toEqual({});
    expect((await roomState(t, token)).lastRequest).toMatchObject({ outcome: "neutral", detail: "skipped_tool" });
    await c.expectNoMessage("question");
  });

  it("eligible without a header, with a 60-byte header and with a line feed in the text", async () => {
    const { t, token, join } = await setup();
    const c = await join(ALL_CAPS);
    const questions = [
      { question: "No header?", options: [{ label: "A" }, { label: "B" }] },
      { question: "Long header?", header: "h".repeat(60), options: [{ label: "A" }, { label: "B" }] },
      { question: "Two\nlines?", header: "", options: [{ label: "A" }, { label: "B" }] },
    ];
    void hook(t, token, questionInput(questions));
    const frames = [await c.awaitMessage("question"), await c.awaitMessage("question"), await c.awaitMessage("question")];
    expect(frames[0]!.header).toBeUndefined();
    expect(frames[1]!.header).toBe(`${"h".repeat(21)}…`);
    expect(frames[2]).toMatchObject({ text: "Two lines?" });
    expect(frames[2]!.header).toBeUndefined();
  });

  it("invalid answers keep the request pending", async () => {
    const { t, token, join } = await setup();
    const c = await join(ALL_CAPS);
    const reply = hook(t, token, questionInput([QUESTION, MULTI]));
    const q = await c.awaitMessage("question");
    const id = q.id as string;
    const bad: unknown[] = [
      [[0]],
      [[0], [0], [0]],
      [[0, 1], [0]],
      [[], [0]],
      [[2], [0]],
      [[0], [0, 0]],
      [[0], [3]],
      [["React"], [0]],
      "React",
    ];
    for (const answers of bad) {
      c.send({ type: "question_response", id, answers } as Json);
      expect(await c.awaitMessage("error"), JSON.stringify(answers)).toMatchObject({ code: "INVALID_MESSAGE", id });
    }
    c.send({ type: "permission_response", id, granted: true });
    expect(await c.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE", id });
    c.send({ type: "decide", id, choice: "approve" });
    expect(await c.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE", id });
    expect(await pending(t, token)).toHaveLength(1);
    c.send({ type: "decide", id, choice: "decline" });
    expect(await reply).toMatchObject({ hookSpecificOutput: { decision: { behavior: "deny" } } });
  });

  it("a question_response from a consumer without ask is NOT_ALLOWED", async () => {
    const { t, token, join } = await setup();
    const c = await join(ALL_CAPS);
    const old = await join(["decide"], "sim", "old");
    void hook(t, token, questionInput());
    const q = await c.awaitMessage("question");
    old.send({ type: "question_response", id: q.id, answers: [[0]] });
    expect(await old.awaitMessage("error")).toMatchObject({ code: "NOT_ALLOWED", id: q.id });
    expect(await pending(t, token)).toHaveLength(1);
  });

  it("permission_defer is neutral; replay resends every frame with the remaining hold", async () => {
    const { t, token, join } = await setup();
    const c = await join(ALL_CAPS);
    const reply = hook(t, token, questionInput([QUESTION, MULTI]));
    await c.awaitMessage("question");
    const late = await join(ALL_CAPS, "sim", "late");
    const r0 = await late.awaitMessage("question");
    const r1 = await late.awaitMessage("question");
    expect([r0.index, r1.index]).toEqual([0, 1]);
    expect(r0.timeout as number).toBeLessThanOrEqual(8000);
    late.send({ type: "permission_defer", id: r0.id });
    expect(await reply).toEqual({});
    expect((await roomState(t, token)).lastRequest).toMatchObject({ detail: "deferred" });
  });

  it("PostToolUse with the answers added cancels it", async () => {
    const { t, token, join } = await setup();
    const c = await join(ALL_CAPS);
    const reply = hook(t, token, questionInput());
    const q = await c.awaitMessage("question");
    await hook(
      t,
      token,
      hookInput("PostToolUse", {
        tool_name: "AskUserQuestion",
        tool_input: { questions: [QUESTION], answers: { [QUESTION.question]: "React" } },
      }),
    );
    expect(await reply).toEqual({});
    expect(await c.awaitMessage("dismiss")).toMatchObject({ id: q.id, reason: "cancelled" });
  });
});

describe("plans", () => {
  async function planWith(plan: string, caps: readonly string[] = ALL_CAPS, kind: Kind = "sim") {
    const ctx = await setup();
    const c = await ctx.join(caps, kind);
    const reply = hook(ctx.t, ctx.token, planInput(plan));
    const frame = await c.awaitMessage("plan");
    return { ...ctx, c, reply, frame, id: frame.id as string };
  }

  it("a complete plan: frame shape, approve_accept_edits allowed", async () => {
    const { c, reply, frame, id, t, token } = await planWith(COMPLETE_PLAN);
    expect(frame).toMatchObject({
      title: "Add pet packs",
      steps: ["Define the pack format", "Add the pets partition", "Port Devo"],
      more: 0,
      timeout: 8000,
    });
    expect(frame.cut).toBeUndefined();
    expect((await pending(t, token))[0]).toMatchObject({ kind: "plan" });
    c.send({ type: "decide", id, choice: "approve_accept_edits" });
    expect(await reply).toEqual({
      hookSpecificOutput: {
        hookEventName: "PermissionRequest",
        decision: {
          behavior: "allow",
          updatedInput: { plan: COMPLETE_PLAN },
          updatedPermissions: [{ type: "setMode", mode: "acceptEdits", destination: "session" }],
        },
      },
    });
  });

  it("a cut plan: approve is allowed with mode default, approve_accept_edits is NOT_ALLOWED", async () => {
    const { c, reply, frame, id, t, token } = await planWith(CUT_PLAN);
    expect(frame).toMatchObject({ cut: true, more: 0 });
    c.send({ type: "decide", id, choice: "approve_accept_edits" });
    expect(await c.awaitMessage("error")).toMatchObject({ code: "NOT_ALLOWED", id });
    expect(await pending(t, token)).toHaveLength(1);
    c.send({ type: "decide", id, choice: "approve" });
    expect(await reply).toMatchObject({
      hookSpecificOutput: {
        decision: { updatedInput: { plan: CUT_PLAN }, updatedPermissions: [{ type: "setMode", mode: "default", destination: "session" }] },
      },
    });
  });

  it("more > 0: approve is NOT_ALLOWED on a sim, revise still works", async () => {
    const { c, reply, frame, id, t, token } = await planWith(LONG_PLAN);
    expect(frame).toMatchObject({ more: 1, cut: true });
    c.send({ type: "decide", id, choice: "approve" });
    expect(await c.awaitMessage("error")).toMatchObject({ code: "NOT_ALLOWED", id });
    expect(await pending(t, token)).toHaveLength(1);
    c.send({ type: "decide", id, choice: "revise" });
    expect(await reply).toMatchObject({ hookSpecificOutput: { decision: { behavior: "deny" } } });
    expect((await roomState(t, token)).lastRequest).toMatchObject({ outcome: "deny", choice: "revise" });
  });

  it("dashboards are not restricted", async () => {
    const { c, reply, id } = await planWith(LONG_PLAN, ["decide", "plan"], "dashboard");
    c.send({ type: "decide", id, choice: "approve_accept_edits" });
    expect(await reply).toMatchObject({ hookSpecificOutput: { decision: { behavior: "allow" } } });
  });

  it("permission_response and question choices do not answer a plan", async () => {
    const { c, id, t, token } = await planWith(COMPLETE_PLAN);
    c.send({ type: "permission_response", id, granted: true });
    expect(await c.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE", id });
    c.send({ type: "decide", id, choice: "decline" });
    expect(await c.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE", id });
    c.send({ type: "decide", id, choice: "deny_ask_why" });
    expect(await c.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE", id });
    expect(await pending(t, token)).toHaveLength(1);
  });

  it("deny_stop interrupts", async () => {
    const { c, reply, id } = await planWith(COMPLETE_PLAN);
    c.send({ type: "decide", id, choice: "deny_stop" });
    expect(await reply).toMatchObject({ hookSpecificOutput: { decision: { behavior: "deny", interrupt: true } } });
    expect(await c.awaitMessage("dismiss")).toMatchObject({ outcome: "deny", choice: "deny_stop" });
  });

  it("any PostToolUseFailure of ExitPlanMode in the session cancels it", async () => {
    const { c, reply, id, t, token } = await planWith(COMPLETE_PLAN);
    await hook(t, token, hookInput("PostToolUseFailure", { tool_name: "ExitPlanMode", tool_input: { plan: "other" } }));
    expect(await reply).toEqual({});
    expect(await c.awaitMessage("dismiss")).toMatchObject({ id, reason: "cancelled" });
  });

  it("plans switched off or no plan consumer: neutral at once", async () => {
    const off = await setup({ features: { plans: false } });
    await off.join(ALL_CAPS);
    expect(await hook(off.t, off.token, planInput(COMPLETE_PLAN))).toEqual({});
    for (const socket of sockets.splice(0)) socket.close();
    await off.t.close();
    const none = await setup();
    await none.join(["decide", "ask"]);
    expect(await hook(none.t, none.token, planInput(COMPLETE_PLAN))).toEqual({});
    expect(await hook(none.t, none.token, planInput("   "))).toEqual({});
  });

  it("a plan of quotes and backslashes fits in 2048 bytes", async () => {
    const plan = `# ${'"'.repeat(80)}\n${Array.from({ length: 12 }, () => `- ${"\\".repeat(120)}`).join("\n")}`;
    const { frame, c } = await planWith(plan);
    expect(utf8ByteLength(JSON.stringify(frame))).toBeLessThanOrEqual(MAX_FRAME_BYTES);
    expect(frame.cut).toBe(true);
    expect(frame.more as number).toBeGreaterThan(0);
    c.close();
  });
});

describe("neutral outcomes of questions and plans", () => {
  it("timeout, holder lost and abort are neutral", async () => {
    const { t, token, join } = await setup({ timing: { holdMs: 600, aliveMs: 400 } });
    const c = await join(ALL_CAPS);

    const timedOut = hook(t, token, planInput(COMPLETE_PLAN));
    const plan = await c.awaitMessage("plan");
    hold(c, plan.id as string);
    const beat = setInterval(() => hold(c, plan.id as string), 100);
    expect(await timedOut).toEqual({});
    clearInterval(beat);
    expect((await roomState(t, token)).lastRequest).toMatchObject({ detail: "timeout" });

    const lost = hook(t, token, questionInput());
    const q = await c.awaitMessage("question");
    hold(c, q.id as string);
    expect(await lost).toEqual({});
    expect((await roomState(t, token)).lastRequest).toMatchObject({ detail: "holder_lost" });

    const controller = new AbortController();
    const aborted = postHook(t, token, questionInput(), { signal: controller.signal }).catch(() => "aborted");
    const q2 = await c.awaitMessage("question");
    controller.abort();
    expect(await aborted).toBe("aborted");
    expect(await c.awaitMessage((m) => m.type === "dismiss" && m.id === q2.id)).toMatchObject({ reason: "cancelled" });
    expect((await roomState(t, token)).lastRequest).toMatchObject({ outcome: "neutral", detail: "cancelled" });
  });
});

describe("extend", () => {
  it("moves the deadline to now + 60 s, sends deadline to consumers that got it, caps at 300 s", async () => {
    const { t, token, join } = await setup({ timing: { holdMs: 10_000 } });
    const c = await join(ALL_CAPS);
    const old = await join(undefined, "sim", "old");
    const reply = hook(t, token, questionInput());
    const q = await c.awaitMessage("question");
    const id = q.id as string;
    hold(c, id);
    c.send({ type: "extend", id });
    const first = await c.awaitMessage("deadline");
    expect(first.timeout as number).toBeGreaterThan(59_000);
    expect(first.timeout as number).toBeLessThanOrEqual(60_000);
    await old.expectNoMessage("deadline");

    // A second extend right away moves the deadline by less than 1 s: no deadline.
    c.send({ type: "extend", id });
    await c.expectNoMessage("deadline");

    const permissions = t.server.internals.permissions as unknown as { now: () => number };
    const realNow = permissions.now;
    permissions.now = () => realNow() + 250_000;
    hold(c, id);
    c.send({ type: "extend", id });
    const capped = await c.awaitMessage("deadline");
    expect(capped.timeout as number).toBeGreaterThan(49_000);
    expect(capped.timeout as number).toBeLessThanOrEqual(50_000);
    expect((await pending(t, token))[0]!.remainingMs as number).toBeLessThanOrEqual(50_000);
    permissions.now = realNow;
    c.send({ type: "decide", id, choice: "decline" });
    await reply;
  });

  it("is ignored for a permission and an unknown id", async () => {
    const { t, token, join } = await setup();
    const c = await join(ALL_CAPS);
    void hook(t, token, permissionInput());
    const p = await c.awaitMessage("permission");
    c.send({ type: "extend", id: p.id });
    c.send({ type: "extend", id: "0".repeat(32) });
    await c.expectNoMessage((m) => m.type === "deadline" || m.type === "error");
  });
});

describe("GET /api/room/requests/:id", () => {
  it("returns the full tool_input of a pending request, otherwise 404", async () => {
    const { t, token, join } = await setup();
    const c = await join(ALL_CAPS);
    void hook(t, token, planInput(CUT_PLAN));
    const plan = await c.awaitMessage("plan");
    const res = await t.fetch(`/api/room/requests/${plan.id as string}`, { token });
    expect(res.status).toBe(200);
    const body = (await res.json()) as Json;
    expect(body).toMatchObject({ id: plan.id, kind: "plan", tool: "ExitPlanMode", toolInput: { plan: CUT_PLAN } });
    expect(body.remainingMs as number).toBeGreaterThan(0);

    for (const id of ["0".repeat(32), "nope"]) {
      const missing = await t.fetch(`/api/room/requests/${id}`, { token });
      expect(missing.status).toBe(404);
      expect(await missing.json()).toMatchObject({ code: "not_found" });
    }
    const other = await t.mintToken();
    expect((await t.fetch(`/api/room/requests/${plan.id as string}`, { token: other.token })).status).toBe(404);
  });
});

describe("time", () => {
  it("sends time to capable consumers when the zone changes and hourly", async () => {
    const { t, token, join } = await setup();
    const c = await join(ALL_CAPS);
    const old = await join(undefined, "sim", "old");
    const res = await t.fetch("/api/room/settings", { method: "PUT", token, json: { timeZone: "Asia/Kolkata" } });
    expect(res.status).toBe(200);
    const msg = await c.awaitMessage("time");
    expect(msg).toMatchObject({ utcOffsetMin: 330 });
    expect(Math.abs((msg.serverTime as number) - Date.now())).toBeLessThan(5000);
    await old.expectNoMessage("time");

    const time = t.server.internals.time;
    const now = Date.now();
    time.tick(now);
    await c.expectNoMessage("time");
    time.tick(now + 60 * 60_000);
    expect(await c.awaitMessage("time")).toMatchObject({ serverTime: now + 60 * 60_000, utcOffsetMin: 330 });
  });

  it("rejects a zone the runtime does not know; \"\" is the process zone", async () => {
    const { t, token } = await setup();
    const bad = await t.fetch("/api/room/settings", { method: "PUT", token, json: { timeZone: "Mars/Olympus" } });
    expect(bad.status).toBe(400);
    const notString = await t.fetch("/api/room/settings", { method: "PUT", token, json: { timeZone: 5 } });
    expect(notString.status).toBe(400);
    const empty = await t.fetch("/api/room/settings", { method: "PUT", token, json: { timeZone: "" } });
    expect(await empty.json()).toMatchObject({ timeZone: "" });
  });
});

describe("telemetry.pet", () => {
  it("is stored with the device's telemetry and returned by GET /api/devices", async () => {
    const t = await start({ timing: { deviceWriteMs: 0 } });
    const { token, roomId } = await t.mintToken();
    const device = t.deviceToken(roomId, "dv-0a1b2c");
    const c = await track(consumer(t, device.token, { kind: "device", deviceId: device.deviceId, caps: ["decide"] as never }));
    c.send({
      type: "telemetry",
      battery: { state: "absent", pct: null, mv: 4100 },
      power: "usb",
      rssi: -50,
      uptimeS: 3,
      heapFree: 1,
      heapMin: 1,
      resetReason: "poweron",
      fw: "0.2.0",
      pet: "devo",
    });
    await until(async () => {
      const res = await t.fetch("/api/devices", { token });
      const devices = ((await res.json()) as { devices: Json[] }).devices;
      return (devices[0]?.telemetry as Json | null)?.pet === "devo";
    });
  });
});
