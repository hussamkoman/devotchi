// The Pets page (protocol 14.8): the catalog and the room's uploads with a
// static preview in any mood, upload and delete, and per online device the
// pets it has, its free slots, "Send to device", "Use", "Remove" and the
// answer to the last command. Previews are SVG built from the pack's numbers;
// pet names only ever go in as text.

import {
  PET_MOODS,
  parsePet,
  petResultText,
  renderPetSvg,
  type Device,
  type DevicePetCommandRequest,
  type PetDef,
  type PetListEntry,
  type PetMood,
} from "@devotchi/shared";
import { h, placeAt, setText } from "../dom.js";
import { relativeTime } from "../format.js";
import type { State } from "../store.js";

export interface PetActions {
  loadPack(id: string): Promise<Uint8Array>;
  upload(file: File): Promise<void>;
  remove(id: string): Promise<void>;
  command(deviceId: string, cmd: DevicePetCommandRequest): Promise<void>;
}

const CYCLE_MS = 700;
const CONFIRM_MS = 4000;
const PREVIEW_W = 132;
const PREVIEW_H = 114;

interface CardView {
  root: HTMLElement;
  preview: HTMLButtonElement;
  mood: HTMLElement;
  name: HTMLElement;
  meta: HTMLElement;
  tag: HTMLElement;
  device: HTMLSelectElement;
  send: HTMLButtonElement;
  use: HTMLButtonElement;
  remove: HTMLButtonElement;
  /** The mood shown when the card is not being previewed. */
  ownMood: PetMood | null;
  cycle: number | null;
  drawn: string;
  confirmUntil: number;
}

interface DeviceView {
  root: HTMLElement;
  title: HTMLElement;
  list: HTMLElement;
  result: HTMLElement;
  key: string;
}

const svgParser = typeof DOMParser === "undefined" ? null : new DOMParser();

/** Builds the preview node; the markup comes from renderPetSvg, which emits numbers and fixed names only. */
function svgNode(markup: string): Node {
  const doc = svgParser?.parseFromString(markup, "image/svg+xml");
  return doc?.documentElement ? document.importNode(doc.documentElement, true) : document.createTextNode("");
}

export class PetsPanel {
  private readonly cards = new Map<string, CardView>();
  private readonly devices = new Map<string, DeviceView>();
  /** Decoded packs by id and CRC, so a changed pack is fetched again. */
  private readonly defs = new Map<string, PetDef | "loading" | "failed">();
  private mood: PetMood = "idle";
  private state: State | null = null;
  private pending = false;

  constructor(
    private readonly root: { gallery: HTMLElement; empty: HTMLElement; devices: HTMLElement; moodSelect: HTMLSelectElement; file: HTMLInputElement; error: HTMLElement },
    private readonly actions: PetActions,
  ) {
    root.moodSelect.replaceChildren(...PET_MOODS.map((m) => h("option", { value: m, selected: m === this.mood }, m)));
    root.moodSelect.addEventListener("change", () => {
      this.mood = root.moodSelect.value as PetMood;
      for (const card of this.cards.values()) card.ownMood = null;
      this.redraw();
    });
    root.file.addEventListener("change", () => {
      const file = root.file.files?.[0];
      if (file === undefined) return;
      this.run(() => this.actions.upload(file), () => (root.file.value = ""));
    });
  }

  update(state: State, now: number): void {
    this.state = state;
    const pets = state.pets ?? [];
    const ids = new Set(pets.map((p) => p.id));
    for (const [id, view] of this.cards) {
      if (!ids.has(id)) {
        this.stopCycle(view);
        view.root.remove();
        this.cards.delete(id);
      }
    }
    const targets = state.devices.filter((d) => d.online && d.pets !== undefined);
    for (const [index, pet] of pets.entries()) {
      let view = this.cards.get(pet.id);
      if (view === undefined) {
        view = this.buildCard(pet.id);
        this.cards.set(pet.id, view);
      }
      placeAt(this.root.gallery, view.root, index);
      this.fillCard(view, pet, targets, now);
    }
    this.root.empty.hidden = pets.length > 0;
    this.root.empty.textContent = state.pets === null ? "Loading pets…" : "No pets.";
    this.updateDevices(state, targets, now);
  }

  // --- gallery ---

