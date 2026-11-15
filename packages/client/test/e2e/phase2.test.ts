// Questions, plans and richer decisions end to end: real server, hook POSTs
// and `devotchi sim` (in-process and as a child process), one round trip per
// outcome of protocol 14.3 and 14.4.

import { afterEach, describe, expect, it } from "vitest";
import type { Json } from "../../../server/test/helpers.js";
import { ALLOW, hookInput } from "../../../server/test/hook-helpers.js";
import type { SimEvent } from "../../src/sim.js";
import { makeWorld, spawnSim, startSim, type SimHandle, type World } from "./helpers.js";
import { requireBuiltCli } from "../helpers.js";

let world: World | null = null;

afterEach(async () => {
  await world?.close();
  world = null;
});

const QUESTION = {
  question: "Which framework?",
  header: "Framework",
  options: [{ label: "React", description: "Component library" }, { label: "Vue" }, { label: "Svelte" }],
};
const MULTI = { question: "Which sections?", options: [{ label: "A" }, { label: "B" }], multiSelect: true };
const PLAN = "# Add pet packs\n\n- Define the pack format\n- Port Devo";
const LONG_PLAN = `# Many steps\n\n${Array.from({ length: 13 }, (_, i) => `- Step ${i + 1}`).join("\n")}`;

const questionHook = (questions: unknown[] = [QUESTION, MULTI]) =>
  hookInput("PermissionRequest", { tool_name: "AskUserQuestion", tool_input: { questions } });
const planHook = (plan = PLAN) => hookInput("PermissionRequest", { tool_name: "ExitPlanMode", tool_input: { plan } });

function decisionOf(reply: unknown): Json {
  return ((reply as { hookSpecificOutput?: { decision?: Json } }).hookSpecificOutput?.decision ?? {}) as Json;
}

const rxError = (code: string) => (e: SimEvent) => (e.msg as Json | undefined)?.type === "error" && (e.msg as Json).code === code;

describe("questions with a sim", () => {
  it.each([
    ["first", { "Which framework?": "React", "Which sections?": "A" }],
    ["last", { "Which framework?": "Svelte", "Which sections?": "B" }],
    ["none", { "Which framework?": "No preference, use your judgment", "Which sections?": "None of these" }],
    ["null", { "Which framework?": "No preference, use your judgment", "Which sections?": "No preference, use your judgment" }],
  ] as const)("--answer %s", async (answer, answers) => {
    world = await makeWorld();
    const sim = await startSim(world, { answer });
    const reply = await world.hook(questionHook());
    expect(decisionOf(reply)).toEqual({ behavior: "allow", updatedInput: { questions: [QUESTION, MULTI], answers } });
    const request = await sim.next("request", (e) => e.kind === "question");
    expect(request.questions).toHaveLength(2);
    expect(await sim.next("dismiss")).toMatchObject({ outcome: "allow", choice: "answered" });
  });

  it("--answer decline denies with the decline message; --answer terminal is neutral", async () => {
    world = await makeWorld();
    const decline = await startSim(world, { name: "decline", answer: "decline" });
    expect(decisionOf(await world.hook(questionHook()))).toEqual({
      behavior: "deny",
      message: "The user declined to answer on Devotchi. Use your best judgment, or ask again later.",
    });
    await decline.sim.stop();
    await startSim(world, { name: "terminal", answer: "terminal" });
    expect(await world.hook(questionHook())).toEqual({});
  });

  it("a phase-1 sim next to a phase-2 one never sees the question; alone it makes the hook neutral at once", async () => {
    world = await makeWorld();
    const old = await startSim(world, { name: "old", caps: [], policy: { kind: "manual" } });
    const fresh = await startSim(world, { name: "new", answer: "first" });
    expect(decisionOf(await world.hook(questionHook())).behavior).toBe("allow");
    expect(fresh.events.some((e) => e.event === "request" && e.kind === "question")).toBe(true);
    expect(old.events.some((e) => (e.msg as Json | undefined)?.type === "question")).toBe(false);
    expect(old.events.find((e) => e.event === "registered")?.caps).toEqual(["decide", "ask", "plan"]);
    await fresh.sim.stop();

    const started = Date.now();
    expect(await world.hook(questionHook())).toEqual({});
    expect(Date.now() - started).toBeLessThan(1000);
  });

  it("--extend sends extend and the sim follows the new deadline", async () => {
    world = await makeWorld();
    const sim = await startSim(world, { extend: true, answer: "terminal", policy: { kind: "after", ms: 600 } });
    expect(await world.hook(questionHook())).toEqual({});
    expect(sim.sent("extend")).toHaveLength(1);
    const deadline = await sim.next("deadline");
    expect(deadline.timeout as number).toBeGreaterThan(59_000);
  });
});

describe("plans with a sim", () => {
  const mode = (reply: unknown) =>
    ((decisionOf(reply).updatedPermissions as Json[] | undefined)?.[0] as Json | undefined)?.mode;

  it("--plan approve / accept-edits allow with the original input and the mode", async () => {
    world = await makeWorld();
    const approve = await startSim(world, { name: "approve", plan: "approve" });
    const reply = await world.hook(planHook());
    expect(decisionOf(reply)).toMatchObject({ behavior: "allow", updatedInput: { plan: PLAN } });
    expect(mode(reply)).toBe("default");
    expect(await approve.next("request", (e) => e.kind === "plan")).toMatchObject({
      title: "Add pet packs",
      steps: ["Define the pack format", "Port Devo"],
      more: 0,
      cut: false,
    });
    await approve.sim.stop();

    await startSim(world, { name: "edits", plan: "accept-edits" });
    expect(mode(await world.hook(planHook()))).toBe("acceptEdits");
  });

  it("--plan revise, stop and terminal", async () => {
    world = await makeWorld();
    let sim = await startSim(world, { name: "revise", plan: "revise" });
    expect(decisionOf(await world.hook(planHook())).message).toMatch(/wants changes/);
    await sim.sim.stop();
    sim = await startSim(world, { name: "stop", plan: "stop" });
    expect(decisionOf(await world.hook(planHook()))).toMatchObject({ behavior: "deny", interrupt: true });
    await sim.sim.stop();
    await startSim(world, { name: "terminal", plan: "terminal" });
    expect(await world.hook(planHook())).toEqual({});
  });

  it("a plan with more > 0 cannot be approved by the sim; the hook ends neutral", async () => {
    world = await makeWorld({ timing: { holdMs: 1500 } });
    const sim = await startSim(world, { plan: "approve" });
    expect(await world.hook(planHook(LONG_PLAN))).toEqual({});
    await sim.next("rx", rxError("NOT_ALLOWED"));
  });
});

