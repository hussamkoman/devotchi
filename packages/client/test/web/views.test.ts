// @vitest-environment happy-dom
import { afterEach, describe, expect, it, vi } from "vitest";
import type { ServerToConsumerMessage } from "@devotchi/shared";
import { initialState, reduce, type Reply, type State } from "../../web/src/store.js";
import { CONFIRM_MS } from "../../web/src/views/card.js";
import { RequestList } from "../../web/src/views/requests.js";

const id = (n: number) => n.toString(16).padStart(32, "0");

function withRequests(n: number): State {
  let state = reduce(initialState(), { type: "connection", phase: "open", now: 0 });
  for (let i = 1; i <= n; i++) {
    state = reduce(state, {
      type: "frame",
      now: 0,
      msg: { type: "permission", id: id(i), tool: "Bash", summary: `cmd ${i}`, risk: "low", timeout: 30_000, timestamp: new Date(0).toISOString() },
    });
  }
  return state;
}

describe("request list rendering", () => {
  it("leaves rendered cards in place on later updates, so focus inside a card survives the countdown tick", () => {
    const list = document.createElement("div");
    const empty = document.createElement("p");
    document.body.append(list, empty);
    const view = new RequestList(list, empty, { reply: () => undefined, touch: () => undefined });
    const state = withRequests(2);
    view.update(state, 0);
    expect([...list.children].map((c) => c.getAttribute("data-id"))).toEqual([id(1), id(2)]);

    const mutations: MutationRecord[] = [];
    const observer = new MutationObserver((records) => mutations.push(...records));
    observer.observe(list, { childList: true });
    view.update(state, 500);
    view.update(state, 1000);
    expect(observer.takeRecords().concat(mutations).filter((m) => m.type === "childList")).toEqual([]);
    observer.disconnect();
  });
});

function mount(...msgs: ServerToConsumerMessage[]) {
  let state = reduce(initialState(), { type: "connection", phase: "open", now: 0 });
  for (const msg of msgs) state = reduce(state, { type: "frame", msg, now: 0 });
  const list = document.createElement("div");
  const empty = document.createElement("p");
  document.body.replaceChildren(list, empty);
  const replies: Reply[] = [];
  const touches: string[] = [];
  const view = new RequestList(list, empty, { reply: (_id, r) => replies.push(r), touch: (id) => touches.push(id) });
  view.update(state, 0);
  const q = <T extends Element = HTMLElement>(sel: string) => list.querySelector(sel) as T | null;
  const all = (sel: string) => [...list.querySelectorAll(sel)] as HTMLElement[];
  const buttonByText = (text: string) => all("button").find((b) => b.textContent?.startsWith(text)) as HTMLButtonElement | undefined;
  return { list, view, replies, touches, q, all, buttonByText, rerender: (s: State = state) => view.update(s, 0), state: () => state, set: (next: State) => (state = next) };
}

const perm = (extra: Partial<Extract<ServerToConsumerMessage, { type: "permission" }>> = {}): ServerToConsumerMessage => ({
  type: "permission",
  id: id(1),
  tool: "Bash",
  summary: "pnpm test",
  risk: "low",
  cmd: "pnpm test --filter server",
  timeout: 30_000,
  ...extra,
});

describe("permission card", () => {
  afterEach(() => vi.useRealTimers());

  it("shows the command first, the reason and Claude's words as text, and the session rule word for word", () => {
    const m = mount(perm({ risk: "med", reason: "Runs package scripts", desc: "<b>Run</b> tests", grant: { rule: "pnpm test", match: "prefix" } }));
    const card = m.q(".card")!;
    expect(card.children[1]?.className).toContain("cmd");
    expect(m.q(".cmd")?.textContent).toBe("pnpm test --filter server");
    expect(m.q(".risk-label")?.textContent).toBe("MEDIUM RISK");
    expect(m.q(".reason")?.textContent).toBe("\u26A0 Runs package scripts");
    expect(m.q(".desc")?.textContent).toBe("Claude: \u201C<b>Run</b> tests\u201D");
    expect(m.q(".desc b")).toBeNull();
    expect(m.all(".menu-row .row-title").map((e) => e.textContent)).toEqual(["Send to terminal", "Allow for this session", "Deny, ask why", "Deny and stop"]);
    expect(m.q(".rule")?.textContent).toBe("pnpm test \u2026");
    expect(m.q(".grant")?.textContent).toContain("Starts like this, until Claude exits.");
    // A grant row is a second-click confirmation.
    m.q<HTMLButtonElement>(".menu-row.grant")!.click();
    expect(m.replies).toEqual([]);
    m.q<HTMLButtonElement>(".menu-row.grant")!.click();
    expect(m.replies).toEqual([{ kind: "decide", choice: "allow_session" }]);
  });

  it("offers no session row without a grant, and maps the deny rows", () => {
    const m = mount(perm());
    expect(m.q(".rule")).toBeNull();
    expect(m.all(".menu-row .row-title").map((e) => e.textContent)).toEqual(["Send to terminal", "Deny, ask why", "Deny and stop"]);
    for (const row of m.all(".menu-row")) row.click();
    m.buttonByText("Deny")!.click();
    m.buttonByText("Allow")!.click();
    expect(m.replies).toEqual([{ kind: "defer" }, { kind: "decide", choice: "deny_ask_why" }, { kind: "decide", choice: "deny_stop" }, { kind: "deny" }, { kind: "allow" }]);
  });

  it("high risk: Allow needs a second click before the confirmation expires", () => {
    vi.useFakeTimers();
    const m = mount(perm({ risk: "high", cmd: "git push --force" }));
    const allow = m.q<HTMLButtonElement>("button.approve")!;
    allow.click();
    expect(allow.textContent).toBe("Click again to allow");
    vi.advanceTimersByTime(CONFIRM_MS);
    expect(allow.textContent).toBe("Allow");
    allow.click();
    allow.click();
    expect(m.replies).toEqual([{ kind: "allow" }]);
  });

  it("a shortened command can be allowed only once the full text is shown", () => {
    const m = mount(perm({ cmd: "echo aaaa", cut: true }));
    const allow = m.q<HTMLButtonElement>("button.approve")!;
    expect(allow.disabled).toBe(true);
    m.set(reduce(m.state(), { type: "detail", id: id(1), full: { state: "loaded", toolInput: { command: "echo aaaa bbbb" } } }));
    m.rerender(m.state());
    expect(m.q(".cmd")?.textContent).toBe("echo aaaa bbbb");
    expect(allow.disabled).toBe(false);
  });
});

