// Paired devices with liveness (protocol 7.5): online state, last frame,
// last ack, held requests, battery, signal and firmware.

import type { Device } from "@devotchi/shared";
import { h, placeAt, setText } from "../dom.js";
import { relativeTime } from "../format.js";
import type { State } from "../store.js";

export interface DeviceActions {
  rename(id: string, name: string): Promise<void>;
  identify(id: string): Promise<void>;
  unpair(id: string): Promise<void>;
}

const LOW_PCT = 15;

export function batteryText(device: Device): { text: string; low: boolean } {
  const t = device.telemetry;
  if (t === null) return { text: "no telemetry yet", low: false };
  const b = t.battery;
  if (b.state === "absent" || b.pct === null) return { text: "USB", low: false };
  const low = t.alert !== undefined || b.pct <= LOW_PCT;
  const bolt = b.state === "charging" ? " ⚡ charging" : b.state === "full" ? " full" : "";
  return { text: `${b.pct}%${bolt}${low ? " (low)" : ""}`, low };
}

export function signalBars(rssi: number): number {
  if (rssi >= -55) return 4;
  if (rssi >= -65) return 3;
  if (rssi >= -75) return 2;
  if (rssi >= -85) return 1;
  return 0;
}

interface DeviceView {
  root: HTMLElement;
  name: HTMLElement;
  dot: HTMLElement;
  online: HTMLElement;
  seen: HTMLElement;
  ack: HTMLElement;
  holding: HTMLElement;
  battery: HTMLElement;
  signal: HTMLElement;
  fw: HTMLElement;
  identify: HTMLButtonElement;
  renameForm: HTMLFormElement;
  renameInput: HTMLInputElement;
  error: HTMLElement;
}

export class DevicesPanel {
  private readonly views = new Map<string, DeviceView>();

  constructor(
    private readonly list: HTMLElement,
    private readonly empty: HTMLElement,
    private readonly actions: DeviceActions,
  ) {}

  update(state: State, now: number): void {
    const ids = new Set(state.devices.map((d) => d.id));
    for (const [id, view] of this.views) {
      if (!ids.has(id)) {
        view.root.remove();
        this.views.delete(id);
      }
    }
    for (const [index, device] of state.devices.entries()) {
      let view = this.views.get(device.id);
      if (view === undefined) {
        view = this.build(device);
        this.views.set(device.id, view);
      }
      placeAt(this.list, view.root, index);
      this.fill(view, device, now);
    }
    this.empty.hidden = state.devices.length > 0;
  }

  private fill(v: DeviceView, d: Device, now: number): void {
    setText(v.name, d.name);
    v.dot.className = `dot ${d.online ? "online" : "offline"}`;
    setText(v.online, d.online ? "online" : "offline");
    setText(v.seen, `last seen ${relativeTime(d.lastSeen, now)}`);
    setText(v.ack, `last ack ${relativeTime(d.lastHoldAt, now)}`);
    setText(v.holding, `holding ${d.holding}`);
    const battery = batteryText(d);
    setText(v.battery, `battery ${battery.text}`);
    v.battery.classList.toggle("low", battery.low);
    const rssi = d.telemetry?.rssi;
    if (rssi === undefined) {
      setText(v.signal, "signal unknown");
    } else {
      const bars = signalBars(rssi);
      setText(v.signal, `signal ${"▂▄▆█".slice(0, bars).padEnd(4, "·")}`);
      v.signal.setAttribute("aria-label", `signal ${bars} of 4 (${rssi} dBm)`);
    }
    setText(v.fw, `firmware ${d.fw ?? d.telemetry?.fw ?? "unknown"}`);
    v.identify.disabled = !d.online;
  }

  private build(d: Device): DeviceView {
    const name = h("strong", { class: "device-name" });
    const renameInput = h("input", { type: "text", maxlength: 24, "aria-label": "New device name", required: true });
    const renameForm = h("form", { class: "rename", hidden: true }, renameInput, h("button", { type: "submit" }, "Save"));
    const renameButton = h("button", { type: "button", class: "link" }, "Rename");
    const identify = h("button", { type: "button" }, "Identify");
    const unpair = h("button", { type: "button", class: "danger" }, "Unpair");
    const error = h("p", { class: "error", role: "alert" });
    const span = (cls: string) => h("span", { class: cls });
    const view: DeviceView = {
      root: h("li", { class: "device" }),
      name,
      dot: span("dot"),
      online: span("online-text"),
      seen: span("meta"),
      ack: span("meta"),
      holding: span("meta"),
      battery: span("meta battery"),
      signal: span("meta"),
      fw: span("meta"),
      identify,
      renameForm,
      renameInput,
      error,
    };
    view.root.append(
      h("div", { class: "device-head" }, view.dot, name, view.online, renameButton),
      renameForm,
      h("div", { class: "device-meta" }, view.seen, view.ack, view.holding, view.battery, view.signal, view.fw),
      h("div", { class: "device-actions" }, identify, unpair),
      error,
    );
    const run = (p: Promise<void>) => {
      setText(error, "");
      p.catch((err: unknown) => setText(error, err instanceof Error ? err.message : String(err)));
    };
    renameButton.addEventListener("click", () => {
      renameForm.hidden = !renameForm.hidden;
      if (!renameForm.hidden) {
        renameInput.value = name.textContent ?? "";
        renameInput.focus();
      }
    });
    renameForm.addEventListener("submit", (e) => {
      e.preventDefault();
      const value = renameInput.value.trim();
      if (value === "") return;
      run(
        this.actions.rename(d.id, value).then(() => {
          renameForm.hidden = true;
        }),
      );
    });
    identify.addEventListener("click", () => run(this.actions.identify(d.id)));
    unpair.addEventListener("click", () => {
      if (window.confirm(`Unpair ${name.textContent ?? d.id}? The device returns to its pairing screen.`)) run(this.actions.unpair(d.id));
    });
    return view;
  }
}
