// Pairing flows A and B (protocol 7.4, 9) over real HTTP, with shrunk TTLs.

import { afterEach, describe, expect, it } from "vitest";
import { findTokenByValue } from "../src/db/tokens.js";
import { TestSocket, UpgradeRejected, consumer, delay, startTestServer, type Json, type StartOptions, type TestServer } from "./helpers.js";

let ts: TestServer | undefined;
const sockets: TestSocket[] = [];

async function start(options: StartOptions = {}): Promise<TestServer> {
  ts = await startTestServer(options);
  return ts;
}

afterEach(async () => {
  for (const socket of sockets.splice(0)) socket.close();
  await ts?.close();
  ts = undefined;
});

async function post(
  t: TestServer,
  path: string,
  json: unknown,
  token?: string,
  headers: Record<string, string> = {},
): Promise<{ status: number; body: Json }> {
  const res = await t.fetch(path, { method: "POST", json, token, headers });
  const text = await res.text();
  return { status: res.status, body: text === "" ? {} : (JSON.parse(text) as Json) };
}

const startPair = (t: TestServer, json: unknown = { deviceId: "dv-a1b2c3", model: "devotchi-v1", fw: "0.2.0" }) =>
  post(t, "/api/device/pair/start", json);
const poll = (t: TestServer, deviceSecret: unknown) => post(t, "/api/device/pair/poll", { deviceSecret });
const confirm = (t: TestServer, token: string, code: unknown, name?: unknown) =>
  post(t, "/api/pair/confirm", name === undefined ? { code } : { code, name }, token);
const confirmFrom = (t: TestServer, ip: string, token: string, code: unknown) =>
  post(t, "/api/pair/confirm", { code }, token, { "X-Forwarded-For": ip });

/** Flow A start without HTTP, for tests about what follows it (start itself is limited to 6/min). */
function started(t: TestServer, deviceId = "dv-a1b2c3"): { code: string; deviceSecret: string } {
  return t.server.internals.pairing.start({ deviceId });
}
const createCode = (t: TestServer, token: string) => post(t, "/api/pair", {}, token);
const claim = (t: TestServer, json: unknown) => post(t, "/api/pair/claim", json);

function tokenRow(t: TestServer, token: string): unknown {
  return findTokenByValue(t.server.internals.db, token);
}

function deviceRows(t: TestServer): Json[] {
  return t.server.internals.db.all("SELECT room_id, id, name, model, fw FROM devices ORDER BY room_id, id");
}

async function connectDevice(t: TestServer, token: string, deviceId = "dv-a1b2c3"): Promise<{ socket: TestSocket; ack: Json }> {
  const socket = await TestSocket.connect(t.wsUrl, { token });
  sockets.push(socket);
  const ack = await socket.register("consumer", { kind: "device", deviceId, model: "devotchi-v1", version: "0.2.0" });
  return { socket, ack };
}

async function rejected(t: TestServer, token: string): Promise<number> {
  const err = await TestSocket.connect(t.wsUrl, { token }).catch((e: unknown) => e);
  return err instanceof UpgradeRejected ? err.status : 0;
}

