// Permission requests end to end: real server, hook POSTs, simulated devices.

import { readFile } from "node:fs/promises";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { delay, type Json } from "../../../server/test/helpers.js";
import { ALLOW, DENY, hookInput, permissionInput } from "../../../server/test/hook-helpers.js";
import { E2E_TIMING, makeWorld, spawnSim, startSim, type World } from "./helpers.js";
import { requireBuiltCli, until } from "../helpers.js";

let world: World | null = null;

afterEach(async () => {
  await world?.close();
  world = null;
});

async function lastRequest(w: World): Promise<Json> {
  const res = await w.t.fetch("/api/room", { token: w.token });
  return ((await res.json()) as { lastRequest: Json }).lastRequest;
}

describe("sim pairing", () => {
  it("flow A: the sim shows a code, devotchi pair confirms it, the device is listed as sim-<name> and answers", async () => {
    requireBuiltCli();
    world = await makeWorld({ storeToken: false });
    const sim = spawnSim(world, ["--name", "desk"]);
    const pairing = await sim.next("pairing");
    expect(pairing.deviceId).toBe("sim-desk");

    const paired = await world.cli(["pair", String(pairing.code), "--name", "Desk"]);
    expect(paired.stderr).toMatch(/provisioned a token/);
    expect(paired.stdout).toMatch(/Paired sim-desk as "Desk"/);
    await sim.next("registered", undefined, 8000);

    const listed = await world.cli(["devices", "--json"]);
    const devices = (JSON.parse(listed.stdout) as { devices: Json[] }).devices;
    expect(devices.map((d) => [d.id, d.name, d.online])).toEqual([["sim-desk", "Desk", true]]);

    // The CLI provisioned its own room on first use; the hooks need that token.
    const config = JSON.parse(await readFile(join(world.box.configDir, "config.json"), "utf8")) as {
      servers: Record<string, { token: string }>;
    };
    expect(Object.keys(config.servers)).toEqual([world.t.url]);
    world.token = config.servers[world.t.url]!.token;
    expect(await world.permission()).toEqual(ALLOW);
    expect(await lastRequest(world)).toMatchObject({ outcome: "allow", detail: "answered", by: "sim-desk" });
    sim.child.kill("SIGTERM");
    expect(await sim.exited).toBe(0);
  });
});

