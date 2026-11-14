// Event log, newest first, built from DOM nodes.

import { h } from "../dom.js";
import type { LogEntry } from "../store.js";

let rendered: readonly LogEntry[] | null = null;

export function updateLog(list: HTMLElement, log: readonly LogEntry[]): void {
  if (log === rendered) return;
  rendered = log;
  list.replaceChildren(
    ...log.map((e) =>
      h(
        "li",
        {},
        h("time", { datetime: e.at }, new Date(e.at).toLocaleTimeString()),
        h("span", { class: `log-kind kind-${e.kind}` }, e.kind),
        h("span", { class: "log-text" }, e.text),
      ),
    ),
  );
}
