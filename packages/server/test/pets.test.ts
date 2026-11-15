// Protocol 14.8 on the server: catalog, room uploads, packs, the `pets`
// message and `pet_cmd`, with the status and code of every error in the table.

import { readFileSync } from "node:fs";
import { afterEach, describe, expect, it } from "vitest";
import { crc32, encodePet, type PetSource } from "@devotchi/shared";
import { TestSocket, consumer, startTestServer, type Json, type TestServer } from "./helpers.js";
import { until } from "./hook-helpers.js";

const repo = new URL("../../../", import.meta.url);
const golden = (id: string) => new Uint8Array(readFileSync(new URL(`pets/golden/${id}.pet`, repo)));
const goldenCrc = (id: string) => readFileSync(new URL(`pets/golden/${id}.crc32`, repo), "utf8").trim();
const sproutSource = () => JSON.parse(readFileSync(new URL("pets/sprout.pet.json", repo), "utf8")) as PetSource;

/** A pack of `id` built from Devo's source. */
function packOf(id: string, rev = 1, name = "Copy"): Uint8Array {
  const src = JSON.parse(readFileSync(new URL("pets/devo.pet.json", repo), "utf8")) as PetSource;
  return encodePet({ ...src, id, name, rev });
}

let ts: TestServer | undefined;
const sockets: TestSocket[] = [];

afterEach(async () => {
  for (const socket of sockets.splice(0)) socket.close();
  await ts?.close();
  ts = undefined;
});

async function track(socket: Promise<TestSocket>): Promise<TestSocket> {
  const s = await socket;
  sockets.push(s);
  return s;
}

async function setup() {
  ts = await startTestServer();
  const t = ts;
  const { token, roomId } = await t.mintToken();
  const upload = (bytes: Uint8Array, tok = token) =>
    t.fetch("/api/pets", { method: "POST", token: tok, headers: { "Content-Type": "application/octet-stream" }, body: Buffer.from(bytes) });
  const command = (deviceId: string, body: unknown, tok = token) =>
    t.fetch(`/api/devices/${deviceId}/pet`, { method: "POST", token: tok, json: body });
  return { t, token, roomId, upload, command };
}

async function expectError(res: Response, status: number, code: string, error?: RegExp | string): Promise<void> {
  expect(res.status).toBe(status);
  const body = (await res.json()) as { error: string; code: string };
  expect(body.code).toBe(code);
  if (error !== undefined) expect(body.error).toMatch(error);
}

const PETS_STATE = {
  type: "pets",
  selected: "devo",
  slots: 8,
  free: 8,
  installed: [
    { id: "devo", rev: 1, builtin: true },
    { id: "ember", rev: 1, builtin: true },
  ],
};

describe("GET /api/pets", () => {
  it("lists the catalog with the golden CRCs, for cli and device tokens", async () => {
    const { t, token, roomId } = await setup();
    const res = await t.fetch("/api/pets", { token });
    expect(res.status).toBe(200);
    const { pets } = (await res.json()) as { pets: Json[] };
    expect(pets.map((p) => p.id)).toEqual(["devo", "ember", "sprout"]);
    expect(pets[0]).toEqual({ id: "devo", name: "Devo", rev: 1, size: 344, crc32: goldenCrc("devo"), source: "catalog", builtin: true });
    expect(pets[1]).toMatchObject({ id: "ember", crc32: goldenCrc("ember"), source: "catalog", builtin: true });
    expect(pets[2]).toEqual({ id: "sprout", name: "Sprout", rev: 1, size: encodePet(sproutSource()).length, crc32: goldenCrc("sprout"), source: "catalog" });
    const device = t.deviceToken(roomId);
    expect((await t.fetch("/api/pets", { token: device.token })).status).toBe(200);
    await expectError(await t.fetch("/api/pets"), 401, "unauthorized");
  });
});