describe("permission decisions with a sim", () => {
  async function decided(sim: SimHandle, w: World, fields: Json = {}) {
    const reply = await w.permission(fields);
    await sim.next("dismiss");
    return reply;
  }

  it("--decide allow_session, deny_ask_why and deny_stop", async () => {
    world = await makeWorld();
    const suggestion = {
      tool_input: { command: "pnpm test" },
      permission_suggestions: [
        { type: "addRules", rules: [{ toolName: "Bash", ruleContent: "pnpm test" }], behavior: "allow", destination: "localSettings" },
      ],
    };
    let sim = await startSim(world, { name: "session", decide: "allow_session" });
    expect(decisionOf(await decided(sim, world, suggestion))).toEqual({
      behavior: "allow",
      updatedPermissions: [
        { type: "addRules", rules: [{ toolName: "Bash", ruleContent: "pnpm test" }], behavior: "allow", destination: "session" },
      ],
    });
    expect(await sim.next("request")).toMatchObject({ grant: { rule: "pnpm test", match: "exact" }, cmd: "pnpm test" });
    await sim.sim.stop();

    sim = await startSim(world, { name: "why", decide: "deny_ask_why" });
    expect(decisionOf(await decided(sim, world)).message).toMatch(/wants to know why/);
    await sim.sim.stop();
    sim = await startSim(world, { name: "stop", decide: "deny_stop" });
    expect(decisionOf(await decided(sim, world))).toMatchObject({ behavior: "deny", interrupt: true });
  });

  it("a cut command: the phase-2 sim's allow is NOT_ALLOWED, a phase-1 sim's allow decides", async () => {
    world = await makeWorld();
    const fresh = await startSim(world, { name: "new", policy: { kind: "all" } });
    const old = await startSim(world, { name: "old", caps: [], policy: { kind: "manual" } });
    const reply = world.permission({ tool_input: { command: `echo ${"a".repeat(700)}` } });
    await fresh.next("rx", rxError("NOT_ALLOWED"));
    const request = await old.next("request");
    expect(request.cut).toBeUndefined();
    expect(old.sim.approve(request.id as string)).toBe(true);
    expect(await reply).toEqual(ALLOW);
  });
});

describe("devotchi sim (child process)", () => {
  it("--answer last, --caps and manual stdin lines for questions and plans", async () => {
    requireBuiltCli();
    world = await makeWorld();
    const auto = spawnSim(world, ["--token", world.token, "--name", "auto", "--answer", "last", "--plan", "revise"]);
    await auto.next("registered", undefined, 8000);
    expect(decisionOf(await world.hook(questionHook([QUESTION]))).updatedInput).toMatchObject({
      answers: { "Which framework?": "Svelte" },
    });
    expect(decisionOf(await world.hook(planHook())).behavior).toBe("deny");
    auto.child.kill("SIGTERM");
    expect(await auto.exited).toBe(0);

    const manual = spawnSim(world, ["--token", world.token, "--name", "manual", "--approve", "manual"]);
    await manual.next("registered", undefined, 8000);
    const question = world.hook(questionHook());
    const shown = await manual.next("request", (e) => e.kind === "question");
    manual.child.stdin!.write(`answer 2;0,1 ${shown.id as string}\n`);
    expect(decisionOf(await question).updatedInput).toMatchObject({
      answers: { "Which framework?": "Svelte", "Which sections?": "A, B" },
    });
    const plan = world.hook(planHook());
    await manual.next("request", (e) => e.kind === "plan");
    manual.child.stdin!.write("plan accept-edits\n");
    expect(decisionOf(await plan)).toMatchObject({ behavior: "allow" });

    const old = spawnSim(world, ["--token", world.token, "--name", "old", "--caps", "none", "--approve", "manual"]);
    await old.next("registered", undefined, 8000);
    manual.child.kill("SIGTERM");
    await manual.exited;
    expect(await world.hook(planHook())).toEqual({});
    old.child.kill("SIGTERM");
    expect(await old.exited).toBe(0);
  });

  it("rejects an unknown --answer", async () => {
    requireBuiltCli();
    world = await makeWorld();
    const result = await world.cli(["sim", "--token", world.token, "--answer", "maybe"]);
    expect(result.code).not.toBe(0);
    expect(result.stderr).toMatch(/--answer must be one of/);
  });
});

describe("permission frames for a phase-2 sim", () => {
  it("carry cmd, desc and reason", async () => {
    world = await makeWorld();
    const sim = await startSim(world, { policy: { kind: "none" } });
    await world.permission({ tool_input: { command: "git push --force", description: "Ship" } });
    expect(await sim.next("request")).toMatchObject({
      cmd: "git push --force",
      desc: "Ship",
      reason: "Force push: rewrites the remote branch",
      risk: "high",
    });
  });
});
