// Building blocks shared by the permission, question and plan cards.

import { h, setText } from "../dom.js";
import { seconds } from "../format.js";
import type { Reply, RequestCard } from "../store.js";

/** A confirming button (high-risk allow, wider grants) stays armed this long. */
export const CONFIRM_MS = 4000;

export interface CardActions {
  reply(id: string, reply: Reply): void;
  /** A click or key inside a question or plan card (protocol 14.6 `extend`). */
  touch(id: string): void;
}

export interface CardContext {
  now: number;
  connected: boolean;
}

export interface CardView {
  readonly root: HTMLElement;
  update(card: RequestCard, ctx: CardContext): void;
  /** Global keys A / R / T on the first card; true when handled. */
  shortcut?(key: "a" | "r" | "t"): boolean;
  dispose?(): void;
}

/** A button that acts on the second click within CONFIRM_MS; the first only arms it. */
export class ConfirmButton {
  readonly el: HTMLButtonElement;
  private timer: ReturnType<typeof setTimeout> | null = null;

  constructor(
    private readonly label: Node | string,
    private readonly armedLabel: string,
    cls: string,
    private readonly fire: () => void,
  ) {
    this.el = h("button", { type: "button", class: cls }, label);
    this.el.addEventListener("click", () => this.press());
  }

  get armed(): boolean {
    return this.timer !== null;
  }

  press(): void {
    if (this.el.disabled) return;
    if (this.timer === null) {
      this.el.classList.add("confirming");
      this.el.replaceChildren(this.armedLabel);
      this.timer = setTimeout(() => this.reset(), CONFIRM_MS);
      return;
    }
    this.reset();
    this.fire();
  }

  reset(): void {
    if (this.timer !== null) clearTimeout(this.timer);
    this.timer = null;
    this.el.classList.remove("confirming");
    this.el.replaceChildren(this.label);
  }
}

export function button(label: string, cls: string, onClick: () => void, title?: string): HTMLButtonElement {
  const el = h("button", { type: "button", class: cls, title }, label);
  el.addEventListener("click", onClick);
  return el;
}

/** "devotchi · 3fa9c1" from the request's project and session. */
export function origin(card: RequestCard): string {
  return [card.project, card.sid].filter((part) => part !== null).join(" · ");
}

export interface CardFrame {
  root: HTMLElement;
  countdown: HTMLElement;
  note: HTMLElement;
}

/** The outer card with its head line; `body` and `foot` go between head and note. */
export function cardFrame(card: RequestCard, cls: string, aria: string, chip: HTMLElement, ...headExtra: (Node | null)[]): CardFrame {
  const countdown = h("span", { class: "countdown", "aria-label": "time left" });
  const note = h("span", { class: "card-note", role: "status" });
  const where = origin(card);
  const root = h(
    "article",
    { class: `card ${cls}`, "data-id": card.id, "data-kind": card.kind, "aria-label": aria },
    h("div", { class: "card-head" }, chip, ...headExtra, where !== "" ? h("span", { class: "project" }, where) : null, countdown),
  );
  return { root, countdown, note };
}

/** Countdown, busy state and the note line every card shows. Returns whether buttons are usable. */
export function updateCommon(frame: CardFrame, card: RequestCard, ctx: CardContext): boolean {
  setText(frame.countdown, seconds(card.deadline - ctx.now));
  const note = card.error ?? (card.sending !== null ? "sending…" : ctx.connected ? "" : "not connected");
  setText(frame.note, note);
  frame.note.classList.toggle("error", card.error !== null);
  return card.sending === null && ctx.connected;
}

/** Sends `touch` for any click or key inside the card. */
export function watchTouches(root: HTMLElement, id: string, actions: CardActions): void {
  const touched = () => actions.touch(id);
  root.addEventListener("click", touched, true);
  root.addEventListener("keydown", touched, true);
}
