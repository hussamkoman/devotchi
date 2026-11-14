// Pending request cards. Cards are keyed by id and updated in place, so the
// countdown can tick without resetting focus or a press-and-hold in progress.

import { h, placeAt, setText } from "../dom.js";
import { seconds } from "../format.js";
import type { Answer, RequestCard, State } from "../store.js";

/** High-risk approvals need the button (or the A key) held this long. */
export const HOLD_TO_APPROVE_MS = 1000;

export interface RequestActions {
  answer(id: string, answer: Answer): void;
}

interface CardView {
  root: HTMLElement;
  countdown: HTMLElement;
  note: HTMLElement;
  approve: HTMLButtonElement;
  reject: HTMLButtonElement;
  defer: HTMLButtonElement;
  holdTimer: ReturnType<typeof setTimeout> | null;
}

const RISK_TEXT = { low: "low", med: "medium", high: "high" } as const;

export class RequestList {
  private readonly cards = new Map<string, CardView>();
  private first: RequestCard | null = null;
  private keyHold: string | null = null;

  constructor(
    private readonly list: HTMLElement,
    private readonly empty: HTMLElement,
    private readonly actions: RequestActions,
  ) {}

  update(state: State, now: number): void {
    const connected = state.connection.phase === "open";
    const ids = new Set(state.requests.map((r) => r.id));
    for (const [id, view] of this.cards) {
      if (ids.has(id)) continue;
      if (view.holdTimer !== null) clearTimeout(view.holdTimer);
      view.root.remove();
      this.cards.delete(id);
    }
    for (const [index, req] of state.requests.entries()) {
      let view = this.cards.get(req.id);
      if (view === undefined) {
        view = this.build(req);
        this.cards.set(req.id, view);
      }
      placeAt(this.list, view.root, index);
      setText(view.countdown, seconds(req.deadline - now));
      const busy = req.sending !== null || !connected;
      for (const button of [view.approve, view.reject, view.defer]) button.disabled = busy;
      setText(view.note, req.error ?? (req.sending !== null ? "sending…" : connected ? "" : "not connected"));
      view.note.classList.toggle("error", req.error !== null);
    }
    this.first = state.requests[0] ?? null;
    this.empty.hidden = state.requests.length > 0;
  }

  /** Global keys A / R / T act on the first request. Returns true when handled. */
  keydown(e: KeyboardEvent): boolean {
    const req = this.first;
    const view = req === null ? undefined : this.cards.get(req.id);
    const key = e.key.toLowerCase();
    if (req === null || view === undefined || !["a", "r", "t"].includes(key)) return false;
    // Auto-repeat of a handled key must not reach the page either.
    if (e.repeat) return true;
    if (key === "a" && !view.approve.disabled) {
      if (req.risk === "high") {
        this.keyHold = req.id;
        this.startHold(req.id, view);
      } else this.actions.answer(req.id, "allow");
      return true;
    }
    if (key === "r" && !view.reject.disabled) {
      this.actions.answer(req.id, "deny");
      return true;
    }
    if (key === "t" && !view.defer.disabled) {
      this.actions.answer(req.id, "defer");
      return true;
    }
    return false;
  }

  keyup(e: KeyboardEvent): void {
    if (e.key.toLowerCase() !== "a" || this.keyHold === null) return;
    const view = this.cards.get(this.keyHold);
    if (view !== undefined) this.cancelHold(view);
    this.keyHold = null;
  }

  private build(req: RequestCard): CardView {
    const risk = RISK_TEXT[req.risk];
    const approve = h("button", { type: "button", class: "approve" }, req.risk === "high" ? "Hold to approve" : "Approve");
    const reject = h("button", { type: "button", class: "reject" }, "Reject");
    const defer = h("button", { type: "button", class: "defer" }, "Send to terminal");
    const countdown = h("span", { class: "countdown", "aria-label": "time left" });
    const note = h("span", { class: "card-note", role: "status" });
    const root = h(
      "article",
      { class: `card risk-${req.risk}`, "data-id": req.id, "aria-label": `${req.tool} request: ${req.summary}` },
      h(
        "div",
        { class: "card-head" },
        h("span", { class: `chip risk-${req.risk}` }, req.tool),
        h("span", { class: "risk-label" }, `risk hint: ${risk}`),
        req.project !== null ? h("span", { class: "project" }, req.project) : null,
        countdown,
      ),
      h("p", { class: "summary" }, req.summary),
      req.details !== null && req.details !== "" ? h("pre", { class: "details" }, req.details) : null,
      h("div", { class: "card-foot" }, approve, reject, defer, note),
    );
    const view: CardView = { root, countdown, note, approve, reject, defer, holdTimer: null };

    if (req.risk === "high") {
      approve.title = `High risk hint: hold for ${HOLD_TO_APPROVE_MS / 1000} s to approve`;
      approve.addEventListener("pointerdown", (e) => {
        if (e.button === 0) this.startHold(req.id, view);
      });
      // blur: a key released after focus moved away never reaches this button.
      for (const ev of ["pointerup", "pointerleave", "pointercancel", "blur"] as const) {
        approve.addEventListener(ev, () => this.cancelHold(view));
      }
      approve.addEventListener("keydown", (e) => {
        if ((e.key === "Enter" || e.key === " ") && !e.repeat) {
          e.preventDefault();
          this.startHold(req.id, view);
        }
      });
      approve.addEventListener("keyup", (e) => {
        if (e.key === "Enter" || e.key === " ") this.cancelHold(view);
      });
    } else {
      approve.addEventListener("click", () => this.actions.answer(req.id, "allow"));
    }
    reject.addEventListener("click", () => this.actions.answer(req.id, "deny"));
    defer.addEventListener("click", () => this.actions.answer(req.id, "defer"));
    return view;
  }

  private startHold(id: string, view: CardView): void {
    if (view.approve.disabled || view.holdTimer !== null) return;
    view.approve.classList.add("holding");
    view.holdTimer = setTimeout(() => {
      view.holdTimer = null;
      view.approve.classList.remove("holding");
      this.actions.answer(id, "allow");
    }, HOLD_TO_APPROVE_MS);
  }

  private cancelHold(view: CardView): void {
    if (view.holdTimer === null) return;
    clearTimeout(view.holdTimer);
    view.holdTimer = null;
    view.approve.classList.remove("holding");
  }
}
