// Question card (ux-flows 4): every question of an AskUserQuestion call on
// one card, with the full option descriptions from the original tool input.
// Only option indexes are sent; the server builds the answer text from the
// original input (protocol 14.4).

import type { QuestionAnswer, QuestionMessage } from "@devotchi/shared";
import { h, setText } from "../dom.js";
import type { QuestionCard, RequestCard } from "../store.js";
import { button, cardFrame, updateCommon, watchTouches, type CardActions, type CardContext, type CardFrame, type CardView } from "./card.js";
import { fullQuestions } from "./fulltext.js";

export const NO_PREFERENCE = "No preference, use your judgment";
export const NONE_OF_THESE = "None of these";

interface QuestionView {
  multi: boolean;
  header: HTMLElement;
  text: HTMLElement;
  labels: HTMLElement[];
  descs: HTMLElement[];
  options: HTMLInputElement[];
  noPreference: HTMLInputElement;
  /** Multi-select only: answer with no option ticked. */
  none: HTMLInputElement | null;
}

function choice(type: "radio" | "checkbox", name: string, cls: string, ...content: Node[]): { label: HTMLElement; input: HTMLInputElement } {
  const input = h("input", { type, name });
  return { label: h("label", { class: `option ${cls}` }, input, h("span", { class: "option-text" }, ...content)), input };
}

export class QuestionCardView implements CardView {
  readonly root: HTMLElement;
  private readonly frame: CardFrame;
  private readonly questions: QuestionView[] = [];
  private readonly send: HTMLButtonElement;
  private readonly terminal: HTMLButtonElement;
  private readonly decline: HTMLButtonElement;
  private usable = false;

  constructor(card: QuestionCard, actions: CardActions) {
    const frames = card.questions.filter((q): q is QuestionMessage => q !== null);
    const chip = h("span", { class: "chip ask" }, "Question");
    const count = frames.length > 1 ? h("span", { class: "risk-label" }, `${frames.length} questions`) : null;
    this.frame = cardFrame(card, "ask", `Question: ${frames[0]?.text ?? ""}`, chip, count);
    this.root = this.frame.root;

    const body = h("div", { class: "questions" });
    for (const [i, q] of frames.entries()) body.append(this.buildQuestion(card.id, i, q));
    this.send = button("Send", "approve", () => {
      const answers = this.answers();
      if (answers.every((a) => a !== undefined)) actions.reply(card.id, { kind: "answers", answers });
    });
    this.terminal = button("Answer in terminal", "defer", () => actions.reply(card.id, { kind: "defer" }), "Moves to your keyboard. You can type your own answer there.");
    this.decline = button("Decline", "reject", () => actions.reply(card.id, { kind: "decide", choice: "decline" }), "Claude is told you declined and uses its own judgment");
    this.root.append(body, h("div", { class: "card-foot" }, this.send, this.terminal, this.decline, this.frame.note));
    this.root.addEventListener("change", (e) => {
      this.exclusive(e.target);
      this.refreshSend();
    });
    watchTouches(this.root, card.id, actions);
  }

  private buildQuestion(id: string, index: number, q: QuestionMessage): HTMLElement {
    const name = `q-${id}-${index}`;
    const type = q.multi ? "checkbox" : "radio";
    const header = h("span", { class: "q-header" });
    const text = h("span", { class: "q-text" });
    const fieldset = h("fieldset", { class: "question" }, h("legend", {}, header, text, h("span", { class: "q-kind" }, q.multi ? "choose any" : "choose one")));
    const labels: HTMLElement[] = [];
    const descs: HTMLElement[] = [];
    const options = q.options.map(() => {
      const label = h("span", { class: "option-label" });
      const desc = h("span", { class: "option-desc" });
      const row = choice(type, name, "", label, desc);
      labels.push(label);
      descs.push(desc);
      fieldset.append(row.label);
      return row.input;
    });
    const none = q.multi ? choice("checkbox", `${name}-none`, "extra", document.createTextNode(NONE_OF_THESE)) : null;
    const noPreference = choice(type, q.multi ? `${name}-np` : name, "extra", document.createTextNode(NO_PREFERENCE));
    if (none !== null) fieldset.append(none.label);
    fieldset.append(noPreference.label);
    this.questions.push({ multi: q.multi, header, text, labels, descs, options, noPreference: noPreference.input, none: none?.input ?? null });
    return fieldset;
  }

  /** Multi-select: "None of these" and "No preference" each exclude every other tick. */
  private exclusive(target: EventTarget | null): void {
    if (!(target instanceof HTMLInputElement) || !target.checked) return;
    for (const q of this.questions) {
      if (!q.multi) continue;
      const all = [...q.options, q.none!, q.noPreference];
      if (!all.includes(target)) continue;
      const solo = target === q.none || target === q.noPreference;
      for (const other of all) if (other !== target && (solo || other === q.none || other === q.noPreference)) other.checked = false;
    }
  }

  /** One entry per question; undefined while that question is unanswered. */
  answers(): (QuestionAnswer | undefined)[] {
    return this.questions.map((q) => {
      if (q.noPreference.checked) return null;
      const ticked = q.options.flatMap((input, i) => (input.checked ? [i] : []));
      if (q.multi) return ticked.length > 0 ? ticked : q.none!.checked ? [] : undefined;
      return ticked.length === 1 ? ticked : undefined;
    });
  }

  private refreshSend(): void {
    this.send.disabled = !this.usable || this.answers().some((a) => a === undefined);
  }

  update(card: RequestCard, ctx: CardContext): void {
    if (card.kind !== "question") return;
    this.usable = updateCommon(this.frame, card, ctx);
    const full = fullQuestions(card.full);
    for (const [i, q] of this.questions.entries()) {
      const frame = card.questions[i];
      const original = full?.[i];
      const header = original?.header ?? frame?.header ?? "";
      setText(q.header, header.toUpperCase());
      q.header.hidden = header === "";
      setText(q.text, original?.question ?? frame?.text ?? "");
      for (const [j, label] of q.labels.entries()) {
        setText(label, original?.options[j]?.label ?? frame?.options[j]?.label ?? "");
        setText(q.descs[j]!, original?.options[j]?.description ?? frame?.options[j]?.desc ?? "");
      }
    }
    for (const q of this.questions) for (const input of [...q.options, q.noPreference, ...(q.none ? [q.none] : [])]) input.disabled = !this.usable;
    this.terminal.disabled = !this.usable;
    this.decline.disabled = !this.usable;
    this.refreshSend();
  }
}
