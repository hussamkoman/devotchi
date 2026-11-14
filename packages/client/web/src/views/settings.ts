// Room mode and hold time (protocol 6) plus local preferences.

import { HOLD_MAX_MS, HOLD_MIN_MS, type RoomSettings } from "@devotchi/shared";
import { byId, h, setText } from "../dom.js";
import { holdLabel } from "../format.js";
import type { State } from "../store.js";

export const HOLD_CHOICES_MS = [10_000, 20_000, 30_000, 45_000, 60_000, 120_000, 180_000, 300_000].filter(
  (ms) => ms >= HOLD_MIN_MS && ms <= HOLD_MAX_MS,
);

export function holdHint(state: State): string {
  const count = state.diagnostics?.dialogWhilePending.count ?? 0;
  return count > 0
    ? "Claude Code showed its own prompt during a pending request; a longer hold time is safe"
    : "while a device shows a request, the terminal waits up to the hold time";
}

export interface SettingsActions {
  update(changes: Partial<RoomSettings>): Promise<void>;
  enableNotifications(): Promise<void>;
}

export class SettingsPanel {
  private readonly ask = byId<HTMLButtonElement>("mode-ask");
  private readonly desk = byId<HTMLButtonElement>("mode-desk");
  private readonly hold = byId<HTMLSelectElement>("hold-time");
  private readonly error = byId("settings-error");

  constructor(private readonly actions: SettingsActions) {
    for (const ms of HOLD_CHOICES_MS) this.hold.append(h("option", { value: ms }, holdLabel(ms)));
    const run = (p: Promise<void>) => {
      setText(this.error, "");
      p.catch((err: unknown) => setText(this.error, err instanceof Error ? err.message : String(err)));
    };
    this.ask.addEventListener("click", () => run(actions.update({ mode: "ask" })));
    this.desk.addEventListener("click", () => run(actions.update({ mode: "desk" })));
    this.hold.addEventListener("change", () => run(actions.update({ holdMs: Number(this.hold.value) })));
    byId("notify-optin").addEventListener("click", () => run(actions.enableNotifications()));
  }

  update(state: State, notificationState: string): void {
    const mode = state.settings?.mode ?? state.mode;
    this.ask.setAttribute("aria-pressed", String(mode === "ask"));
    this.desk.setAttribute("aria-pressed", String(mode === "desk"));
    const holdMs = state.settings?.holdMs;
    if (holdMs !== undefined) {
      if (![...this.hold.options].some((o) => Number(o.value) === holdMs)) {
        this.hold.append(h("option", { value: holdMs }, holdLabel(holdMs)));
      }
      if (document.activeElement !== this.hold) this.hold.value = String(holdMs);
    }
    this.hold.disabled = holdMs === undefined;
    setText(byId("hold-hint"), holdHint(state));
    setText(byId("notify-state"), notificationState);
    byId<HTMLButtonElement>("notify-optin").hidden = notificationState !== "off";
    const c = state.config;
    setText(byId("server-info"), c === null ? "" : `${c.serverUrl} · server ${c.serverVersion} · ${c.mode}${c.local ? " · embedded" : ""}`);
  }
}
