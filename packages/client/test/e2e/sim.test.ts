// Device-side behaviour of the sim that the request tests do not cover.

import { join } from "node:path";
import { readFile } from "node:fs/promises";
import { afterEach, describe, expect, it } from "vitest";
import { producer, type Json } from "../../../server/test/helpers.js";
import { until } from "../helpers.js";
import { SIM_QUEUE_SIZE } from "../../src/sim.js";
import { makeWorld, startSim, type World } from "./helpers.js";

let world: World | null = null;

afterEach(async () => {
  await world?.close();
  world = null;
});

describe("devotchi sim", () => {
  it("a status without mood keeps the mood; one with a mood changes it", async () => {
    world = await makeWorld();
    const s = await startSim(world);
    const p = await producer(world.t, world.token);
    p.send({ type: "mood", mood: "happy" });
    await until(() => s.sim.mood === "happy", 2000, "happy");
    p.send({ type: "status", msg: "Reading code" });
    await until(() => s.sim.status === "Reading code", 2000, "status");
    expect(s.sim.mood).toBe("happy");
    p.send({ type: "status", msg: "Fixing", mood: "focused" });
    await until(() => s.sim.mood === "focused", 2000, "focused");
    p.close();
  });

  it("sends scripted telemetry on register and every interval", async () => {
    world = await makeWorld();
    const device = world.t.deviceToken(world.roomId, "sim-batt", "batt");
    await startSim(world, {
      name: "batt",
      token: device.token,
      telemetryIntervalMs: 200,
      battery: [{ pct: 50, state: "discharging" }, { pct: 8, state: "discharging" }],
    });
    const telemetry = async () => {
      const res = await world!.t.fetch("/api/devices", { token: world!.token });
      return ((await res.json()) as { devices: Json[] }).devices[0]!.telemetry as Json | null;
    };
    await until(async () => ((await telemetry())?.battery as Json | undefined)?.pct === 8, 3000, "second sample");
    expect(await telemetry()).toMatchObject({ power: "battery", alert: "battery_low", fw: expect.any(String) });
  });

  it(`keeps at most ${SIM_QUEUE_SIZE} requests and does not name the ones it cannot show`, async () => {
    world = await makeWorld();
    const s = await startSim(world, { policy: { kind: "manual" } });
    const replies = [1, 2, 3, 4, 5].map((n) => world!.permission({ tool_input: { command: `echo ${n}` } }));
    await until(() => s.events.filter((e) => e.event === "request").length === 5, 3000, "five requests");
    const dropped = s.events.find((e) => e.event === "request" && e.accepted === false)!;
    expect(dropped.reason).toBe("queue full");
    expect(s.sim.requests).toHaveLength(SIM_QUEUE_SIZE);
    expect(await replies[4]).toEqual({});
    const named = new Set(s.sent("holding").flatMap((h) => h.ids as string[]));
    expect(named.has(String(dropped.id))).toBe(false);
    for (const id of s.sim.requests) expect(s.sim.approve(id)).toBe(true);
    expect((await Promise.all(replies.slice(0, 4))).every((r) => JSON.stringify(r).includes("allow"))).toBe(true);
  });

  it("--repair: after devices rm the sim forgets its state and shows a new pairing code", async () => {
    world = await makeWorld();
    const created = (await (await world.t.fetch("/api/pair", { method: "POST", json: {}, token: world.token })).json()) as { code: string };
    const statePath = join(world.box.dir, "sim.json");
    const s = await startSim(world, { name: "re", code: created.code, statePath, repair: true });
    expect(JSON.parse(await readFile(statePath, "utf8"))).toMatchObject({ deviceId: "sim-re", roomId: world.roomId });
    expect((await world.cli(["devices", "rm", "sim-re", "--yes"])).code).toBe(0);
    await s.next("closed", (e) => e.code === 4003);
    await s.next("pairing");
    expect(JSON.parse(await readFile(statePath, "utf8"))).toEqual({});
  });
});
