// Connection badge and peer summary.

import { byId, setText } from "../dom.js";
import type { State } from "../store.js";

export function connectionText(state: State, now: number): string {
  const c = state.connection;
  switch (c.phase) {
    case "open":
      return "Connected";
    case "connecting":
      return "Connecting…";
    case "reconnecting": {
      const s = Math.max(0, Math.ceil(((c.retryAt ?? now) - now) / 1000));
      return `Reconnecting in ${s} s`;
    }
    case "stopped":
      return `Disconnected: ${c.reason ?? "stopped"}. Restart devotchi start.`;
  }
}

export function updateHeader(state: State, now: number): void {
  const badge = byId("connection");
  badge.dataset.phase = state.connection.phase;
  setText(byId("connection-text"), connectionText(state, now));
  badge.title = state.connection.reason ?? "";
  const p = state.peers;
  setText(
    byId("peers"),
    p === null ? "" : `${p.devices} device${p.devices === 1 ? "" : "s"} · ${p.producers} agent${p.producers === 1 ? "" : "s"} · ${p.consumers} screen${p.consumers === 1 ? "" : "s"}`,
  );
}
