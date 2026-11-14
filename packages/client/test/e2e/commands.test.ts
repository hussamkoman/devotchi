// CLI commands end to end against a real server and simulated devices.

import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import { afterEach, beforeAll, describe, expect, it } from "vitest";
import { delay, type Json } from "../../../server/test/helpers.js";
import { ALLOW, hookInput, permissionInput } from "../../../server/test/hook-helpers.js";
import { CLI_PATH, requireBuiltCli, until } from "../helpers.js";
import { makeWorld, runCliWithInput, spawnSim, startSim, type World } from "./helpers.js";

let world: World | null = null;
let mcp: Client | null = null;

beforeAll(() => requireBuiltCli());

afterEach(async () => {
  await mcp?.close();
  mcp = null;
  await world?.close();
  world = null;
});

function deviceSim(w: World, name: string, extra: Parameters<typeof startSim>[1] = {}) {
  const device = w.t.deviceToken(w.roomId, `sim-${name}`, name);
  return startSim(w, { name, token: device.token, ...extra });
}

describe("status, devices and doctor", () => {
  it("show device liveness, and lastAck moves after a request", async () => {
    world = await makeWorld();
    await deviceSim(world, "desk", { battery: [{ pct: 82, state: "discharging" }] });

    const before = JSON.parse((await world.cli(["status", "--json"])).stdout) as Json;
    expect(before).toMatchObject({
      server: { proto: 2, mode: "single-user" },
      token: { valid: true, roomId: world.roomId },
      room: { mode: "ask" },
      lastRequest: null,
    });
    const [device] = before.devices as Json[];
    expect(device).toMatchObject({ id: "sim-desk", online: true, lastAckSecAgo: null, battery: { state: "discharging", pct: 82 } });

    expect(await world.permission()).toEqual(ALLOW);
    const after = JSON.parse((await world.cli(["status", "--json"])).stdout) as Json;
    expect((after.devices as Json[])[0]).toMatchObject({ lastAckSecAgo: 0 });
    expect(after.lastRequest).toMatchObject({ outcome: "allow", detail: "answered", by: "sim-desk" });

    const text = await world.cli(["status"]);
    expect(text.code).toBe(0);
    expect(text.stdout).toMatch(/Last request: Bash: allow \(answered\) by sim-desk/);
    expect(text.stdout).toMatch(/sim-desk "desk": online, last seen \d+ s ago, last ack \d+ s ago, holding 0, battery 82% discharging/);

    const devices = await world.cli(["devices"]);
    expect(devices.stdout).toMatch(/sim-desk "desk": online, .*last ack \d+ s ago/);
    const json = JSON.parse((await world.cli(["devices", "list", "--json"])).stdout) as { devices: Json[] };
    expect(json.devices[0]).toMatchObject({ id: "sim-desk", online: true, holding: 0 });
    expect(json.devices[0]!.lastHoldAt).not.toBeNull();
  });

  it("doctor passes with hooks installed and gives the hold-time advice for count 0 and 1", async () => {
    world = await makeWorld();
    const s = await deviceSim(world, "desk", { policy: { kind: "manual" } });
    const setup = await world.cli(["setup", "--dir", world.box.dir]);
    expect(setup.code, setup.stderr).toBe(0);
    const check = await world.cli(["setup", "--check", "--dir", world.box.dir]);
    expect(check.code, check.stdout).toBe(0);
    expect(check.stdout).toMatch(/up to date/);

    const first = await world.cli(["doctor", "--dir", world.box.dir]);
    expect(first.code, first.stdout).toBe(0);
    for (const id of ["config", "health", "token", "websocket", "hooks", "devices"]) {
      expect(first.stdout).toMatch(new RegExp(`^PASS  ${id}`, "m"));
    }
    expect(first.stdout).toMatch(/last ack never/);
    expect(first.stdout).toMatch(/INFO {2}hold\s+No sign that Claude Code shows its own prompt .*hold time 30 s/);

    // A permission_prompt notification while the request is pending is the evidence the server counts.
    const reply = world.permission({ session_id: "diag" });
    await s.next("tx", (e) => (e.msg as Json).type === "holding");
    expect(await world.hook(hookInput("Notification", { session_id: "diag", notification_type: "permission_prompt", message: "x" }))).toEqual({});
    expect(await world.hook(hookInput("Stop", { session_id: "diag" }))).toEqual({});
    expect(await reply).toEqual({});

    const second = await world.cli(["doctor", "--dir", world.box.dir, "--json"]);
    const checks = (JSON.parse(second.stdout) as { ok: boolean; checks: Json[] }).checks;
    const hold = checks.find((c) => c.id === "hold")!;
    expect(hold.message).toMatch(/seen showing its prompt while a request was pending \(1 time, last /);
    expect(hold.fix).toMatch(/devotchi hold <seconds>/);
    const devices = checks.find((c) => c.id === "devices")!;
    expect(devices.message).toMatch(/last ack \d+ s ago/);
  });

  it("doctor fails without writing anything when the server is down", async () => {
    world = await makeWorld();
    const before = await world.cli(["config"]);
    const port = new URL(world.t.url).port;
    await world.t.server.close();
    const result = await world.cli(["doctor", "--server", `http://127.0.0.1:${port}`]);
    expect(result.code).toBe(1);
    expect(result.stdout).toMatch(/^FAIL {2}health/m);
    expect((await world.cli(["config"])).stdout).toBe(before.stdout);
  });
});

describe("device management", () => {
  it("devices rm revokes the sim's token: it exits with code 3", async () => {
    world = await makeWorld();
    const device = world.t.deviceToken(world.roomId, "sim-gone", "gone");
    const sim = spawnSim(world, ["--token", device.token, "--name", "gone"]);
    await sim.next("registered");
    expect((await world.cli(["devices", "rename", "sim-gone", "Old desk"])).stdout).toMatch(/Renamed sim-gone to "Old desk"/);
    expect((await world.cli(["devices", "identify", "sim-gone"])).code).toBe(0);
    await sim.next("identify");
    expect((await world.cli(["devices", "rm", "sim-gone"])).code).toBe(2);
    expect((await world.cli(["devices", "rm", "sim-gone", "--yes"])).code).toBe(0);
    await sim.next("closed", (e) => e.code === 4003);
    expect(await sim.exited).toBe(3);
    const missing = await world.cli(["devices", "identify", "sim-gone"]);
    expect(missing.code).toBe(1);
    expect(missing.stderr).toMatch(/no device sim-gone/);
  });

  it("pair --new creates a code the sim claims (flow B); a bad code fails clearly", async () => {
    world = await makeWorld();
    const created = JSON.parse((await world.cli(["pair", "--new", "--json"])).stdout) as { code: string; expiresInS: number };
    expect(created.code).toMatch(/^[A-Z2-9]{6}$/);
    const s = await startSim(world, { name: "claim", code: created.code, token: undefined });
    expect(s.events.find((e) => e.event === "paired")).toMatchObject({ deviceId: "sim-claim", roomId: world.roomId });
    expect(await world.permission()).toEqual(ALLOW);
    const bad = await world.cli(["pair", "ABCDEF"]);
    expect(bad.code).toBe(1);
    expect(bad.stderr).toMatch(/no device is showing code ABC DEF/);
    expect((await world.cli(["config"])).stdout).not.toMatch(new RegExp(created.code));
  });
});

describe("notify and run", () => {
  it("notify reaches the sim and reports the delivered count", async () => {
    world = await makeWorld();
    const s = await startSim(world);
    const result = await world.cli(["notify", "deploy_complete", "v2 is live", "--mood", "excited", "--urgent", "--source", "ci"]);
    expect(result.stdout).toMatch(/Sent deploy_complete to 1 consumer/);
    const rx = await s.next("rx", (e) => (e.msg as Json).type === "notify");
    expect(rx.msg).toMatchObject({ event: "deploy_complete", detail: "v2 is live", mood: "excited", urgency: 2, source: "ci" });
    expect((await world.cli(["notify", "Bad-Event"])).code).toBe(2);
  });

  it("run propagates the exit code and notifies <label>_failed / _succeeded", async () => {
    world = await makeWorld();
    const s = await startSim(world);
    const failed = await world.cli(["run", "--name", "unit tests", "--", process.execPath, "-e", "process.exit(3)"]);
    expect(failed.code).toBe(3);
    const fail = await s.next("rx", (e) => (e.msg as Json).event === "unit_tests_failed");
    expect((fail.msg as Json).detail).toMatch(/^exit 3 after 0m0\ds$/);

    const ok = await world.cli(["run", "--", process.execPath, "-e", "console.log('hi')"]);
    expect(ok.code).toBe(0);
    expect(ok.stdout).toBe("hi\n");
    await s.next("rx", (e) => (e.msg as Json).event === "node_succeeded");
  });
});

describe("MCP with a sim", () => {
  it("show_status reaches the sim (delivered), and set_mood happy survives a PostToolUse hook", async () => {
    world = await makeWorld();
    const s = await startSim(world);
    const transport = new StdioClientTransport({
      command: process.execPath,
      args: [CLI_PATH, "mcp"],
      env: world.box.env as Record<string, string>,
      stderr: "pipe",
    });
    let stderr = "";
    transport.stderr?.on("data", (chunk: Buffer) => (stderr += chunk.toString()));
    mcp = new Client({ name: "e2e", version: "0.0.0" });
    await mcp.connect(transport);
    await until(() => stderr.includes("registered with"), 5000, "the MCP producer");
    const call = async (name: string, args: Json) => {
      const result = (await mcp!.callTool({ name, arguments: args })) as { content: { text: string }[] };
      return JSON.parse(result.content[0]!.text) as Json;
    };

    expect(await call("show_status", { msg: "Running tests" })).toMatchObject({ ok: true, delivered: true, consumers: 1 });
    await s.next("rx", (e) => (e.msg as Json).type === "status" && (e.msg as Json).msg === "Running tests");
    expect(await call("set_mood", { mood: "happy" })).toMatchObject({ delivered: true });
    await until(() => s.sim.mood === "happy", 2000, "mood happy");

    expect(await world.hook(hookInput("PostToolUse", { tool_name: "Edit", tool_input: { file_path: "/x/a.ts" }, tool_response: {} }))).toEqual({});
    const status = await s.next("rx", (e) => (e.msg as Json).type === "status" && /a\.ts/.test(String((e.msg as Json).msg)));
    expect((status.msg as Json).mood).toBeUndefined();
    await delay(100);
    expect(s.sim.mood).toBe("happy");
  });
});

describe("multi-user", () => {
  it("login -> token -> pair -> approve", async () => {
    world = await makeWorld({ storeToken: false, server: { mode: "multi-user", bcryptCost: 4 } });
    const reg = await world.t.fetch("/api/auth/register", {
      method: "POST",
      json: { email: "dev@example.com", password: "correct horse battery" },
    });
    expect(reg.status).toBe(201);

    const noLogin = await world.cli(["status"]);
    expect(noLogin.code).toBe(1);
    expect(noLogin.stderr).toMatch(/devotchi login/);

    const wrong = await runCliWithInput(world, ["login", "--email", "dev@example.com"], "wrong password\n");
    expect(wrong.code).toBe(1);
    expect(wrong.stderr).toMatch(/wrong email or password/);
    const login = await runCliWithInput(world, ["login", "--email", "dev@example.com"], "correct horse battery\n");
    expect(login.code, login.stderr).toBe(0);
    expect(login.stdout).toMatch(/Logged in/);

    const created = JSON.parse((await world.cli(["pair", "--new", "--json"])).stdout) as { code: string };
    const s = await startSim(world, { name: "mu", code: created.code, token: undefined });
    const roomId = s.events.find((e) => e.event === "paired")!.roomId as string;
    const status = JSON.parse((await world.cli(["status", "--json"])).stdout) as Json;
    expect(status).toMatchObject({ token: { roomId, kind: "token" }, server: { mode: "multi-user" } });

    const config = JSON.parse((await world.cli(["config"])).stdout) as { servers: Record<string, { token: string }> };
    expect(Object.keys(config.servers)).toEqual([world.t.url]);
    const { readFile } = await import("node:fs/promises");
    const raw = JSON.parse(await readFile(`${world.box.configDir}/config.json`, "utf8")) as { servers: Record<string, { token: string }> };
    world.token = raw.servers[world.t.url]!.token;
    expect(world.token).not.toMatch(/^dvs_/);
    expect(await world.hook(permissionInput())).toEqual(ALLOW);

    const out = await world.cli(["logout", "--revoke"]);
    expect(out.stdout).toMatch(/Revoked the token/);
    expect((await world.t.fetch("/api/me", { token: world.token })).status).toBe(401);
  });
});
