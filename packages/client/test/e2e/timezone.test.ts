// Protocol 14.9: `devotchi start` / `devotchi pair` give the room the PC's
// zone only while the room has none.

import { afterEach, describe, expect, it } from "vitest";
import type { Room, RoomSettings } from "@devotchi/shared";
import { adoptTimeZone } from "../../src/timezone.js";
import { makeWorld, type World } from "./helpers.js";

let world: World | null = null;

afterEach(async () => {
  await world?.close();
  world = null;
});

async function zoneOf(w: World): Promise<string> {
  const room = (await (await w.t.fetch("/api/room", { token: w.token })).json()) as Room;
  return room.settings.timeZone;
}

describe("room time zone from the PC", () => {
  it("pair sets it once and never overwrites a chosen zone", async () => {
    world = await makeWorld();
    expect(await zoneOf(world)).toBe("");
    world.box.env.TZ = "Asia/Tokyo";
    const first = await world.cli(["pair", "--new"]);
    expect(first.code, first.stderr).toBe(0);
    expect(first.stdout).toContain("Room time zone: Asia/Tokyo (from this PC)");
    expect(await zoneOf(world)).toBe("Asia/Tokyo");

    world.box.env.TZ = "Europe/Berlin";
    const second = await world.cli(["pair", "--new"]);
    expect(second.code, second.stderr).toBe(0);
    expect(second.stdout).not.toContain("Room time zone");
    expect(await zoneOf(world)).toBe("Asia/Tokyo");

    // --json output stays pure JSON.
    await world.t.fetch("/api/room/settings", { method: "PUT", token: world.token, json: { timeZone: "" } });
    const json = await world.cli(["pair", "--new", "--json"]);
    expect(() => JSON.parse(json.stdout) as unknown).not.toThrow();
    expect(await zoneOf(world)).toBe("Europe/Berlin");
  });

  it("skips zones the runtime does not know and servers that already have one", async () => {
    const calls: unknown[] = [];
    const api = (timeZone: string) => ({
      room: async () => ({ settings: { timeZone } }) as Room,
      updateRoomSettings: async (changes: Partial<RoomSettings>) => {
        calls.push(changes);
        return { mode: "ask", holdMs: 30_000, shareText: false, timeZone: changes.timeZone ?? "" } satisfies RoomSettings;
      },
    });
    expect(await adoptTimeZone(api(""), "Not/AZone")).toBeNull();
    expect(await adoptTimeZone(api("Europe/Paris"), "Asia/Tokyo")).toBeNull();
    expect(calls).toEqual([]);
    expect(await adoptTimeZone(api(""), "asia/tokyo")).toBe("Asia/Tokyo");
    expect(calls).toEqual([{ timeZone: "Asia/Tokyo" }]);
  });
});
