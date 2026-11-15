// Plan card (ux-flows 3.3): the whole Markdown plan as plain preformatted
// text (never rendered as HTML), Approve, Approve and accept edits, Revise,
// Stop and Terminal. With the whole plan on screen the dashboard is not bound
// by the device rules for `cut` / `more` (protocol 14.5); when the full text
// cannot be loaded it applies them to the frame it has.

import { h, setText } from "../dom.js";
import type { PlanCard, RequestCard } from "../store.js";
import { ConfirmButton, button, cardFrame, updateCommon, watchTouches, type CardActions, type CardContext, type CardFrame, type CardView } from "./card.js";
import { fullPlan } from "./fulltext.js";

/** Protocol 14.5 table for a frame shown without the full plan. */
export function planChoices(card: PlanCard, wholePlanShown: boolean): { approve: boolean; acceptEdits: boolean } {
  if (wholePlanShown) return { approve: true, acceptEdits: true };
  return { approve: card.more === 0, acceptEdits: card.more === 0 && !card.cut };
}

export class PlanCardView implements CardView {
  readonly root: HTMLElement;
  private readonly frame: CardFrame;
  private readonly title: HTMLElement;
  private readonly body: HTMLElement;
  private readonly hint: HTMLElement;
  private readonly approve: HTMLButtonElement;
  private readonly acceptEdits: ConfirmButton;
  private readonly others: HTMLButtonElement[];
  private shown: "full" | "frame" | null = null;

  constructor(card: PlanCard, actions: CardActions) {
    const id = card.id;
    this.frame = cardFrame(card, "plan", `Plan: ${card.title}`, h("span", { class: "chip ask" }, "Plan"));
    this.root = this.frame.root;
    this.title = h("p", { class: "summary plan-title" });
    this.body = h("div", { class: "plan-body" });
    this.hint = h("p", { class: "hint" });
    this.approve = button("Approve", "approve", () => actions.reply(id, { kind: "decide", choice: "approve" }), "Claude leaves plan mode and still asks before each edit");
    this.acceptEdits = new ConfirmButton("Approve and accept edits", "Click again: Claude may change any file", "grant", () =>
      actions.reply(id, { kind: "decide", choice: "approve_accept_edits" }),
    );
    this.acceptEdits.el.title = "Claude may change any file in this project without asking, until it exits";
    this.others = [
      button("Revise", "reject", () => actions.reply(id, { kind: "decide", choice: "revise" }), "Claude stays in plan mode and asks what to change"),
      button("Stop", "reject", () => actions.reply(id, { kind: "decide", choice: "deny_stop" }), "Claude stops this turn"),
      button("Terminal", "defer", () => actions.reply(id, { kind: "defer" }), "Decide at the keyboard instead"),
    ];
    this.root.append(this.title, this.body, this.hint, h("div", { class: "card-foot" }, this.approve, this.acceptEdits.el, ...this.others, this.frame.note));
    watchTouches(this.root, id, actions);
  }

  update(card: RequestCard, ctx: CardContext): void {
    if (card.kind !== "plan") return;
    const usable = updateCommon(this.frame, card, ctx);
    const plan = fullPlan(card.full);
    setText(this.title, card.title);
    this.title.hidden = plan !== null;
    if (plan !== null && this.shown !== "full") {
      this.shown = "full";
      this.body.replaceChildren(h("pre", { class: "plan-text" }, plan));
    } else if (plan === null && this.shown !== "frame") {
      this.shown = "frame";
      const steps = h("ol", { class: "steps" }, ...card.steps.map((step) => h("li", {}, step)));
      this.body.replaceChildren(steps, card.more > 0 ? h("p", { class: "hint" }, `+${card.more} more step${card.more === 1 ? "" : "s"}`) : "");
    }
    const allowed = planChoices(card, plan !== null);
    let hint = "";
    if (plan === null && card.full.state === "loading") hint = "Loading the full plan…";
    else if (plan === null && (card.cut || card.more > 0)) hint = "Shortened · full plan in the terminal";
    setText(this.hint, hint);
    this.approve.disabled = !usable || !allowed.approve;
    this.acceptEdits.el.disabled = !usable || !allowed.acceptEdits;
    if (this.acceptEdits.el.disabled) this.acceptEdits.reset();
    for (const b of this.others) b.disabled = !usable;
  }

  dispose(): void {
    this.acceptEdits.reset();
  }
}
