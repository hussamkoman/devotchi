// Human-readable liveness and battery text shared by status, devices,
// doctor and the MCP device summary.

import type { Device, DismissChoice, LastRequest, PendingRequestInfo } from "@devotchi/shared";

export function secondsSince(iso: string | null | undefined, now: number): number | null {
  if (iso === null || iso === undefined) return null;
  const at = Date.parse(iso);
  return Number.isFinite(at) ? Math.max(0, Math.round((now - at) / 1000)) : null;
}

/** "never", "4 s ago", "3 min ago", "2 h ago", "5 d ago". */
export function ago(seconds: number | null): string {
  if (seconds === null) return "never";
  if (seconds < 120) return `${seconds} s ago`;
  if (seconds < 7200) return `${Math.round(seconds / 60)} min ago`;
  if (seconds < 172_800) return `${Math.round(seconds / 3600)} h ago`;
  return `${Math.round(seconds / 86_400)} d ago`;
}

/** "4m07s" style duration, used in `run` notifications. */
export function minutesSeconds(ms: number): string {
  const total = Math.max(0, Math.round(ms / 1000));
  return `${Math.floor(total / 60)}m${String(total % 60).padStart(2, "0")}s`;
}

export function batteryText(device: Device): string {
  const battery = device.telemetry?.battery;
  if (battery === undefined) return "battery unknown";
  if (battery.state === "absent") return "no battery";
  return `battery ${battery.pct ?? "?"}% ${battery.state}`;
}

/** One summary per device: what `status` and `get_device_state` report. */
export function deviceLiveness(device: Device, now: number) {
  const t = device.telemetry;
  return {
    id: device.id,
    name: device.name,
    online: device.online,
    lastSeenSecAgo: secondsSince(device.lastSeen, now),
    lastAckSecAgo: secondsSince(device.lastHoldAt, now),
    holding: device.holding,
    battery: t === null ? null : { state: t.battery.state, pct: t.battery.pct },
    rssi: t?.rssi ?? null,
    fw: device.fw ?? t?.fw ?? null,
  };
}

export function deviceLine(device: Device, now: number): string {
  const live = deviceLiveness(device, now);
  const parts = [
    live.online ? "online" : "offline",
    `last seen ${ago(live.lastSeenSecAgo)}`,
    `last ack ${ago(live.lastAckSecAgo)}`,
    `holding ${live.holding}`,
    batteryText(device),
  ];
  if (live.rssi !== null) parts.push(`RSSI ${live.rssi} dBm`);
  if (live.fw !== null) parts.push(`fw ${live.fw}`);
  return `${device.id} "${device.name}": ${parts.join(", ")}`;
}

export function lastRequestLine(last: LastRequest | null, now: number): string {
  if (last === null) return "none yet";
  const by = last.by !== undefined ? ` by ${last.by}` : "";
  const choice = last.choice !== undefined ? `, ${CHOICE_WORDS[last.choice] ?? last.choice}` : "";
  return `${last.tool}: ${last.outcome} (${last.detail}${choice})${by}, ${ago(secondsSince(last.at, now))}`;
}

/** `dismiss.choice` / `lastRequest.choice` in words (protocol 14.3). */
export const CHOICE_WORDS: Record<DismissChoice, string> = {
  allow_session: "allowed for this session",
  deny_ask_why: "denied, asked why",
  deny_stop: "denied and stopped",
  approve: "plan approved, edits still asked",
  approve_accept_edits: "plan approved, edits accepted",
  revise: "plan sent back for changes",
  decline: "declined to answer",
  answered: "answers sent",
};

/** "2 (1 permission, 1 question)" for the pending requests of a room. */
export function pendingLine(pending: readonly PendingRequestInfo[]): string {
  if (pending.length === 0) return "none";
  const counts = new Map<string, number>();
  for (const p of pending) {
    // A server without phase 2 sends no kind: all its requests are permissions.
    const kind = p.kind ?? "permission";
    counts.set(kind, (counts.get(kind) ?? 0) + 1);
  }
  return `${pending.length} (${[...counts].map(([kind, n]) => `${n} ${kind}`).join(", ")})`;
}
