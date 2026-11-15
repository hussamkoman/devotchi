import { describe, expect, it } from "vitest";
import type { QuestionMessage, ServerToConsumerMessage } from "@devotchi/shared";
import {
  EXTEND_BELOW_MS,
  EXTEND_GAP_MS,
  extendDue,
  initialState,
  isPresentable,
  needsFullText,
  reduce,
  type Action,
  type QuestionCard,
  type State,
} from "../../web/src/store.js";

const id = (n: number) => n.toString(16).padStart(32, "0");
const T0 = 1_800_000_000_000;

const frame = (msg: ServerToConsumerMessage, now = T0): Action => ({ type: "frame", msg, now });
const run = (state: State, ...actions: Action[]) => actions.reduce(reduce, state);
const open = () => run(initialState(), { type: "connection", phase: "open", now: T0 });

function question(index: number, count: number, extra: Partial<QuestionMessage> = {}): QuestionMessage {
  return {
    type: "question",
    id: id(1),
    index,
    count,
    text: `Question ${index}?`,
    multi: false,
    options: [{ label: "React", desc: "Component library" }, { label: "Vue" }, { label: "Svelte" }],
    timeout: 30_000,
    ...extra,
  };
}

const plan: ServerToConsumerMessage = { type: "plan", id: id(2), title: "Add pet packs", steps: ["Define", "Add", "Port"], more: 0, cut: true, timeout: 30_000 };

describe("store: questions", () => {
  it("collects the frames of one call and presents the card only once all arrived", () => {
    let s = run(open(), frame(question(0, 3)), frame(question(2, 3, { multi: true })));
    expect(s.requests).toHaveLength(1);
    const card = s.requests[0] as QuestionCard;
    expect(card).toMatchObject({ kind: "question", label: "Question", count: 3 });
    expect(isPresentable(card)).toBe(false);
    s = run(s, frame(question(1, 3), T0 + 50));
    const full = s.requests[0] as QuestionCard;
    expect(isPresentable(full)).toBe(true);
    expect(full.questions.map((q) => q?.index)).toEqual([0, 1, 2]);
    expect(full.deadline).toBe(T0 + 50 + 30_000);
    expect(needsFullText(full)).toBe(true);
  });

  it("ignores frames whose count or index does not fit the call, and a replay replaces a frame", () => {
    let s = run(open(), frame(question(0, 2)), frame(question(1, 3)), frame(question(5, 2)));
    expect((s.requests[0] as QuestionCard).questions).toEqual([expect.objectContaining({ index: 0 }), null]);
    s = run(s, frame(question(0, 2, { text: "again" })));
    expect((s.requests[0] as QuestionCard).questions[0]?.text).toBe("again");
  });

  it("a dashboard answer reads back the chosen labels in the receipt", () => {
    let s = run(open(), frame(question(0, 2)), frame(question(1, 2, { multi: true })));
    s = run(s, { type: "send", id: id(1), reply: { kind: "answers", answers: [[1], [0, 2]] }, now: T0 });
    s = run(s, frame({ type: "dismiss", id: id(1), reason: "answered", outcome: "allow", by: "dashboard", choice: "answered" }));
    expect(s.requests).toEqual([]);
    expect(s.toasts.at(-1)?.text).toBe("Question: answers sent: Vue; React, Svelte, answered on the dashboard");
  });
});

describe("store: plans, deadlines and choices", () => {
  it("keeps the plan frame and follows deadline messages", () => {
    let s = run(open(), frame(plan));
    expect(s.requests[0]).toMatchObject({ kind: "plan", label: "Plan", title: "Add pet packs", more: 0, cut: true, deadline: T0 + 30_000 });
    s = run(s, frame({ type: "deadline", id: id(2), timeout: 60_000 }, T0 + 10_000));
    expect(s.requests[0]?.deadline).toBe(T0 + 70_000);
    s = run(s, { type: "tick", now: T0 + 40_000 });
    expect(s.requests).toHaveLength(1);
  });

  it("writes dismiss.choice in words", () => {
    const perm = (n: number, extra = {}): ServerToConsumerMessage => ({ type: "permission", id: id(n), tool: "Bash", summary: "x", risk: "low", timeout: 30_000, ...extra });
    let s = run(
      open(),
      frame(perm(3, { cmd: "pnpm test", grant: { rule: "pnpm test", match: "prefix" } })),
      frame(perm(4)),
      frame(perm(5)),
      frame(plan),
    );
    s = run(
      s,
      frame({ type: "dismiss", id: id(3), reason: "answered", outcome: "allow", by: "dashboard", choice: "allow_session" }),
      frame({ type: "dismiss", id: id(4), reason: "answered", outcome: "deny", by: "dashboard", choice: "deny_ask_why" }),
      frame({ type: "dismiss", id: id(5), reason: "answered", outcome: "deny", by: "dashboard", choice: "deny_stop" }),
      frame({ type: "dismiss", id: id(2), reason: "answered", outcome: "allow", by: "dashboard", choice: "approve_accept_edits" }),
    );
    expect(s.toasts.map((t) => t.text)).toEqual([
      "Bash: allowed for this session: pnpm test …, answered on the dashboard",
      "Bash: denied, Claude was asked why, answered on the dashboard",
      "Bash: denied, Claude stopped, answered on the dashboard",
      "Plan: plan approved, edits accepted, answered on the dashboard",
    ]);
    expect(s.log[0]?.text).toBe("Plan: plan approved, edits accepted, answered on the dashboard");
  });

  it("loads the full text for questions, plans and shortened permissions only", () => {
    let s = run(
      open(),
      frame({ type: "permission", id: id(6), tool: "Bash", summary: "x", risk: "low", cmd: "x", timeout: 30_000 }),
      frame({ type: "permission", id: id(7), tool: "Bash", summary: "x", risk: "low", cmd: "x", cut: true, timeout: 30_000 }),
      frame(plan),
    );
    expect(s.requests.map(needsFullText)).toEqual([false, true, true]);
    s = run(s, { type: "detail", id: id(2), full: { state: "loaded", toolInput: { plan: "# Add pet packs" } } });
    expect(s.requests[2]?.full).toEqual({ state: "loaded", toolInput: { plan: "# Add pet packs" } });
  });
});

describe("extend rules (protocol 14.6)", () => {
  it("extends questions and plans only below 55 s, at most every 5 s", () => {
    let s = run(open(), frame(plan));
    const card = () => s.requests[0]!;
    expect(EXTEND_BELOW_MS).toBe(55_000);
    expect(extendDue(card(), T0)).toBe(true);
    s = run(s, { type: "extend", id: id(2), now: T0 });
    expect(extendDue(card(), T0 + EXTEND_GAP_MS - 1)).toBe(false);
    expect(extendDue(card(), T0 + EXTEND_GAP_MS)).toBe(true);
    s = run(s, frame({ type: "deadline", id: id(2), timeout: 60_000 }, T0 + 6000));
    expect(extendDue(card(), T0 + 6000)).toBe(false);
    expect(extendDue(card(), T0 + 6000 + 5001)).toBe(true);
  });

  it("never extends a permission or a question that is not complete", () => {
    const s = run(open(), frame({ type: "permission", id: id(8), tool: "Bash", summary: "x", risk: "low", timeout: 10_000 }), frame(question(0, 2)));
    expect(s.requests.map((r) => extendDue(r, T0))).toEqual([false, false]);
  });
});
