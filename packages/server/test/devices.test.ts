// Device registry (protocol 3.2 device identity, 4.12 telemetry, 4.13
// identify, 7.5 devices API) over real HTTP and WebSocket connections.

import { afterEach, describe, expect, it } from "vitest";
import type { Device } from "@devotchi/shared";
import { TestSocket, UpgradeRejected, consumer, delay, startTestServer, type Json, type StartOptions, type TestServer } from "./helpers.js";
import { ALLOW, hold, requestPermission, roomState, until } from "./hook-helpers.js";

let ts: TestServer | undefined;
const sockets: TestSocket[] = [];

const PATIENT = { ackMs: 5000, aliveMs: 5000, holdMs: 8000 };

async function start(options: StartOptions = {}): Promise<TestServer> {
  ts = await startTestServer({ ...options, timing: { ...PATIENT, ...options.timing } });
  return ts;
}

async function track(socket: Promise<TestSocket>): Promise<TestSocket> {
  const s = await socket;
  sockets.push(s);
  return s;
}

afterEach(async () => {
  for (const socket of sockets.splice(0)) socket.close();
  await ts?.close();
  ts = undefined;
});

const TELEMETRY = {
  type: "telemetry",
  battery: { state: "absent", pct: null, mv: 4116 },
  power: "usb",
  rssi: -58,
  uptimeS: 5321,
  heapFree: 148392,
  heapMin: 101224,
  resetReason: "usb",
  fw: "0.2.0",
  screen: "pet",
  stage: "active",
};
const { type: _type, ...TELEMETRY_DATA } = TELEMETRY;

async function devices(t: TestServer, token: string): Promise<Device[]> {
  const res = await t.fetch("/api/devices", { token });
  expect(res.status).toBe(200);
  return ((await res.json()) as { devices: Device[] }).devices;
}

function device(t: TestServer, token: string, client: Json = {}): Promise<TestSocket> {
  return track(consumer(t, token, { kind: "device", deviceId: "dv-999999", ...client }));
}

async function rejectedStatus(t: TestServer, token: string): Promise<number> {
  const err = await TestSocket.connect(t.wsUrl, { token }).catch((e: unknown) => e);
  return err instanceof UpgradeRejected ? err.status : 0;
}

