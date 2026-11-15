// Permission card (ux-flows 3.1, 3.2): the exact command first, the server's
// risk reason, Claude's own description marked as such, Allow / Deny and the
// "More" menu with the device's rows. Allowing beyond one call, and allowing
// a high-risk call, takes a second confirming click.

import { h, setText } from "../dom.js";
import type { PermissionCard, RequestCard } from "../store.js";
import { ConfirmButton, button, cardFrame, updateCommon, type CardActions, type CardContext, type CardFrame, type CardView } from "./card.js";
import { fullCommand } from "./fulltext.js";

export const RISK_WORDS = { low: "LOW RISK", med: "MEDIUM RISK", high: "HIGH RISK" } as const;
const RISK_BARS = { low: 1, med: 2, high: 3 } as const;

function riskBars(risk: PermissionCard["risk"]): HTMLElement {
  const bars = h("span", { class: `bars risk-${risk}`, "aria-hidden": "true" });
  for (let i = 1; i <= 3; i++) bars.append(h("i", { class: i <= RISK_BARS[risk] ? "on" : "" }));
  return bars;
}

function menuRow(title: string, cls: string, ...explain: (Node | string)[]): HTMLElement {
  return h("span", { class: `row-text ${cls}` }, h("span", { class: "row-title" }, title), ...explain.map((e) => (typeof e === "string" ? h("span", { class: "row-desc" }, e) : e)));
}

export class PermissionCardView implements CardView {
  readonly root: HTMLElement;
  private readonly frame: CardFrame;
  private readonly well: HTMLElement;
  private readonly cutNote: HTMLElement;
  private readonly allow: ConfirmButton | HTMLButtonElement;
  private readonly deny: HTMLButtonElement;
  private readonly more: HTMLButtonElement;
  private readonly menu: HTMLElement;
  private readonly session: ConfirmButton | null;
  private readonly rows: HTMLButtonElement[];

  constructor(card: PermissionCard, actions: CardActions) {
    const id = card.id;
    const chip = h("span", { class: `chip risk-${card.risk}` }, card.tool);
    this.frame = cardFrame(card, `risk-${card.risk}`, `${card.tool} request: ${card.cmd ?? card.summary}`, chip, riskBars(card.risk), h("span", { class: "risk-label" }, RISK_WORDS[card.risk]));
    this.root = this.frame.root;

    this.well = h("pre", { class: `cmd risk-${card.risk}` });
    this.cutNote = h("p", { class: "hint cut-note" });
    const reason = card.reason !== null ? h("p", { class: `reason risk-${card.risk}` }, `⚠ ${card.reason}`) : null;
    const desc = card.desc !== null ? h("p", { class: "desc" }, `Claude: “${card.desc}”`) : null;
    const summary = card.cmd === null ? h("p", { class: "summary" }, card.summary) : null;

    const allowOnce = () => actions.reply(id, { kind: "allow" });
    this.allow = card.risk === "high" ? new ConfirmButton("Allow", "Click again to allow", "approve", allowOnce) : button("Allow", "approve", allowOnce);
    if (card.risk === "high") this.allowEl.title = "High risk: click twice to allow this one call";
    this.deny = button("Deny", "reject", () => actions.reply(id, { kind: "deny" }), "Claude is told not to retry this call and to try another way");
    this.more = h("button", { type: "button", class: "more", "aria-expanded": "false" }, "More ▾");

    const terminal = h("button", { type: "button", class: "menu-row" }, menuRow("Send to terminal", "", "Decide at the keyboard instead."));
    terminal.addEventListener("click", () => actions.reply(id, { kind: "defer" }));
    this.session = null;
    const rows: HTMLButtonElement[] = [terminal];
    const grant = card.grant;
    if (grant !== null) {
      const rule = h("code", { class: "rule" }, grant.match === "prefix" ? `${grant.rule} …` : grant.rule);
      const how = grant.match === "prefix" ? "Starts like this, until Claude exits." : "Exactly this command, until Claude exits.";
      this.session = new ConfirmButton(
        menuRow("Allow for this session", "", rule, how),
        "Click again to allow for this session",
        "menu-row grant",
        () => actions.reply(id, { kind: "decide", choice: "allow_session" }),
      );
      rows.push(this.session.el);
    }
    const askWhy = h("button", { type: "button", class: "menu-row" }, menuRow("Deny, ask why", "", "Claude explains what this call is for, then may ask again."));
    askWhy.addEventListener("click", () => actions.reply(id, { kind: "decide", choice: "deny_ask_why" }));
    const stop = h("button", { type: "button", class: "menu-row" }, menuRow("Deny and stop", "", "Claude stops this turn."));
    stop.addEventListener("click", () => actions.reply(id, { kind: "decide", choice: "deny_stop" }));
    rows.push(askWhy, stop);
    this.rows = rows;
    this.menu = h("div", { class: "menu", role: "group", "aria-label": "More choices" }, ...rows);
    this.menu.hidden = true;
    this.more.addEventListener("click", () => {
      this.menu.hidden = !this.menu.hidden;
      this.more.setAttribute("aria-expanded", String(!this.menu.hidden));
      this.session?.reset();
    });

    this.root.append(
      this.well,
      this.cutNote,
      ...[reason, desc, summary].filter((el) => el !== null),
      h("div", { class: "card-foot" }, this.allowEl, this.deny, this.more, this.frame.note),
      this.menu,
    );
  }

  private get allowEl(): HTMLButtonElement {
    return this.allow instanceof ConfirmButton ? this.allow.el : this.allow;
  }

  update(card: RequestCard, ctx: CardContext): void {
    if (card.kind !== "permission") return;
    const usable = updateCommon(this.frame, card, ctx);
    const full = card.cut ? fullCommand(card.full, card.tool) : null;
    setText(this.well, full ?? card.cmd ?? card.details ?? card.summary);
    // The dashboard may allow a shortened command only once it shows the whole of it (ux-flows 3.4 rule 4).
    const readable = !card.cut || full !== null;
    setText(
      this.cutNote,
      !card.cut || full !== null ? "" : card.full.state === "failed" ? "Shortened, and the full command could not be loaded: allow it in the terminal" : "Shortened; loading the full command…",
    );
    this.allowEl.disabled = !usable || !readable;
    if (this.allowEl.disabled && this.allow instanceof ConfirmButton) this.allow.reset();
    this.deny.disabled = !usable;
    for (const row of this.rows) row.disabled = !usable;
    if (!usable) this.session?.reset();
  }

  shortcut(key: "a" | "r" | "t"): boolean {
    const target = key === "a" ? this.allowEl : key === "r" ? this.deny : this.rows[0];
    if (target === undefined || target.disabled) return false;
    if (key === "a" && this.allow instanceof ConfirmButton) this.allow.press();
    else target.click();
    return true;
  }

  dispose(): void {
    if (this.allow instanceof ConfirmButton) this.allow.reset();
    this.session?.reset();
  }
}