describe("flow A: the device shows a code", () => {
  it("start, pending poll, confirm, poll delivers the credentials", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const started = await startPair(t);
    expect(started.status).toBe(201);
    expect(started.body).toEqual({
      code: expect.stringMatching(/^[ABCDEFGHJKMNPQRSTUVWXYZ23456789]{6}$/),
      deviceSecret: expect.stringMatching(/^[0-9a-f]{32}$/),
      expiresInS: 300,
      pollIntervalMs: 2000,
    });
    const { code, deviceSecret } = started.body as { code: string; deviceSecret: string };
    expect(t.server.internals.db.get("SELECT secret_hash FROM device_pairings WHERE code = ?", code)).not.toEqual({
      secret_hash: deviceSecret,
    });

    const pending = await poll(t, deviceSecret);
    expect(pending.status).toBe(202);
    expect(pending.body).toEqual({ status: "pending", expiresInS: expect.any(Number) });
    expect(pending.body.expiresInS as number).toBeGreaterThan(290);

    // Lower case and a space, as typed by a person.
    const confirmed = await confirm(t, cli.token, `${code.slice(0, 3).toLowerCase()} ${code.slice(3)}`, "desk");
    expect(confirmed).toEqual({ status: 200, body: { deviceId: "dv-a1b2c3", name: "desk" } });

    const delivered = await poll(t, deviceSecret);
    expect(delivered.status).toBe(200);
    expect(delivered.body).toEqual({
      token: expect.stringMatching(/^[0-9a-f-]{36}$/),
      roomId: cli.roomId,
      deviceId: "dv-a1b2c3",
      name: "desk",
    });
    const kind = t.server.internals.db.get("SELECT kind, label FROM tokens WHERE room_id = ? AND kind = 'device'", cli.roomId);
    expect(kind).toEqual({ kind: "device", label: "device" });

    const { ack } = await connectDevice(t, delivered.body.token as string);
    expect(ack.device).toEqual({ id: "dv-a1b2c3", name: "desk" });
    const devices = (await (await t.fetch("/api/devices", { token: cli.token })).json()) as { devices: Json[] };
    expect(devices.devices).toEqual([expect.objectContaining({ id: "dv-a1b2c3", name: "desk", model: "devotchi-v1", fw: "0.2.0", online: true })]);
    expect(t.logs.some((line) => line.includes(code) || line.includes(deviceSecret))).toBe(false);
  });

  it("names the device 'device' by default and validates the name", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const { code } = started(t);
    for (const name of ["", " ", "x".repeat(25), "ab\ncd", 42, null, ["desk"], "é".repeat(12) + "x"]) {
      const res = await confirm(t, cli.token, code, name);
      expect(res.status, JSON.stringify(name)).toBe(400);
      expect(res.body.code).toBe("invalid_request");
    }
    expect((await confirm(t, cli.token, code)).body).toEqual({ deviceId: "dv-a1b2c3", name: "device" });
    // 24 bytes of two-byte characters is the maximum.
    expect((await confirm(t, cli.token, started(t, "dv-000002").code, "é".repeat(12))).status).toBe(200);
  });

  it("expires an unconfirmed code", async () => {
    const t = await start({ timing: { pairCodeTtlMs: 150 } });
    const cli = await t.mintToken();
    const { code, deviceSecret } = (await startPair(t)).body as { code: string; deviceSecret: string };
    await delay(200);
    expect(await poll(t, deviceSecret)).toEqual({ status: 410, body: { error: expect.any(String), code: "expired" } });
    expect(await confirm(t, cli.token, code)).toEqual({ status: 410, body: { error: expect.any(String), code: "expired" } });
    t.server.internals.pairing.prune();
    expect((await poll(t, deviceSecret)).status).toBe(404);
  });

  it("refuses a second confirm of the same code", async () => {
    const t = await start();
    const a = await t.mintToken();
    const b = await t.mintToken();
    const { code } = (await startPair(t)).body as { code: string };
    expect((await confirm(t, a.token, code)).status).toBe(200);
    expect(await confirm(t, a.token, code)).toEqual({ status: 410, body: { error: expect.any(String), code: "already_used" } });
    expect((await confirm(t, b.token, code)).body.code).toBe("already_used");
    expect(deviceRows(t)).toHaveLength(1);
  });

  it("repeats the delivered body for the repeat window, then answers 410", async () => {
    const t = await start({ timing: { pairDeliveryMs: 300 } });
    const cli = await t.mintToken();
    const { code, deviceSecret } = (await startPair(t)).body as { code: string; deviceSecret: string };
    await confirm(t, cli.token, code);
    const first = await poll(t, deviceSecret);
    expect(first.status).toBe(200);
    // The response was lost: the device polls again with the same secret.
    const again = await poll(t, deviceSecret);
    expect(again).toEqual(first);
    await delay(400);
    expect((await poll(t, deviceSecret)).body.code).toBe("expired");
    // The delivered device itself stays paired.
    await connectDevice(t, first.body.token as string);
  });

  it("delivers when the code expires between confirm and poll", async () => {
    const t = await start({ timing: { pairCodeTtlMs: 200, pairDeliveryMs: 2000 } });
    const cli = await t.mintToken();
    const { code, deviceSecret } = (await startPair(t)).body as { code: string; deviceSecret: string };
    await confirm(t, cli.token, code);
    await delay(250);
    t.server.internals.pairing.prune();
    expect((await poll(t, deviceSecret)).status).toBe(200);
  });

  it("removes a confirmed pairing whose token was not fetched in time", async () => {
    const t = await start({ timing: { pairDeliveryMs: 150 } });
    const cli = await t.mintToken();
    const { code, deviceSecret } = (await startPair(t)).body as { code: string; deviceSecret: string };
    await confirm(t, cli.token, code);
    const { db } = t.server.internals;
    const pairing = db.get<{ token_id: string }>("SELECT token_id FROM device_pairings WHERE code = ?", code);
    expect(pairing?.token_id).toMatch(/^[0-9a-f]{8}$/);
    expect(deviceRows(t)).toHaveLength(1);
    await delay(200);
    expect(await poll(t, deviceSecret)).toEqual({ status: 410, body: { error: expect.any(String), code: "expired" } });
    expect(deviceRows(t)).toEqual([]);
    expect(db.get("SELECT 1 FROM tokens WHERE id = ?", pairing?.token_id ?? "")).toBeUndefined();
    expect(((await (await t.fetch("/api/devices", { token: cli.token })).json()) as { devices: unknown[] }).devices).toEqual([]);
    expect((await poll(t, deviceSecret)).status).toBe(410);
  });

  it("removes an undelivered pairing on the maintenance prune without a poll", async () => {
    const t = await start({ timing: { pairDeliveryMs: 100 } });
    const cli = await t.mintToken();
    const { code } = (await startPair(t)).body as { code: string };
    await confirm(t, cli.token, code);
    expect(deviceRows(t)).toHaveLength(1);
    await delay(150);
    t.server.internals.pairing.prune();
    expect(deviceRows(t)).toEqual([]);
    expect(t.server.internals.db.get("SELECT 1 FROM tokens WHERE kind = 'device'")).toBeUndefined();
  });

  it("removes a pairing confirmed before a restart and answers its poll with 410", async () => {
    const t = await start({ keepDir: true });
    const cli = await t.mintToken();
    const { code, deviceSecret } = (await startPair(t)).body as { code: string; deviceSecret: string };
    const pendingOther = (await startPair(t, { deviceId: "dv-0000ff" })).body as { code: string; deviceSecret: string };
    await confirm(t, cli.token, code);
    expect(deviceRows(t)).toHaveLength(1);
    const dir = t.dir;
    await t.close();
    ts = undefined;

    const reopened = await start({ dir });
    expect(deviceRows(reopened)).toEqual([]);
    expect(reopened.server.internals.db.get("SELECT 1 FROM tokens WHERE kind = 'device'")).toBeUndefined();
    expect(await poll(reopened, deviceSecret)).toEqual({ status: 410, body: { error: expect.any(String), code: "expired" } });
    // A pairing that was still pending survives the restart and can be confirmed.
    expect((await poll(reopened, pendingOther.deviceSecret)).status).toBe(202);
    expect((await confirm(reopened, cli.token, pendingOther.code)).status).toBe(200);
    expect((await poll(reopened, pendingOther.deviceSecret)).status).toBe(200);
  });

  it("answers 410 when the device was deleted between confirm and poll", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const { code, deviceSecret } = (await startPair(t)).body as { code: string; deviceSecret: string };
    await confirm(t, cli.token, code);
    expect((await t.fetch("/api/devices/dv-a1b2c3", { method: "DELETE", token: cli.token })).status).toBe(204);
    expect((await poll(t, deviceSecret)).body.code).toBe("expired");
  });

  it("keeps concurrent codes apart", async () => {
    const t = await start();
    const a = await t.mintToken();
    const b = await t.mintToken();
    const first = (await startPair(t, { deviceId: "dv-111111" })).body as { code: string; deviceSecret: string };
    const second = (await startPair(t, { deviceId: "dv-222222" })).body as { code: string; deviceSecret: string };
    expect(first.code).not.toBe(second.code);
    expect(first.deviceSecret).not.toBe(second.deviceSecret);
    expect((await confirm(t, b.token, second.code, "two")).body).toEqual({ deviceId: "dv-222222", name: "two" });
    expect((await poll(t, first.deviceSecret)).status).toBe(202);
    expect((await confirm(t, a.token, first.code, "one")).body).toEqual({ deviceId: "dv-111111", name: "one" });
    const one = await poll(t, first.deviceSecret);
    const two = await poll(t, second.deviceSecret);
    expect(one.body).toMatchObject({ roomId: a.roomId, deviceId: "dv-111111", name: "one" });
    expect(two.body).toMatchObject({ roomId: b.roomId, deviceId: "dv-222222", name: "two" });
    expect(one.body.token).not.toBe(two.body.token);
  });

  it("validates start, poll and confirm bodies", async () => {
    const windowMs = 30;
    const t = await start({ timing: { rateWindowMs: windowMs } });
    const cli = await t.mintToken();
    for (const json of [{}, { deviceId: "dv-XYZ123" }, { deviceId: "device" }, { deviceId: 7 }, { deviceId: "dv-a1b2c3", model: 5 }, { deviceId: "dv-a1b2c3", fw: {} }, [1], "x", null]) {
      const res = await startPair(t, json);
      expect(res.status, JSON.stringify(json)).toBe(400);
      expect(res.body.code).toBe("invalid_request");
      await delay(windowMs / 5);
    }
    for (const secret of [undefined, 42, "abc", "A".repeat(32), "0".repeat(33), ["0".repeat(32)]]) {
      expect((await poll(t, secret)).status).toBe(400);
    }
    expect((await poll(t, "0".repeat(32))).status).toBe(404);
    for (const code of [undefined, 123, "", "ABC", "K7M2Q0", { code: "K7M2QX" }]) {
      expect((await confirm(t, cli.token, code)).status).toBe(400);
    }
    expect((await confirm(t, cli.token, "K7M2QX")).status).toBe(404);
  });

  it("cuts long model and fw values instead of rejecting them", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const { code } = (await startPair(t, { deviceId: "dv-a1b2c3", model: "m".repeat(40), fw: "1".repeat(30) })).body as { code: string };
    await confirm(t, cli.token, code);
    expect(deviceRows(t)).toEqual([{ room_id: cli.roomId, id: "dv-a1b2c3", name: "device", model: "m".repeat(24), fw: "1".repeat(24) }]);
  });

  it("requires a cli token or session to confirm", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const device = t.deviceToken(cli.roomId);
    const { code } = (await startPair(t)).body as { code: string };
    expect((await confirm(t, device.token, code)).status).toBe(403);
    expect((await post(t, "/api/pair/confirm", { code })).status).toBe(401);
    expect((await confirm(t, cli.token, code)).status).toBe(200);
  });

  it("throttles start at 6 and poll at 60 per minute per IP", async () => {
    const t = await start();
    for (let i = 0; i < 6; i++) expect((await startPair(t)).status).toBe(201);
    const limited = await startPair(t);
    expect(limited.status).toBe(429);
    expect(limited.body.code).toBe("rate_limited");
    for (let i = 0; i < 60; i++) expect((await poll(t, "0".repeat(32))).status).toBe(404);
    expect((await poll(t, "0".repeat(32))).status).toBe(429);
  });

  it("throttles confirm per token and per IP", async () => {
    const t = await start({ trustProxy: true });
    const one = t.cliToken();
    for (let i = 0; i < 10; i++) expect((await confirmFrom(t, `10.0.0.${i}`, one.token, "K7M2QX")).status).toBe(404);
    expect((await confirmFrom(t, "10.0.0.99", one.token, "K7M2QX")).status).toBe(429);
    // Fresh tokens, which are free to mint in single-user mode, do not get around the per-IP limit.
    for (let i = 0; i < 10; i++) expect((await confirmFrom(t, "10.0.1.1", t.cliToken().token, "K7M2QX")).status).toBe(404);
    const limited = await t.fetch("/api/pair/confirm", {
      method: "POST",
      json: { code: "K7M2QX" },
      token: t.cliToken().token,
      headers: { "X-Forwarded-For": "10.0.1.1" },
    });
    expect(limited.status).toBe(429);
    expect(Number(limited.headers.get("retry-after"))).toBeGreaterThanOrEqual(1);
  });

  it("locks an IP out after 100 failed codes in an hour", async () => {
    const windowMs = 40;
    const t = await start({ timing: { rateWindowMs: windowMs } });
    const cli = await t.mintToken();
    const { code } = started(t);
    for (let batch = 0; batch < 10; batch++) {
      for (let i = 0; i < 10; i++) expect((await confirm(t, t.cliToken().token, "K7M2QX")).status).toBe(404);
      await delay(windowMs + 5);
    }
    // The per-minute window has passed; the hourly failure budget has not.
    const locked = await confirm(t, cli.token, code);
    expect(locked.status).toBe(429);
    expect(locked.body.code).toBe("rate_limited");
  });
});

