import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { MOODS, buildTelemetry, utf8ByteLength } from "@devotchi/shared";
import { TestSocket, consumer, startTestServer, type Json, type TestServer } from "../../server/test/helpers.js";
import { ConfigStore } from "../src/config.js";
import { CLI_PATH, makeSandbox, requireBuiltCli, unusedPort, until, type Sandbox } from "./helpers.js";

let box: Sandbox;
let t: TestServer | null = null;
let client: Client | null = null;
let stderr = "";

beforeEach(async () => {
  requireBuiltCli();
  box = await makeSandbox();
  stderr = "";
});

afterEach(async () => {
  await client?.close();
  client = null;
  await t?.close();
  t = null;
  await box.cleanup();
});

async function startMcp(server: string): Promise<Client> {
  const transport = new StdioClientTransport({
    command: process.execPath,
    args: [CLI_PATH, "mcp", "--server", server],
    env: box.env as Record<string, string>,
    stderr: "pipe",
  });
  transport.stderr?.on("data", (chunk: Buffer) => (stderr += chunk.toString()));
  const c = new Client({ name: "devotchi-test", version: "0.0.0" });
  await c.connect(transport);
  client = c;
  return c;
}

async function call(c: Client, name: string, args: Json = {}): Promise<Json> {
  const result = (await c.callTool({ name, arguments: args })) as { content: { type: string; text: string }[]; isError?: boolean };
  expect(result.isError, JSON.stringify(result)).toBeFalsy();
  return JSON.parse(result.content[0]!.text) as Json;
}

/** A test server, a room token stored in the sandbox config, and an MCP client registered in that room. */
async function setup(): Promise<{ c: Client; token: string; roomId: string }> {
  t = await startTestServer();
  const minted = t.cliToken("test");
  await new ConfigStore(box.env).setCredentials(t.url, { token: minted.token, roomId: minted.roomId });
  const c = await startMcp(t.url);
  await until(() => stderr.includes("registered with"), 5000, "the MCP producer to register");
  return { c, token: minted.token, roomId: minted.roomId };
}