describe("GET /api/pets/:id/pack", () => {
  it("serves the pack with Content-Length, ETag and octet-stream", async () => {
    const { t, token } = await setup();
    const res = await t.fetch("/api/pets/ember/pack", { token });
    expect(res.status).toBe(200);
    expect(res.headers.get("content-type")).toBe("application/octet-stream");
    expect(res.headers.get("content-length")).toBe(String(golden("ember").length));
    expect(res.headers.get("etag")).toBe(`"${goldenCrc("ember")}"`);
    expect(Buffer.from(await res.arrayBuffer()).equals(Buffer.from(golden("ember")))).toBe(true);
  });

  it("is room-scoped for uploads: device tokens of the room read them, other rooms get 404", async () => {
    const { t, token, roomId, upload } = await setup();
    expect((await upload(packOf("mine"))).status).toBe(201);
    const device = t.deviceToken(roomId);
    const own = await t.fetch("/api/pets/mine/pack", { token: device.token });
    expect(own.status).toBe(200);
    expect(Buffer.from(await own.arrayBuffer()).equals(Buffer.from(packOf("mine")))).toBe(true);
    const other = await t.mintToken();
    const otherDevice = t.deviceToken(other.roomId);
    await expectError(await t.fetch("/api/pets/mine/pack", { token: other.token }), 404, "not_found");
    await expectError(await t.fetch("/api/pets/mine/pack", { token: otherDevice.token }), 404, "not_found");
    await expectError(await t.fetch("/api/pets/nope/pack", { token }), 404, "not_found");
    await expectError(await t.fetch("/api/pets/BAD!/pack", { token }), 404, "not_found");
  });

  it("never serves a stored pack that fails validation", async () => {
    const { t, token, roomId, upload } = await setup();
    expect((await upload(packOf("mine"))).status).toBe(201);
    const broken = packOf("mine");
    broken[40] ^= 1;
    t.server.internals.db.run("UPDATE pets SET bytes = ? WHERE room_id = ? AND id = 'mine'", broken, roomId);
    await expectError(await t.fetch("/api/pets/mine/pack", { token }), 404, "not_found");
    const { pets } = (await (await t.fetch("/api/pets", { token })).json()) as { pets: Json[] };
    expect(pets.map((p) => p.id)).not.toContain("mine");
  });
});

describe("POST /api/pets", () => {
  it("stores a pack or a JSON source for the room; the header id is the id", async () => {
    const { t, token, upload } = await setup();
    const pack = packOf("copy", 3, "Copy Cat");
    const res = await upload(pack);
    expect(res.status).toBe(201);
    expect(await res.json()).toEqual({
      id: "copy",
      name: "Copy Cat",
      rev: 3,
      size: pack.length,
      crc32: crc32(pack.subarray(0, pack.length - 4)).toString(16).padStart(8, "0"),
      source: "room",
    });
    expect((await upload(pack)).status).toBe(200);
    const fromJson = await t.fetch("/api/pets", { method: "POST", token, json: { ...sproutSource(), id: "sprout2" } });
    expect(fromJson.status).toBe(201);
    expect(await fromJson.json()).toMatchObject({ id: "sprout2", source: "room" });
    const { pets } = (await (await t.fetch("/api/pets", { token })).json()) as { pets: Json[] };
    expect(pets.map((p) => `${p.id}:${p.source}`)).toEqual(["devo:catalog", "ember:catalog", "sprout:catalog", "copy:room", "sprout2:room"]);
  });

  it("answers every error of the 14.8 table with its code", async () => {
    const { t, token, roomId, upload } = await setup();
    const badCrc = packOf("bad");
    badCrc[50] ^= 0x10;
    await expectError(await upload(badCrc), 400, "invalid_request", /crc/i);
    await expectError(await upload(new Uint8Array(10)), 400, "invalid_request", /10 bytes/);
    await expectError(
      await t.fetch("/api/pets", { method: "POST", token, json: { ...sproutSource(), id: "Upper" } }),
      400,
      "invalid_request",
      /^Pet source refused: id: "Upper"/,
    );
    await expectError(await upload(new Uint8Array(2049)), 413, "payload_too_large");
    await expectError(
      await t.fetch("/api/pets", { method: "POST", token, json: { ...sproutSource(), pad: "x".repeat(17 * 1024) } }),
      413,
      "payload_too_large",
    );
    await expectError(
      await t.fetch("/api/pets", { method: "POST", token, headers: { "Content-Type": "text/plain" }, body: "hi" }),
      400,
      "invalid_request",
    );
    await expectError(await t.fetch("/api/pets", { method: "POST", token }), 400, "invalid_request");
    await expectError(await upload(golden("devo")), 409, "conflict", /catalog/);
    expect((await upload(packOf("same", 2))).status).toBe(201);
    await expectError(await upload(packOf("same", 2, "Changed")), 409, "conflict", /higher rev/);
    expect((await upload(packOf("same", 3, "Changed"))).status).toBe(201);
    for (let i = 1; i < 16; i++) expect((await upload(packOf(`p${i}`))).status).toBe(201);
    await expectError(await upload(packOf("p16")), 409, "conflict", /16 pets/);
    const device = t.deviceToken(roomId);
    await expectError(await upload(packOf("dev"), device.token), 403, "forbidden");
  });
});