describe("flow B: the PC creates a code", () => {
  it("creates a code and the device claims it", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const created = await createCode(t, cli.token);
    expect(created.status).toBe(201);
    expect(created.body).toEqual({
      code: expect.stringMatching(/^[ABCDEFGHJKMNPQRSTUVWXYZ23456789]{6}$/),
      expiresAt: expect.stringMatching(/Z$/),
      expiresInS: 300,
    });
    expect(created.body).not.toHaveProperty("pairingCode");
    const claimed = await claim(t, { code: created.body.code, deviceId: "dv-e356e4", model: "devotchi-v1", fw: "0.2.0" });
    expect(claimed).toEqual({
      status: 201,
      body: { token: expect.stringMatching(/^[0-9a-f-]{36}$/), roomId: cli.roomId, deviceId: "dv-e356e4", name: "device" },
    });
    expect(deviceRows(t)).toEqual([{ room_id: cli.roomId, id: "dv-e356e4", name: "device", model: "devotchi-v1", fw: "0.2.0" }]);
    const { ack } = await connectDevice(t, claimed.body.token as string, "dv-e356e4");
    expect(ack.device).toEqual({ id: "dv-e356e4", name: "device" });
    expect(t.logs.some((line) => line.includes(created.body.code as string))).toBe(false);
  });

  it("gives a device without deviceId a random dv- id", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const { code } = (await createCode(t, cli.token)).body;
    const claimed = await claim(t, { code });
    expect(claimed.status).toBe(201);
    expect(claimed.body.deviceId).toMatch(/^dv-[0-9a-f]{6}$/);
  });

  it("answers reuse, expiry and unknown codes", async () => {
    const t = await start({ timing: { pairCodeTtlMs: 150 } });
    const cli = await t.mintToken();
    const { code } = (await createCode(t, cli.token)).body;
    expect((await claim(t, { code })).status).toBe(201);
    expect(await claim(t, { code })).toEqual({ status: 410, body: { error: expect.any(String), code: "already_used" } });
    const { code: late } = (await createCode(t, cli.token)).body;
    await delay(200);
    expect(await claim(t, { code: late })).toEqual({ status: 410, body: { error: expect.any(String), code: "expired" } });
    expect((await claim(t, { code: "K7M2QX" })).status).toBe(404);
    t.server.internals.pairing.prune();
    expect(t.server.internals.db.get("SELECT COUNT(*) AS n FROM pairings")).toEqual({ n: 0 });
  });

  it("validates claim bodies", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const { code } = (await createCode(t, cli.token)).body;
    for (const json of [{}, { code: 5 }, { code: [code] }, { code, deviceId: "dv-12345" }, { code, deviceId: "DV-A1B2C3" }, { code, deviceId: 1 }, { code, model: 1 }, { code, fw: [] }, "x"]) {
      const res = await claim(t, json);
      expect(res.status, JSON.stringify(json)).toBe(400);
      expect(res.body.code).toBe("invalid_request");
      await delay(0);
    }
  });

  it("requires a cli token to create a code", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const device = t.deviceToken(cli.roomId);
    expect((await createCode(t, device.token)).status).toBe(403);
    expect((await post(t, "/api/pair", {})).status).toBe(401);
    const empty = await t.fetch("/api/pair", { method: "POST", token: cli.token });
    expect(empty.status).toBe(201);
  });

  it("throttles claims at 10 per minute and 20 failures per hour", async () => {
    const windowMs = 50;
    const t = await start({ timing: { rateWindowMs: windowMs } });
    const cli = await t.mintToken();
    for (let i = 0; i < 10; i++) expect((await claim(t, { code: "K7M2QX" })).status).toBe(404);
    expect((await claim(t, { code: "K7M2QX" })).status).toBe(429);
    await delay(windowMs + 5);
    for (let i = 0; i < 10; i++) expect((await claim(t, { code: "K7M2QX" })).status).toBe(404);
    await delay(windowMs + 5);
    const { code } = (await createCode(t, cli.token)).body;
    const locked = await claim(t, { code });
    expect(locked.status).toBe(429);
  });
});