describe("permission requests with a sim", () => {
  it("approve -> allow, reject -> deny, defer -> {}", async () => {
    world = await makeWorld();
    const yes = await startSim(world, { name: "yes", policy: { kind: "all" } });
    expect(await world.permission()).toEqual(ALLOW);
    await yes.sim.stop();

    const no = await startSim(world, { name: "no", policy: { kind: "none" } });
    expect(await world.permission()).toEqual(DENY);
    expect(await lastRequest(world)).toMatchObject({ outcome: "deny", by: "sim-no" });
    await no.sim.stop();

    const later = await startSim(world, { name: "later", policy: { kind: "defer" } });
    expect(await world.permission()).toEqual({});
    expect(await lastRequest(world)).toMatchObject({ outcome: "neutral", detail: "deferred" });
    await later.next("dismiss", (e) => e.reason === "released");
    await later.sim.stop();
  });

  it("after=<ms> approves once the delay has passed", async () => {
    world = await makeWorld();
    await startSim(world, { policy: { kind: "after", ms: 400 } });
    const started = Date.now();
    expect(await world.permission()).toEqual(ALLOW);
    expect(Date.now() - started).toBeGreaterThanOrEqual(380);
  });

  it("--no-ack: {} after the ack timeout with lastRequest no_ack", async () => {
    world = await makeWorld();
    const s = await startSim(world, { policy: { kind: "manual" }, ack: "none" });
    const started = Date.now();
    expect(await world.permission()).toEqual({});
    expect(Date.now() - started).toBeGreaterThanOrEqual(E2E_TIMING.ackMs! - 50);
    expect(await lastRequest(world)).toMatchObject({ outcome: "neutral", detail: "no_ack" });
    expect(s.sent("holding")).toEqual([]);
    await s.next("dismiss", (e) => e.reason === "released");
  });

  it("--stop-heartbeat-after: {} with holder_lost once the heartbeats stop", async () => {
    world = await makeWorld();
    const s = await startSim(world, { policy: { kind: "manual" }, stopHeartbeatAfterMs: 500 });
    const started = Date.now();
    expect(await world.permission()).toEqual({});
    expect(Date.now() - started).toBeGreaterThanOrEqual(500 + 1000);
    expect(await lastRequest(world)).toMatchObject({ outcome: "neutral", detail: "holder_lost" });
    expect(s.sent("holding").length).toBeGreaterThanOrEqual(2);
  });

  it("a sim killed mid-request: {} with holder_lost, and a sim that connects afterwards gets no replay", async () => {
    world = await makeWorld();
    const victim = spawnSim(world, ["--token", world.token, "--name", "victim", "--approve", "manual"]);
    await victim.next("registered");
    const reply = world.permission();
    await victim.next("tx", (e) => (e.msg as Json).type === "holding");
    victim.child.kill("SIGKILL");
    expect(await reply).toEqual({});
    expect(await lastRequest(world)).toMatchObject({ outcome: "neutral", detail: "holder_lost" });

    const fresh = await startSim(world, { name: "fresh", policy: { kind: "manual" } });
    await delay(300);
    expect(fresh.events.filter((e) => e.event === "request")).toEqual([]);
  });

  it("hold timeout: {} and the sim logs dismiss timeout without sending an answer", async () => {
    world = await makeWorld({ timing: { holdMs: 1200 } });
    const s = await startSim(world, { policy: { kind: "manual" } });
    const started = Date.now();
    expect(await world.permission()).toEqual({});
    expect(Date.now() - started).toBeGreaterThanOrEqual(1150);
    await s.next("dismiss", (e) => e.reason === "timeout");
    expect(await lastRequest(world)).toMatchObject({ outcome: "neutral", detail: "timeout" });
    expect(s.sent("permission_response")).toEqual([]);
    expect(s.sent("permission_defer")).toEqual([]);
  });

  for (const ack of ["debounced", "naive"] as const) {
    it(`three concurrent requests are all held and answered (${ack} ack)`, async () => {
      world = await makeWorld();
      const s = await startSim(world, { policy: { kind: "after", ms: 1000 }, ack });
      const replies = await Promise.all(
        [1, 2, 3].map((n) => world!.permission({ tool_input: { command: `echo ${n}` } })),
      );
      expect(replies).toEqual([ALLOW, ALLOW, ALLOW]);
      const room = (await (await world.t.fetch("/api/room", { token: world.token })).json()) as { lastRequest: Json };
      expect(room.lastRequest).toMatchObject({ outcome: "allow", detail: "answered" });
      const holdings = s.sent("holding");
      expect(world.t.logs.join("\n")).not.toMatch(/no_ack/);
      expect(world.t.logs.join("\n")).not.toMatch(/RATE_LIMITED/);
      if (ack === "debounced") expect((holdings[0]!.ids as string[]).length).toBe(3);
      else expect(holdings.slice(0, 3).every((h) => (h.ids as string[]).length === 1)).toBe(true);
    });
  }

  it("two sims: one answers, the other gets dismiss answered and never answers", async () => {
    world = await makeWorld();
    const first = await startSim(world, { name: "one", policy: { kind: "all" } });
    const second = await startSim(world, { name: "two", policy: { kind: "manual" } });
    expect(await world.permission()).toEqual(ALLOW);
    const dismissed = await second.next("dismiss");
    expect(dismissed).toMatchObject({ reason: "answered", outcome: "allow", by: "sim-one" });
    await first.next("dismiss", (e) => e.reason === "answered");
    expect(second.sim.approve(String(dismissed.id))).toBe(false);
    expect(second.sent("permission_response")).toEqual([]);
  });

  it("a second sim connecting mid-request gets the replay while the first still holds", async () => {
    world = await makeWorld();
    const first = await startSim(world, { name: "one", policy: { kind: "manual" } });
    const reply = world.permission();
    const request = await first.next("request");
    await delay(400);
    const second = await startSim(world, { name: "two", policy: { kind: "manual" } });
    const replayed = await second.next("request");
    expect(replayed.id).toBe(request.id);
    expect(replayed.timeout as number).toBeLessThan(request.timeout as number);
    expect(second.sim.approve()).toBe(true);
    expect(await reply).toEqual(ALLOW);
    await first.next("dismiss", (e) => e.by === "sim-two");
  });

  it("desk mode: {} at once and the sim receives nothing", async () => {
    world = await makeWorld();
    const s = await startSim(world, { policy: { kind: "all" } });
    expect((await world.cli(["mode", "desk"])).code).toBe(0);
    await s.next("rx", (e) => (e.msg as Json).type === "mode");
    const started = Date.now();
    expect(await world.permission()).toEqual({});
    expect(Date.now() - started).toBeLessThan(500);
    expect(await lastRequest(world)).toMatchObject({ detail: "desk" });
    expect(s.events.filter((e) => e.event === "request")).toEqual([]);
  });
});

describe("terminal answers cancel pending requests", () => {
  const input = { tool_name: "Bash", tool_input: { command: "rm -rf dist" } };
  const cancelling: [string, Json][] = [
    ["PostToolUse with the same tool and input", hookInput("PostToolUse", { ...input, tool_response: {} })],
    ["PostToolBatch", hookInput("PostToolBatch", {})],
    ["Stop", hookInput("Stop", {})],
    ["UserPromptSubmit", hookInput("UserPromptSubmit", { prompt: "go on" })],
  ];
  for (const [name, body] of cancelling) {
    it(`${name} dismisses the request with cancelled`, async () => {
      world = await makeWorld();
      const s = await startSim(world, { policy: { kind: "manual" } });
      const reply = world.permission(input);
      const request = await s.next("request");
      await s.next("tx", (e) => (e.msg as Json).type === "holding");
      expect(await world.hook(body)).toEqual({});
      const dismissed = await s.next("dismiss");
      expect(dismissed).toMatchObject({ id: request.id, reason: "cancelled" });
      expect(await reply).toEqual({});
      expect(await lastRequest(world)).toMatchObject({ detail: "cancelled" });
    });
  }

  it("an aborted hook request is cancelled too", async () => {
    world = await makeWorld();
    const s = await startSim(world, { policy: { kind: "manual" } });
    const abort = new AbortController();
    const pending = world.post(permissionInput(), { signal: abort.signal }).catch(() => null);
    await s.next("request");
    abort.abort();
    await pending;
    await s.next("dismiss", (e) => e.reason === "cancelled");
    await until(async () => (await lastRequest(world!))?.detail === "cancelled", 3000, "cancelled outcome");
  });
});