  private buildCard(id: string): CardView {
    const preview = h("button", { type: "button", class: "pet-preview", "aria-label": `Preview ${id}: next mood` });
    const mood = h("span", { class: "pet-mood meta" });
    const name = h("strong", { class: "pet-name" });
    const meta = h("span", { class: "meta" });
    const tag = h("span", { class: "tag" });
    const device = h("select", { "aria-label": `Device for ${id}` });
    const send = h("button", { type: "button" }, "Send to device");
    const use = h("button", { type: "button" }, "Use");
    const remove = h("button", { type: "button", class: "danger" }, "Delete");
    const root = h(
      "figure",
      { class: "pet-card", "data-id": id },
      preview,
      h("figcaption", {}, h("div", { class: "row" }, name, tag), meta, mood),
      h("div", { class: "row pet-actions" }, device, send, use, remove),
    );
    const view: CardView = { root, preview, mood, name, meta, tag, device, send, use, remove, ownMood: null, cycle: null, drawn: "", confirmUntil: 0 };
    preview.addEventListener("click", () => {
      const current = view.ownMood ?? this.mood;
      view.ownMood = PET_MOODS[(PET_MOODS.indexOf(current) + 1) % PET_MOODS.length];
      this.drawCard(view, id);
    });
    preview.addEventListener("mouseenter", () => {
      this.stopCycle(view);
      view.cycle = window.setInterval(() => {
        const current = view.ownMood ?? this.mood;
        view.ownMood = PET_MOODS[(PET_MOODS.indexOf(current) + 1) % PET_MOODS.length];
        this.drawCard(view, id);
      }, CYCLE_MS);
    });
    preview.addEventListener("mouseleave", () => {
      this.stopCycle(view);
      view.ownMood = null;
      this.drawCard(view, id);
    });
    send.addEventListener("click", () => {
      const deviceId = device.value;
      if (deviceId !== "") this.run(() => this.actions.command(deviceId, { op: "install", pet: id, select: true }));
    });
    use.addEventListener("click", () => {
      const deviceId = device.value;
      if (deviceId !== "") this.run(() => this.actions.command(deviceId, { op: "select", pet: id }));
    });
    remove.addEventListener("click", () => {
      const now = Date.now();
      if (now > view.confirmUntil) {
        view.confirmUntil = now + CONFIRM_MS;
        setText(remove, "Click again to delete");
        window.setTimeout(() => setText(remove, "Delete"), CONFIRM_MS);
        return;
      }
      view.confirmUntil = 0;
      this.run(() => this.actions.remove(id));
    });
    return view;
  }

  private fillCard(view: CardView, pet: PetListEntry, targets: Device[], now: number): void {
    setText(view.name, pet.name);
    setText(view.meta, `${pet.id} · rev ${pet.rev} · ${pet.size} bytes · ${pet.source === "catalog" ? "catalog" : "uploaded"}`);
    const builtin = pet.builtin === true || targets.some((d) => d.pets?.installed.some((p) => p.id === pet.id && p.builtin === true));
    setText(view.tag, builtin ? "built in" : pet.source === "room" ? "this room" : "");
    view.tag.hidden = view.tag.textContent === "";
    view.remove.hidden = pet.source !== "room";
    if (now > view.confirmUntil && view.remove.textContent !== "Delete") setText(view.remove, "Delete");

    const chosen = view.device.value;
    const options = targets.map((d) => `${d.id}\u0000${d.name}`).join("\u0001");
    if (view.device.dataset.options !== options) {
      view.device.dataset.options = options;
      view.device.replaceChildren(
        ...(targets.length === 0 ? [h("option", { value: "" }, "no device online")] : targets.map((d) => h("option", { value: d.id, selected: d.id === chosen }, d.name))),
      );
    }
    const target = targets.find((d) => d.id === view.device.value);
    const has = target?.pets?.installed.find((p) => p.id === pet.id);
    view.device.disabled = targets.length === 0;
    view.send.disabled = target === undefined || has?.builtin === true || (has !== undefined && has.rev >= pet.rev);
    view.send.title = has?.builtin === true ? "built into the device" : has !== undefined && has.rev >= pet.rev ? "already on the device" : "";
    view.use.disabled = target === undefined || has === undefined || target.pets?.selected === pet.id;
    this.loadDef(pet);
    this.drawCard(view, pet.id);
  }

  private key(pet: PetListEntry): string {
    return `${pet.id}:${pet.crc32}`;
  }