describe("device identity", () => {
  it("reports a device token under its registry id even when it claims another id", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const paired = t.deviceToken(cli.roomId, "dv-111111", "desk");
    const d = await device(t, paired.token, { deviceId: "dv-222222" });

    const held = await requestPermission(t, cli.token, d);
    hold(d, held.id);
    await until(async () => (await roomState(t, cli.token)).pending[0]?.holders.length === 1);
    expect((await roomState(t, cli.token)).pending[0].holders).toEqual(["dv-111111"]);
    expect((await devices(t, cli.token)).map((x) => [x.id, x.holding])).toEqual([["dv-111111", 1]]);

    d.send({ type: "permission_response", id: held.id, granted: true });
    expect(await held.reply).toEqual(ALLOW);
    expect(await d.awaitMessage("dismiss")).toMatchObject({ reason: "answered", by: "dv-111111" });
    expect((await roomState(t, cli.token)).lastRequest?.by).toBe("dv-111111");
    expect((await devices(t, cli.token)).map((x) => x.id)).toEqual(["dv-111111"]);
    expect(t.server.internals.db.get("SELECT id FROM devices WHERE token_id = ?", paired.id)).toEqual({ id: "dv-111111" });
  });

  it("names a cli-token sim by its own id and keeps it out of the registry", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const sim = await track(consumer(t, cli.token, { kind: "sim", deviceId: "sim-bench" }));
    const held = await requestPermission(t, cli.token, sim);
    hold(sim, held.id);
    await until(async () => (await roomState(t, cli.token)).pending[0]?.holders.length === 1);
    expect((await roomState(t, cli.token)).pending[0].holders).toEqual(["sim-bench"]);
    sim.send({ type: "permission_response", id: held.id, granted: false });
    expect(await sim.awaitMessage("dismiss")).toMatchObject({ outcome: "deny", by: "sim-bench" });
    expect(await devices(t, cli.token)).toEqual([]);
    expect((await roomState(t, cli.token)).devices).toEqual([]);
    expect(t.server.internals.db.get("SELECT COUNT(*) AS n FROM devices")).toEqual({ n: 0 });
  });

  it("stores client.version as fw and client.model on register", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const paired = t.deviceToken(cli.roomId, "dv-111111");
    const d = await device(t, paired.token, { deviceId: "dv-111111", model: "devotchi-v1", version: "0.3.1" });
    expect((await devices(t, cli.token))[0]).toMatchObject({ model: "devotchi-v1", fw: "0.3.1" });
    d.close();
    await d.closed;
    // A register without them keeps the stored values.
    await device(t, paired.token, { deviceId: "dv-111111" });
    expect((await devices(t, cli.token))[0]).toMatchObject({ model: "devotchi-v1", fw: "0.3.1" });
  });

  it("closes a device token without a registry row with 4003", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const paired = t.deviceToken(cli.roomId, "dv-111111");
    t.server.internals.db.run("DELETE FROM devices WHERE token_id = ?", paired.id);
    const s = await track(TestSocket.connect(t.wsUrl, { token: paired.token }));
    s.send({ type: "register", role: "consumer", proto: 2, client: { kind: "device", deviceId: "dv-111111" } });
    expect(await s.awaitMessage("error")).toMatchObject({ code: "NOT_ALLOWED" });
    expect((await s.closed).code).toBe(4003);
  });

  it("restricts device tokens to the consumer WebSocket", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const paired = t.deviceToken(cli.roomId, "dv-111111");
    const routes: [string, string, unknown?][] = [
      ["GET", "/api/devices"],
      ["GET", "/api/room"],
      ["GET", "/api/tokens"],
      ["POST", "/api/pair", {}],
      ["POST", "/api/pair/confirm", { code: "K7M2QX" }],
      ["PATCH", "/api/devices/dv-111111", { name: "x" }],
      ["DELETE", "/api/devices/dv-111111"],
      ["POST", "/api/devices/dv-111111/identify", {}],
      ["POST", "/api/notify", { event: "x" }],
      ["POST", "/hook/event", { hook_event_name: "Stop" }],
    ];
    for (const [method, path, json] of routes) {
      const res = await t.fetch(path, { method, json, token: paired.token });
      expect(res.status, `${method} ${path}`).toBe(403);
    }
    const s = await track(TestSocket.connect(t.wsUrl, { token: paired.token }));
    s.send({ type: "register", role: "producer", proto: 2, client: { kind: "mcp", version: "1" } });
    expect(await s.awaitMessage("error")).toMatchObject({ code: "NOT_ALLOWED" });
    expect((await s.closed).code).toBe(1008);
  });
});

describe("GET /api/devices", () => {
  it("returns the Device shape with liveness that follows holding and disconnect", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const paired = t.deviceToken(cli.roomId, "dv-e356e4", "desk");
    const before = await devices(t, cli.token);
    expect(before).toEqual([
      {
        id: "dv-e356e4",
        name: "desk",
        model: null,
        fw: null,
        createdAt: expect.stringMatching(/Z$/),
        online: false,
        lastSeen: null,
        lastHoldAt: null,
        holding: 0,
        telemetry: null,
        telemetryAt: null,
      },
    ]);

    const d = await device(t, paired.token, { deviceId: "dv-e356e4" });
    const online = (await devices(t, cli.token))[0];
    expect(online.online).toBe(true);
    expect(online.lastSeen).toMatch(/Z$/);
    expect(online.lastHoldAt).toBeNull();

    const held = await requestPermission(t, cli.token, d);
    hold(d, held.id);
    await until(async () => (await devices(t, cli.token))[0].holding === 1);
    const holding = (await devices(t, cli.token))[0];
    expect(holding.lastHoldAt).toMatch(/Z$/);
    expect(Date.parse(holding.lastSeen ?? "")).toBeGreaterThanOrEqual(Date.parse(online.lastSeen ?? ""));
    expect((await roomState(t, cli.token)).devices).toEqual([expect.objectContaining({ id: "dv-e356e4", holding: 1 })]);

    d.close();
    await d.closed;
    await until(async () => !(await devices(t, cli.token))[0].online);
    const offline = (await devices(t, cli.token))[0];
    expect(offline).toMatchObject({ online: false, holding: 0, lastHoldAt: holding.lastHoldAt });
    expect(offline.lastSeen).toBe(holding.lastSeen);
    // Written to the database on disconnect.
    const row = t.server.internals.db.get<{ last_seen: string; last_hold_at: string }>(
      "SELECT last_seen, last_hold_at FROM devices WHERE token_id = ?",
      paired.id,
    );
    expect(row).toEqual({ last_seen: offline.lastSeen, last_hold_at: offline.lastHoldAt });
    await held.reply;
  });

  it("flushes last_seen to the database at the write interval while connected", async () => {
    const t = await start({ timing: { deviceWriteMs: 50 } });
    const cli = await t.mintToken();
    const paired = t.deviceToken(cli.roomId, "dv-e356e4");
    const d = await device(t, paired.token, { deviceId: "dv-e356e4" });
    const read = () => t.server.internals.db.get<{ last_seen: string | null }>("SELECT last_seen FROM devices WHERE token_id = ?", paired.id)?.last_seen;
    await until(() => read() != null);
    const first = read();
    await delay(20);
    d.send({ type: "ping" });
    await d.awaitMessage("pong");
    await until(() => read() !== first);
    expect(Date.parse(read() ?? "")).toBeGreaterThan(Date.parse(first ?? ""));
  });

  it("lists only the caller's room", async () => {
    const t = await start();
    const a = await t.mintToken();
    const b = await t.mintToken();
    t.deviceToken(a.roomId, "dv-aaaaaa");
    t.deviceToken(b.roomId, "dv-bbbbbb");
    expect((await devices(t, a.token)).map((x) => x.id)).toEqual(["dv-aaaaaa"]);
    expect((await devices(t, b.token)).map((x) => x.id)).toEqual(["dv-bbbbbb"]);
  });
});