describe("devotchi mcp", () => {
  it("answers initialize and tools/list while the server is unreachable", async () => {
    const port = await unusedPort();
    const started = Date.now();
    const c = await startMcp(`http://127.0.0.1:${port}`);
    expect(Date.now() - started).toBeLessThan(5000);
    expect(c.getServerVersion()).toMatchObject({ name: "devotchi" });

    const { tools } = await c.listTools();
    expect(tools.map((tool) => tool.name).sort()).toEqual(["get_device_state", "notify", "set_mood", "show_status"]);
    for (const tool of tools) {
      expect(tool.title, tool.name).toBeTruthy();
      expect(tool.description?.length ?? 0, tool.name).toBeGreaterThan(40);
    }
    const show = tools.find((tool) => tool.name === "show_status")!;
    expect(show.description).toMatch(/keeps its current mood/);
    const moodEnum = (show.inputSchema.properties as Record<string, { enum?: string[] }>).mood!.enum;
    expect(moodEnum).toEqual([...MOODS]);

    const result = await call(c, "show_status", { msg: "offline" });
    expect(result).toMatchObject({ ok: true, delivered: false, consumers: 0, devices: 0 });
    const state = await call(c, "get_device_state");
    expect(state.ok).toBe(false);
    expect(String(state.error)).toMatch(/cannot reach/);
  });

  it("answers initialize with a corrupt config file and reports the problem in tool results", async () => {
    await mkdir(box.configDir, { recursive: true });
    await writeFile(join(box.configDir, "config.json"), "{ nope");
    const c = await startMcp(`http://127.0.0.1:${await unusedPort()}`);
    const result = await call(c, "show_status", { msg: "x" });
    expect(result).toMatchObject({ ok: false, delivered: false });
    expect(String(result.error)).toMatch(/not valid JSON/);
    expect(await readFile(join(box.configDir, "config.json"), "utf8")).toBe("{ nope");
  });

  it("puts nothing but JSON-RPC on stdout and logs to stderr", async () => {
    const port = await unusedPort();
    const c = await startMcp(`http://127.0.0.1:${port}`);
    await call(c, "set_mood", { mood: "happy" });
    await until(() => stderr.includes("ready on stdio"));
    expect(stderr).not.toContain("ExperimentalWarning");
  });

  it("sends each tool's message to a consumer, honouring an omitted mood", async () => {
    const { c, token } = await setup();

    const none = await call(c, "show_status", { msg: "nobody watching" });
    expect(none).toMatchObject({ ok: true, delivered: false, consumers: 0 });

    const sim = await consumer(t!, token);
    // Past the 250 ms status spacing, so the next status is sent rather than queued.
    await new Promise((resolve) => setTimeout(resolve, 300));

    const plain = await call(c, "show_status", { msg: "Reading code" });
    expect(plain).toMatchObject({ ok: true, delivered: true, consumers: 1, devices: 1 });
    const status = await sim.awaitMessage((m) => m.type === "status" && m.msg === "Reading code");
    expect(status).not.toHaveProperty("mood");
    expect(typeof status.timestamp).toBe("string");

    await new Promise((resolve) => setTimeout(resolve, 300));
    await call(c, "show_status", { msg: "Deep work", mood: "focused" });
    const withMood = await sim.awaitMessage((m) => m.type === "status" && m.msg === "Deep work");
    expect(withMood.mood).toBe("focused");

    await call(c, "notify", { event: "tests_failed", mood: "sad", detail: "3 of 47 failing", urgency: 2 });
    const notify = await sim.awaitMessage("notify");
    expect(notify).toMatchObject({ event: "tests_failed", mood: "sad", detail: "3 of 47 failing", urgency: 2 });

    await call(c, "set_mood", { mood: "anxious", reason: "risky migration" });
    expect(await sim.awaitMessage("mood")).toMatchObject({ mood: "anxious", reason: "risky migration" });

    for (const mood of MOODS) {
      const result = await call(c, "set_mood", { mood });
      expect(result, mood).toMatchObject({ ok: true, delivered: true });
      await sim.awaitMessage((m) => m.type === "mood" && m.mood === mood);
    }

    const bad = (await c.callTool({ name: "notify", arguments: { event: "Not Valid!", mood: "happy" } })) as { isError?: boolean };
    expect(bad.isError).toBe(true);
    const badMood = (await c.callTool({ name: "set_mood", arguments: { mood: "ecstatic" } })) as { isError?: boolean };
    expect(badMood.isError).toBe(true);
    sim.close();
  });

  it("truncates oversized and CJK text to the byte limits without losing the socket", async () => {
    const { c, token } = await setup();
    const sim = await consumer(t!, token);
    await new Promise((resolve) => setTimeout(resolve, 100));

    await call(c, "show_status", { msg: "漢字".repeat(400) });
    const status = await sim.awaitMessage("status");
    const msg = String(status.msg);
    expect(utf8ByteLength(msg)).toBeLessThanOrEqual(200);
    expect(msg).toMatch(/^(漢字)+漢?$/u);

    await call(c, "notify", { event: "x".repeat(100), mood: "happy", detail: "😀".repeat(500) });
    const notify = await sim.awaitMessage("notify");
    expect(utf8ByteLength(String(notify.event))).toBe(40);
    expect(utf8ByteLength(String(notify.detail))).toBeLessThanOrEqual(300);
    expect(String(notify.detail)).not.toContain("�");

    await call(c, "set_mood", { mood: "proud", reason: "é".repeat(5000) });
    expect(utf8ByteLength(String((await sim.awaitMessage("mood")).reason))).toBeLessThanOrEqual(200);

    await new Promise((resolve) => setTimeout(resolve, 300));
    const after = await call(c, "show_status", { msg: "still here" });
    expect(after).toMatchObject({ ok: true, delivered: true });
    await sim.awaitMessage((m) => m.msg === "still here");
    expect(stderr).not.toMatch(/server error|MESSAGE_TOO_LARGE|disconnected/);
    sim.close();
  });

  it("paces a burst of 75 show_status calls so the server never rate limits, latest wins", async () => {
    const { c, token } = await setup();
    const sim = await consumer(t!, token);
    await new Promise((resolve) => setTimeout(resolve, 100));

    const results: Json[] = [];
    for (let i = 1; i <= 75; i++) results.push(await call(c, "show_status", { msg: `step ${i}` }));
    expect(results.every((r) => r.ok === true)).toBe(true);
    expect(results.some((r) => r.throttled === true)).toBe(true);

    await sim.awaitMessage((m) => m.type === "status" && m.msg === "step 75", 3000);
    await new Promise((resolve) => setTimeout(resolve, 300));
    const statuses = sim.received.filter((m) => m.type === "status");
    expect(statuses.length).toBeLessThan(60);
    expect(statuses.at(-1)!.msg).toBe("step 75");
    expect(stderr).not.toContain("RATE_LIMITED");
    expect(t!.logs.join("\n")).not.toMatch(/RATE_LIMITED|rate limit/i);
    sim.close();
  });

  it("get_device_state summarises the room with device liveness", async () => {
    const { c, roomId } = await setup();
    const device = t!.deviceToken(roomId, "dv-a1b2c3", "desk");
    const socket = await TestSocket.connect(t!.wsUrl, { token: device.token });
    await socket.register("consumer", { kind: "device", deviceId: "dv-a1b2c3", version: "0.2.0" });
    socket.send(
      buildTelemetry({
        battery: { state: "discharging", pct: 81, mv: 3950 },
        power: "battery",
        rssi: -60,
        uptimeS: 10,
        heapFree: 150000,
        heapMin: 100000,
        resetReason: "poweron",
        fw: "0.2.0",
      }) as unknown as Json,
    );
    await new Promise((resolve) => setTimeout(resolve, 100));

    const state = await call(c, "get_device_state");
    expect(state).toMatchObject({ ok: true, mode: "ask", pending: 0, consumers: 1 });
    const devices = state.devices as Json[];
    expect(devices).toHaveLength(1);
    expect(devices[0]).toMatchObject({ id: "dv-a1b2c3", name: "desk", online: true, lastAckSecAgo: null });
    expect(typeof devices[0]!.lastSeenSecAgo).toBe("number");
    expect(devices[0]!.battery).toEqual({ state: "discharging", pct: 81 });
    socket.close();
  });
});
