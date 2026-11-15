// Pending request cards. Cards are keyed by id and updated in place, so the
// countdown can tick without resetting focus, a half-filled question or an
// armed confirmation.

import { placeAt } from "../dom.js";
import { isPresentable, type RequestCard, type State } from "../store.js";
import type { CardActions, CardView } from "./card.js";
import { PermissionCardView } from "./permission-card.js";
import { PlanCardView } from "./plan-card.js";
import { QuestionCardView } from "./question-card.js";

export type { CardActions } from "./card.js";

function build(card: RequestCard, actions: CardActions): CardView {
  switch (card.kind) {
    case "permission":
      return new PermissionCardView(card, actions);
    case "question":
      return new QuestionCardView(card, actions);
    case "plan":
      return new PlanCardView(card, actions);
  }
}

export class RequestList {
  private readonly cards = new Map<string, CardView>();
  private first: CardView | null = null;

  constructor(
    private readonly list: HTMLElement,
    private readonly empty: HTMLElement,
    private readonly actions: CardActions,
  ) {}

  update(state: State, now: number): void {
    const ctx = { now, connected: state.connection.phase === "open" };
    const shown = state.requests.filter(isPresentable);
    const ids = new Set(shown.map((r) => r.id));
    for (const [id, view] of this.cards) {
      if (ids.has(id)) continue;
      view.dispose?.();
      view.root.remove();
      this.cards.delete(id);
    }
    for (const [index, req] of shown.entries()) {
      let view = this.cards.get(req.id);
      if (view === undefined) {
        view = build(req, this.actions);
        this.cards.set(req.id, view);
      }
      placeAt(this.list, view.root, index);
      view.update(req, ctx);
    }
    this.first = shown[0] !== undefined ? (this.cards.get(shown[0].id) ?? null) : null;
    this.empty.hidden = shown.length > 0;
  }

  /** Global keys A / R / T act on the first request when it is a permission. Returns true when handled. */
  keydown(e: KeyboardEvent): boolean {
    const key = e.key.toLowerCase();
    if (this.first?.shortcut === undefined || (key !== "a" && key !== "r" && key !== "t")) return false;
    // Auto-repeat of a handled key must not reach the page either.
    if (e.repeat) return true;
    return this.first.shortcut(key);
  }
}