describe("PATCH /api/devices/:id", () => {
  it("renames a device and validates the name", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const other = await t.mintToken();
    const paired = t.deviceToken(cli.roomId, "dv-e356e4");
    const res = await t.fetch("/api/devices/dv-e356e4", { method: "PATCH", json: { name: "kitchen" }, token: cli.token });
    expect(res.status).toBe(200);
    expect(await res.json()).toMatchObject({ id: "dv-e356e4", name: "kitchen", online: false });
    for (const json of [{}, { name: "" }, { name: 5 }, { name: "x".repeat(25) }, { name: "a\tb" }, [], "kitchen"]) {
      const bad = await t.fetch("/api/devices/dv-e356e4", { method: "PATCH", json, token: cli.token });
      expect(bad.status, JSON.stringify(json)).toBe(400);
    }
    expect((await t.fetch("/api/devices/dv-e356e4", { method: "PATCH", json: { name: "x" }, token: other.token })).status).toBe(404);
    expect((await t.fetch("/api/devices/dv-000000", { method: "PATCH", json: { name: "x" }, token: cli.token })).status).toBe(404);
    const d = await track(TestSocket.connect(t.wsUrl, { token: paired.token }));
    expect((await d.register("consumer", { kind: "device", deviceId: "dv-e356e4" })).device).toEqual({ id: "dv-e356e4", name: "kitchen" });
  });
});

describe("POST /api/devices/:id/identify", () => {
  it("sends identify to an online device and answers 409 when it is offline", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const other = await t.mintToken();
    const paired = t.deviceToken(cli.roomId, "dv-e356e4");
    const path = "/api/devices/dv-e356e4/identify";
    const offline = await t.fetch(path, { method: "POST", json: {}, token: cli.token });
    expect(offline.status).toBe(409);
    expect((await offline.json()).code).toBe("conflict");

    const d = await device(t, paired.token, { deviceId: "dv-e356e4" });
    const sent = await t.fetch(path, { method: "POST", token: cli.token });
    expect(sent.status).toBe(202);
    expect(await sent.json()).toEqual({ sent: true });
    expect(await d.awaitMessage("identify")).toEqual({ type: "identify", durationMs: 3000 });
    expect((await t.fetch(path, { method: "POST", json: { durationMs: 5000 }, token: cli.token })).status).toBe(202);
    expect(await d.awaitMessage("identify")).toEqual({ type: "identify", durationMs: 5000 });
    for (const durationMs of [999, 10001, 1500.5, "3000", null]) {
      expect((await t.fetch(path, { method: "POST", json: { durationMs }, token: cli.token })).status).toBe(400);
    }
    expect((await t.fetch(path, { method: "POST", json: {}, token: other.token })).status).toBe(404);
    expect((await t.fetch("/api/devices/dv-000000/identify", { method: "POST", json: {}, token: cli.token })).status).toBe(404);
  });
});