  private loadDef(pet: PetListEntry): void {
    const key = this.key(pet);
    if (this.defs.has(key)) return;
    this.defs.set(key, "loading");
    this.actions
      .loadPack(pet.id)
      .then((bytes) => this.defs.set(key, parsePet(bytes)))
      .catch(() => this.defs.set(key, "failed"))
      .finally(() => this.redraw());
  }

  private drawCard(view: CardView, id: string): void {
    const pet = this.state?.pets?.find((p) => p.id === id);
    if (pet === undefined) return;
    const mood = view.ownMood ?? this.mood;
    const def = this.defs.get(this.key(pet));
    const drawn = `${this.key(pet)}:${mood}:${typeof def === "object" ? "ok" : String(def)}`;
    if (view.drawn === drawn) return;
    view.drawn = drawn;
    setText(view.mood, mood);
    if (typeof def === "object") {
      view.preview.replaceChildren(svgNode(renderPetSvg(def, mood, { width: PREVIEW_W, height: PREVIEW_H, idPrefix: `pv-${id}` })));
    } else {
      view.preview.replaceChildren(h("span", { class: "meta" }, def === "failed" ? "preview unavailable" : "loading…"));
    }
  }

  private redraw(): void {
    for (const [id, view] of this.cards) this.drawCard(view, id);
  }

  private stopCycle(view: CardView): void {
    if (view.cycle !== null) window.clearInterval(view.cycle);
    view.cycle = null;
  }

  // --- devices ---

  private updateDevices(state: State, targets: Device[], now: number): void {
    const ids = new Set(targets.map((d) => d.id));
    for (const [id, view] of this.devices) {
      if (!ids.has(id)) {
        view.root.remove();
        this.devices.delete(id);
      }
    }
    for (const [index, device] of targets.entries()) {
      let view = this.devices.get(device.id);
      if (view === undefined) {
        view = this.buildDevice();
        this.devices.set(device.id, view);
      }
      placeAt(this.root.devices, view.root, index);
      this.fillDevice(view, device, state, now);
    }
    if (targets.length === 0 && this.root.devices.children.length === 0) {
      this.root.devices.append(h("li", { class: "empty pet-devices-empty" }, "No online device reports pets."));
    } else if (targets.length > 0) {
      for (const el of [...this.root.devices.querySelectorAll(".pet-devices-empty")]) el.remove();
    }
  }

  private buildDevice(): DeviceView {
    const title = h("div", { class: "pet-device-title" });
    const list = h("ul", { class: "pet-installed" });
    const result = h("p", { class: "meta", role: "status" });
    return { root: h("li", { class: "pet-device" }, title, list, result), title, list, result, key: "" };
  }

  private fillDevice(view: DeviceView, device: Device, state: State, now: number): void {
    const pets = device.pets!;
    const names = new Map((state.pets ?? []).map((p) => [p.id, p.name]));
    const shown = names.get(pets.selected) ?? pets.selected;
    setText(view.title, `${device.name}: shows ${shown} · ${pets.free} of ${pets.slots} slots free`);
    const key = JSON.stringify(pets);
    if (view.key !== key) {
      view.key = key;
      view.list.replaceChildren(
        ...pets.installed.map((p) => {
          const use = h("button", { type: "button", disabled: p.id === pets.selected }, p.id === pets.selected ? "Shown" : "Use");
          use.addEventListener("click", () => this.run(() => this.actions.command(device.id, { op: "select", pet: p.id })));
          const remove = h("button", { type: "button", class: "danger" }, "Remove");
          remove.hidden = p.builtin === true;
          remove.addEventListener("click", () => this.run(() => this.actions.command(device.id, { op: "remove", pet: p.id })));
          return h(
            "li",
            {},
            h("span", {}, names.get(p.id) ?? p.id),
            h("span", { class: "meta" }, p.builtin === true ? " built in" : ` rev ${p.rev}`),
            use,
            remove,
          );
        }),
      );
    }
    const last = state.petResults[device.id];
    setText(view.result, last === undefined ? "" : `Last command: ${petResultText(last.result)} (${relativeTime(new Date(last.at).toISOString(), now)})`);
    view.result.classList.toggle("error", last !== undefined && !last.result.ok);
  }

  private run(action: () => Promise<void>, after?: () => void): void {
    if (this.pending) return;
    this.pending = true;
    setText(this.root.error, "");
    action()
      .catch((err: unknown) => setText(this.root.error, err instanceof Error ? err.message : String(err)))
      .finally(() => {
        this.pending = false;
        after?.();
      });
  }
}