describe("DELETE /api/pets/:id", () => {
  it("deletes room packs only", async () => {
    const { t, token, roomId, upload } = await setup();
    expect((await upload(packOf("gone"))).status).toBe(201);
    const device = t.deviceToken(roomId);
    await expectError(await t.fetch("/api/pets/gone", { method: "DELETE", token: device.token }), 403, "forbidden");
    expect((await t.fetch("/api/pets/gone", { method: "DELETE", token })).status).toBe(204);
    await expectError(await t.fetch("/api/pets/gone", { method: "DELETE", token }), 404, "not_found");
    await expectError(await t.fetch("/api/pets/ember", { method: "DELETE", token }), 409, "conflict");
    const other = await t.mintToken();
    expect((await upload(packOf("theirs"), other.token)).status).toBe(201);
    await expectError(await t.fetch("/api/pets/theirs", { method: "DELETE", token }), 404, "not_found");
  });
});

describe("pets message", () => {
  it("is kept per device, listed without result and forwarded to dashboards with device", async () => {
    const { t, token, roomId } = await setup();
    const device = t.deviceToken(roomId, "dv-0a1b2c");
    const dash = await track(consumer(t, token, { kind: "dashboard", caps: ["decide", "pets"] as never }));
    const ack = dash.received.find((m) => m.type === "register_ack")!;
    expect(ack.caps).toContain("pets");
    const d = await track(consumer(t, device.token, { kind: "device", deviceId: device.deviceId, caps: ["pets"] as never }));
    d.send({ ...PETS_STATE, result: { op: "select", pet: "ember", ok: false, error: "unknown" } });
    const forwarded = await dash.awaitMessage("pets");
    expect(forwarded).toEqual({ ...PETS_STATE, device: "dv-0a1b2c", result: { op: "select", pet: "ember", ok: false, error: "unknown" } });
    const { devices } = (await (await t.fetch("/api/devices", { token })).json()) as { devices: Json[] };
    const { type: _type, ...state } = PETS_STATE;
    expect(devices[0].pets).toEqual(state);
    // A dashboard cannot send pets, and the cap is not granted to it.
    dash.send(PETS_STATE);
    expect(await dash.awaitMessage("error")).toMatchObject({ code: "NOT_ALLOWED" });
    expect(await (await t.fetch(`/api/devices/${device.deviceId}/pet`, { method: "POST", token, json: { op: "select", pet: "ember" } })).status).toBe(202);
    expect(await d.awaitMessage("pet_cmd")).toEqual({ type: "pet_cmd", op: "select", pet: "ember" });
  });

  it("is refused when malformed or over 700 bytes", async () => {
    const { t, roomId } = await setup();
    const device = t.deviceToken(roomId);
    const d = await track(consumer(t, device.token, { kind: "device", deviceId: device.deviceId, caps: ["pets"] as never }));
    d.send({ ...PETS_STATE, selected: "Devo" });
    expect(await d.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE" });
    d.send({ ...PETS_STATE, result: { op: "install", pet: "x", ok: false } });
    expect(await d.awaitMessage("error")).toMatchObject({ code: "INVALID_MESSAGE" });
    d.send({ ...PETS_STATE, installed: Array.from({ length: 24 }, (_, i) => ({ id: `pet${i + 10}abc`, rev: 200 })) });
    expect(await d.awaitMessage("error")).toMatchObject({ code: "MESSAGE_TOO_LARGE" });
  });
});

describe("POST /api/devices/:id/pet", () => {
  it("sends pet_cmd with rev, size and crc32 of the stored pack", async () => {
    const { t, roomId, upload, command } = await setup();
    const pack = packOf("mine", 7);
    expect((await upload(pack)).status).toBe(201);
    const device = t.deviceToken(roomId);
    const d = await track(consumer(t, device.token, { kind: "device", deviceId: device.deviceId, caps: ["pets"] as never }));
    d.send(PETS_STATE);
    const res = await command(device.deviceId, { op: "install", pet: "mine", select: true });
    expect(res.status).toBe(202);
    expect(await res.json()).toEqual({ sent: true });
    expect(await d.awaitMessage("pet_cmd")).toEqual({
      type: "pet_cmd",
      op: "install",
      pet: "mine",
      rev: 7,
      size: pack.length,
      crc32: crc32(pack.subarray(0, pack.length - 4)).toString(16).padStart(8, "0"),
      select: true,
    });
    expect((await command(device.deviceId, { op: "install", pet: "sprout" })).status).toBe(202);
    expect(await d.awaitMessage("pet_cmd")).toMatchObject({ op: "install", pet: "sprout", select: false });
    expect((await command(device.deviceId, { op: "remove", pet: "mine" })).status).toBe(202);
    expect(await d.awaitMessage("pet_cmd")).toEqual({ type: "pet_cmd", op: "remove", pet: "mine" });
  });

  it("answers every error of the 14.8 table with its code", async () => {
    const { t, token, roomId, command } = await setup();
    const device = t.deviceToken(roomId);
    // `pet` is checked before the device is looked up.
    await expectError(await command("dv-ffffff", { op: "install", pet: "../x" }), 400, "invalid_request", /\[a-z0-9\]/);
    await expectError(await command(device.deviceId, { op: "install", pet: "Mochi" }), 400, "invalid_request");
    await expectError(await command(device.deviceId, { op: "install", pet: "toolongpet" }), 400, "invalid_request");
    await expectError(await command(device.deviceId, { op: "wear", pet: "sprout" }), 400, "invalid_request", /op/);
    await expectError(await command(device.deviceId, { op: "install", pet: "sprout", select: "yes" }), 400, "invalid_request");
    await expectError(await command(device.deviceId, { op: "install", pet: "sprout", rev: 9 }), 400, "invalid_request", /rev/);
    await expectError(await command(device.deviceId, "nope"), 400, "invalid_request");
    await expectError(await command("dv-ffffff", { op: "select", pet: "sprout" }), 404, "not_found", /device/i);
    await expectError(await command(device.deviceId, { op: "select", pet: "sprout" }), 409, "conflict", "device offline");
    const old = await track(consumer(t, device.token, { kind: "device", deviceId: device.deviceId }));
    await expectError(await command(device.deviceId, { op: "select", pet: "sprout" }), 409, "conflict", "device does not support pets");
    old.close();
    await until(async () => (await command(device.deviceId, { op: "select", pet: "sprout" })).status === 409 &&
      ((await (await command(device.deviceId, { op: "select", pet: "sprout" })).json()) as Json).error === "device offline");
    const d = await track(consumer(t, device.token, { kind: "device", deviceId: device.deviceId, caps: ["pets"] as never }));
    d.send(PETS_STATE);
    await until(async () => {
      const { devices } = (await (await t.fetch("/api/devices", { token })).json()) as { devices: Json[] };
      return devices[0].pets !== undefined;
    });
    await expectError(await command(device.deviceId, { op: "install", pet: "devo" }), 409, "conflict", "built-in pet");
    await expectError(await command(device.deviceId, { op: "remove", pet: "ember" }), 409, "conflict", "built-in pet");
    await expectError(await command(device.deviceId, { op: "install", pet: "nosuch" }), 404, "not_found", /pet/);
    const other = await t.mintToken();
    await expectError(await command(device.deviceId, { op: "select", pet: "devo" }, other.token), 404, "not_found");
    await expectError(await command(device.deviceId, { op: "select", pet: "devo" }, device.token), 403, "forbidden");
    expect((await command(device.deviceId, { op: "select", pet: "devo" })).status).toBe(202);
  });
});