describe("pairing a device id again", () => {
  it("replaces the device in the same room: the old socket closes with 4003 and its token is dead", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const first = await claim(t, { code: (await createCode(t, cli.token)).body.code, deviceId: "dv-e356e4" });
    const { socket } = await connectDevice(t, first.body.token as string, "dv-e356e4");

    const { code, deviceSecret } = (await startPair(t, { deviceId: "dv-e356e4" })).body as { code: string; deviceSecret: string };
    expect((await confirm(t, cli.token, code, "again")).status).toBe(200);
    expect((await socket.closed).code).toBe(4003);
    expect(await rejected(t, first.body.token as string)).toBe(401);
    const second = await poll(t, deviceSecret);
    await connectDevice(t, second.body.token as string, "dv-e356e4");
    expect(deviceRows(t)).toEqual([{ room_id: cli.roomId, id: "dv-e356e4", name: "again", model: "devotchi-v1", fw: "0.2.0" }]);
    expect(tokenRow(t, first.body.token as string)).toBeUndefined();
  });

  it("never touches a device with the same id in another room", async () => {
    const t = await start();
    const a = await t.mintToken();
    const b = await t.mintToken();
    const inA = await claim(t, { code: (await createCode(t, a.token)).body.code, deviceId: "dv-e356e4" });
    const { socket: socketA } = await connectDevice(t, inA.body.token as string, "dv-e356e4");

    const inB = await claim(t, { code: (await createCode(t, b.token)).body.code, deviceId: "dv-e356e4" });
    expect(inB.status).toBe(201);
    const { code, deviceSecret } = (await startPair(t, { deviceId: "dv-e356e4" })).body as { code: string; deviceSecret: string };
    const c = await t.mintToken();
    expect((await confirm(t, c.token, code)).status).toBe(200);
    const inC = await poll(t, deviceSecret);
    const { socket: socketB } = await connectDevice(t, inB.body.token as string, "dv-e356e4");
    await connectDevice(t, inC.body.token as string, "dv-e356e4");

    await delay(100);
    expect(socketA.isOpen).toBe(true);
    expect(socketB.isOpen).toBe(true);
    expect(deviceRows(t).map((row) => row.room_id).sort()).toEqual([a.roomId, b.roomId, c.roomId].sort());
    for (const cli of [a, b, c]) {
      const list = (await (await t.fetch("/api/devices", { token: cli.token })).json()) as { devices: Json[] };
      expect(list.devices).toEqual([expect.objectContaining({ id: "dv-e356e4", online: true })]);
    }
  });
});

describe("registered sims of a paired token", () => {
  it("lets a device token register as kind sim", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const { code, deviceSecret } = (await startPair(t, { deviceId: "sim-bench" })).body as { code: string; deviceSecret: string };
    await confirm(t, cli.token, code);
    const { token } = (await poll(t, deviceSecret)).body as { token: string };
    const s = await consumer(t, token, { kind: "sim", deviceId: "sim-bench" });
    sockets.push(s);
    const list = (await (await t.fetch("/api/devices", { token: cli.token })).json()) as { devices: Json[] };
    expect(list.devices).toEqual([expect.objectContaining({ id: "sim-bench", online: true })]);
  });
});