const qframe = (index: number, multi: boolean): ServerToConsumerMessage => ({
  type: "question",
  id: id(2),
  index,
  count: 2,
  header: "Pick",
  text: `Q${index}?`,
  multi,
  options: [{ label: "A", desc: "short" }, { label: "B" }],
  timeout: 30_000,
});

describe("question card", () => {
  it("enables Send once every question is answered and sends indexes only", () => {
    const m = mount(qframe(0, false), qframe(1, true));
    const send = m.buttonByText("Send")!;
    expect(m.all(".q-header").map((e) => e.textContent)).toEqual(["PICK", "PICK"]);
    expect(send.disabled).toBe(true);
    const inputs = m.all("fieldset").map((f) => [...f.querySelectorAll("input")] as HTMLInputElement[]);
    inputs[0]![1]!.click();
    expect(send.disabled).toBe(true);
    inputs[1]![0]!.click();
    inputs[1]![1]!.click();
    expect(send.disabled).toBe(false);
    send.click();
    expect(m.replies.at(-1)).toEqual({ kind: "answers", answers: [[1], [0, 1]] });
    // "None of these" clears the ticks; "No preference" answers null.
    inputs[1]![2]!.click();
    expect(inputs[1]!.slice(0, 2).map((i) => i.checked)).toEqual([false, false]);
    inputs[0]![2]!.click();
    send.click();
    expect(m.replies.at(-1)).toEqual({ kind: "answers", answers: [null, []] });
    expect(m.touches.length).toBeGreaterThan(0);
  });

  it("shows the original option descriptions once the full request is loaded", () => {
    const m = mount(qframe(0, false), qframe(1, false));
    const long = "d".repeat(400);
    m.set(reduce(m.state(), { type: "detail", id: id(2), full: { state: "loaded", toolInput: { questions: [{ question: "Q0?", options: [{ label: "A", description: long }, { label: "B" }] }] } } }));
    m.rerender(m.state());
    expect(m.q(".option-desc")?.textContent).toBe(long);
    m.buttonByText("Decline")!.click();
    m.buttonByText("Answer in terminal")!.click();
    expect(m.replies).toEqual([{ kind: "decide", choice: "decline" }, { kind: "defer" }]);
  });
});

describe("plan card", () => {
  const planMsg = (extra = {}): ServerToConsumerMessage => ({ type: "plan", id: id(3), title: "T", steps: ["a", "b"], more: 0, timeout: 30_000, ...extra });

  it("shows the whole plan as text, never as HTML, with every choice", () => {
    const m = mount(planMsg({ cut: true, more: 3 }));
    expect(m.buttonByText("Approve")!.disabled).toBe(true);
    const markdown = "# Plan\n\n<img src=x onerror=alert(1)>\n- step";
    m.set(reduce(m.state(), { type: "detail", id: id(3), full: { state: "loaded", toolInput: { plan: markdown } } }));
    m.rerender(m.state());
    expect(m.q(".plan-text")?.textContent).toBe(markdown);
    expect(m.q("img")).toBeNull();
    for (const label of ["Approve", "Revise", "Stop", "Terminal"]) m.buttonByText(label)!.click();
    const accept = m.buttonByText("Approve and accept edits")!;
    accept.click();
    accept.click();
    expect(m.replies).toEqual([
      { kind: "decide", choice: "approve" },
      { kind: "decide", choice: "revise" },
      { kind: "decide", choice: "deny_stop" },
      { kind: "defer" },
      { kind: "decide", choice: "approve_accept_edits" },
    ]);
  });

  it("without the full text applies the device table to the frame", () => {
    const failed = (extra: object) => {
      const m = mount(planMsg(extra));
      m.set(reduce(m.state(), { type: "detail", id: id(3), full: { state: "failed" } }));
      m.rerender(m.state());
      return [m.buttonByText("Approve")!.disabled, m.buttonByText("Approve and accept edits")!.disabled];
    };
    expect(failed({})).toEqual([false, false]);
    expect(failed({ cut: true })).toEqual([false, true]);
    expect(failed({ cut: true, more: 2 })).toEqual([true, true]);
  });
});
