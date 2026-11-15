// @vitest-environment happy-dom
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it, vi } from "vitest";
import type { Device, DevicePetCommandRequest, PetListEntry } from "@devotchi/shared";
import { initialState, petResultText, reduce, type State } from "../../web/src/store.js";
import { PetsPanel } from "../../web/src/views/pets.js";

// happy-dom replaces URL, so the path is built with node:path.
const repo = join(dirname(fileURLToPath(import.meta.url)), "../../../..");
const golden = (id: string) => new Uint8Array(readFileSync(join(repo, "pets/golden", `${id}.pet`)));

const PETS: PetListEntry[] = [
  { id: "devo", name: "Devo", rev: 1, size: 344, crc32: "6cd52e21", source: "catalog" },
  { id: "ember", name: "Ember", rev: 1, size: 546, crc32: "8f684d5d", source: "catalog" },
  { id: "evil", name: "<img src=x onerror=alert(1)>", rev: 1, size: 344, crc32: "00000000", source: "room" },
];

const DEVICE: Device = {
  id: "dv-0a1b2c",
  name: "desk",
  model: null,
  fw: null,
  createdAt: new Date(0).toISOString(),
  online: true,
  lastSeen: null,
  lastHoldAt: null,
  holding: 0,
  telemetry: null,
  telemetryAt: null,
  pets: {
    selected: "devo",
    slots: 8,
    free: 7,
    installed: [
      { id: "devo", rev: 1, builtin: true },
      { id: "ember", rev: 1, builtin: true },
      { id: "evil", rev: 1 },
    ],
  },
};

function withPets(): State {
  let state = reduce(initialState(), { type: "pets", pets: PETS });
  state = reduce(state, { type: "devices", devices: [DEVICE], now: 0 });
  return state;
}

describe("pets in the store", () => {
  it("keeps a device's pets message and its last result, with a toast and a log line", () => {
    const state = reduce(withPets(), {
      type: "frame",
      now: 1000,
      msg: {
        type: "pets",
        device: "dv-0a1b2c",
        selected: "devo",
        slots: 8,
        free: 8,
        installed: [{ id: "devo", rev: 1, builtin: true }],
        result: { op: "install", pet: "mochi", ok: false, error: "crc" },
      },
    });
    expect(state.devices[0].pets).toEqual({ selected: "devo", slots: 8, free: 8, installed: [{ id: "devo", rev: 1, builtin: true }] });
    expect(state.petResults["dv-0a1b2c"]).toEqual({ result: { op: "install", pet: "mochi", ok: false, error: "crc" }, at: 1000 });
    expect(state.toasts.at(-1)?.text).toBe("desk: could not install mochi: the pack was damaged on the way (CRC)");
    expect(state.log[0].kind).toBe("pets");
    expect(petResultText({ op: "select", pet: "ember", ok: true })).toBe("now shows ember");
  });
});

describe("pets page", () => {
  function mount(actions: Partial<ConstructorParameters<typeof PetsPanel>[1]> = {}) {
    const el = (tag: string) => document.body.appendChild(document.createElement(tag));
    const root = {
      gallery: el("div"),
      empty: el("p"),
      devices: el("ul"),
      moodSelect: el("select") as HTMLSelectElement,
      file: el("input") as HTMLInputElement,
      error: el("p"),
    };
    const commands: [string, DevicePetCommandRequest][] = [];
    const panel = new PetsPanel(root, {
      loadPack: async (id) => golden(id === "evil" ? "devo" : id),
      upload: async () => undefined,
      remove: async () => undefined,
      command: async (deviceId, cmd) => {
        commands.push([deviceId, cmd]);
      },
      ...actions,
    });
    return { root, panel, commands };
  }

  it("shows every pet with an SVG preview, its name as text, and marks built-in pets", async () => {
    const { root, panel } = mount();
    panel.update(withPets(), 0);
    await vi.waitFor(() => expect(root.gallery.querySelectorAll("svg").length).toBe(3));
    const cards = [...root.gallery.querySelectorAll(".pet-card")];
    expect(cards.map((c) => c.getAttribute("data-id"))).toEqual(["devo", "ember", "evil"]);
    expect(cards[2].querySelector(".pet-name")?.textContent).toBe("<img src=x onerror=alert(1)>");
    expect(root.gallery.querySelector("img")).toBeNull();
    expect(cards[0].querySelector(".tag")?.textContent).toBe("built in");
    expect((cards[0].querySelector("button.danger") as HTMLElement).hidden).toBe(true);
    expect((cards[2].querySelector("button.danger") as HTMLElement).hidden).toBe(false);
    expect(root.devices.textContent).toContain("desk: shows Devo · 7 of 8 slots free");
  });

  it("previews a mood on click and sends install, select and remove", async () => {
    const { root, panel, commands } = mount();
    panel.update(withPets(), 0);
    await vi.waitFor(() => expect(root.gallery.querySelectorAll("svg").length).toBe(3));
    const devo = root.gallery.querySelector('[data-id="devo"]')!;
    const before = devo.querySelector(".pet-preview")!.innerHTML;
    (devo.querySelector(".pet-preview") as HTMLButtonElement).click();
    expect(devo.querySelector(".pet-mood")?.textContent).toBe("focused");
    expect(devo.querySelector(".pet-preview")!.innerHTML).not.toBe(before);

    const sendDevo = [...devo.querySelectorAll("button")].find((b) => b.textContent === "Send to device") as HTMLButtonElement;
    expect(sendDevo.disabled).toBe(true);
    const evil = root.gallery.querySelector('[data-id="evil"]')!;
    const useEvil = [...evil.querySelectorAll("button")].find((b) => b.textContent === "Use") as HTMLButtonElement;
    useEvil.click();
    await vi.waitFor(() => expect(commands).toEqual([["dv-0a1b2c", { op: "select", pet: "evil" }]]));
    const removeOnDevice = [...root.devices.querySelectorAll("button")].filter((b) => b.textContent === "Remove");
    expect(removeOnDevice.map((b) => b.hidden)).toEqual([true, true, false]);
    // One command at a time: the panel ignores clicks until the previous request settled.
    await new Promise((resolve) => setTimeout(resolve, 0));
    removeOnDevice[2].click();
    await vi.waitFor(() => expect(commands.at(-1)).toEqual(["dv-0a1b2c", { op: "remove", pet: "evil" }]));
  });
});