describe("DELETE /api/devices/:id", () => {
  it("deletes the row and token and closes the socket with 4003", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const other = await t.mintToken();
    const paired = t.deviceToken(cli.roomId, "dv-e356e4");
    const d = await device(t, paired.token, { deviceId: "dv-e356e4" });
    expect((await t.fetch("/api/devices/dv-e356e4", { method: "DELETE", token: other.token })).status).toBe(404);
    expect(d.isOpen).toBe(true);
    const res = await t.fetch("/api/devices/dv-e356e4", { method: "DELETE", token: cli.token });
    expect(res.status).toBe(204);
    expect((await d.closed).code).toBe(4003);
    expect(await rejectedStatus(t, paired.token)).toBe(401);
    expect(await devices(t, cli.token)).toEqual([]);
    expect(t.server.internals.db.get("SELECT 1 FROM tokens WHERE id = ?", paired.id)).toBeUndefined();
    expect((await t.fetch("/api/devices/dv-e356e4", { method: "DELETE", token: cli.token })).status).toBe(404);
  });
});

describe("telemetry", () => {
  it("stores the latest sample and exposes it without forwarding it", async () => {
    const t = await start({ timing: { systemMessageIntervalMs: 0, deviceWriteMs: 60_000 } });
    const cli = await t.mintToken();
    const paired = t.deviceToken(cli.roomId, "dv-e356e4");
    const watcher = await track(consumer(t, cli.token, { kind: "dashboard" }));
    const d = await device(t, paired.token, { deviceId: "dv-e356e4" });
    d.send(TELEMETRY);
    await until(async () => (await devices(t, cli.token))[0].telemetry !== null);
    const first = (await devices(t, cli.token))[0];
    expect(first.telemetry).toEqual(TELEMETRY_DATA);
    expect(first.telemetryAt).toMatch(/Z$/);
    const read = () =>
      t.server.internals.db.get<{ telemetry: string; telemetry_at: string }>(
        "SELECT telemetry, telemetry_at FROM devices WHERE token_id = ?",
        paired.id,
      );
    expect(JSON.parse(read()?.telemetry ?? "null")).toEqual(TELEMETRY_DATA);

    // Within the write interval the new sample is in memory only.
    const lowBattery = { ...TELEMETRY, battery: { state: "discharging", pct: 12, mv: 3480 }, power: "battery", alert: "battery_low" };
    await delay(5);
    d.send(lowBattery);
    await until(async () => (await devices(t, cli.token))[0].telemetry?.power === "battery");
    expect((await devices(t, cli.token))[0].telemetry).toMatchObject({ alert: "battery_low", battery: { pct: 12 } });
    expect(JSON.parse(read()?.telemetry ?? "null").power).toBe("usb");

    d.close();
    await d.closed;
    await until(() => JSON.parse(read()?.telemetry ?? "null").power === "battery");
    expect((await devices(t, cli.token))[0].telemetry).toMatchObject({ power: "battery" });
    await watcher.expectNoMessage("telemetry");
  });

  it("rate limits telemetry to one per second per socket", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const paired = t.deviceToken(cli.roomId, "dv-e356e4");
    const d = await device(t, paired.token, { deviceId: "dv-e356e4" });
    d.send(TELEMETRY);
    d.send(TELEMETRY);
    expect(await d.awaitMessage("error")).toMatchObject({ code: "RATE_LIMITED" });
  });

  it("keeps a cli-token sim's telemetry in memory only", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const sim = await track(consumer(t, cli.token, { kind: "sim", deviceId: "sim-bench" }));
    sim.send(TELEMETRY);
    await until(() => [...t.server.internals.rooms.sockets].some((c) => c.lastTelemetry !== null));
    const conn = [...t.server.internals.rooms.sockets].find((c) => c.registration?.identity === "sim-bench");
    expect(conn?.lastTelemetry?.data).toEqual(TELEMETRY_DATA);
    expect(await devices(t, cli.token)).toEqual([]);
    expect(t.server.internals.db.get("SELECT COUNT(*) AS n FROM devices")).toEqual({ n: 0 });
  });

  it("rejects invalid telemetry", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const paired = t.deviceToken(cli.roomId, "dv-e356e4");
    const d = await device(t, paired.token, { deviceId: "dv-e356e4" });
    d.send({ ...TELEMETRY, battery: { state: "full", pct: 140, mv: 4200 } });
    expect(await d.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE" });
    expect((await devices(t, cli.token))[0].telemetry).toBeNull();
  });
});
