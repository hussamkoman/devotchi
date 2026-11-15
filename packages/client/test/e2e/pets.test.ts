// Protocol 14.8 end to end: the CLI uploads a pet, sends it to a sim that
// fetches and checks the pack like the firmware, and the sim's answers for
// every failure it can report.

import { readFileSync } from "node:fs";
import { writeFile } from "node:fs/promises";
import { join } from "node:path";
import { afterEach, describe, expect, it, vi } from "vitest";
import { crc32, crc32Hex, encodePet, type Device, type PetSource, type PetsMessage } from "@devotchi/shared";
import { SimPets } from "../../src/sim-pets.js";
import { requireBuiltCli } from "../helpers.js";
import { makeWorld, startSim, type World } from "./helpers.js";

const repo = new URL("../../../../", import.meta.url);
const devoSource = () => JSON.parse(readFileSync(new URL("pets/devo.pet.json", repo), "utf8")) as PetSource;
const packOf = (id: string, rev = 1) => encodePet({ ...devoSource(), id, name: "Copy", rev });
const crcOf = (pack: Uint8Array) => crc32Hex(crc32(pack.subarray(0, pack.length - 4)));

let world: World | null = null;

afterEach(async () => {
  await world?.close();
  world = null;
});

describe("pets with a sim", () => {
  it("upload -> list -> send -> the sim fetches, validates and reports it installed", async () => {
    requireBuiltCli();
    world = await makeWorld();
    const device = world.t.deviceToken(world.roomId, "sim-pet", "petsim");
    const s = await startSim(world, { name: "pet", token: device.token });
    await s.next("tx", (e) => (e.msg as PetsMessage).type === "pets");

    const file = join(world.box.dir, "whatever-name.pet");
    await writeFile(file, packOf("copy", 2));
    const added = await world.cli(["pets", "add", file]);
    expect(added.stderr).toBe("");
    expect(added.code).toBe(0);
    expect(added.stdout).toContain('Added copy "Copy" rev 2');
    const readded = await world.cli(["pets", "add", file]);
    expect(readded.code).toBe(0);
    expect(readded.stdout).toMatch(/^copy "Copy" rev 2 \(\d+ bytes\) is already in this room, unchanged\./m);

    const listed = await world.cli(["pets"]);
    expect(listed.code).toBe(0);
    expect(listed.stdout).toMatch(/^copy "Copy": rev 2, \d+ bytes, crc32 [0-9a-f]{8}, uploaded to this room$/m);
    expect(listed.stdout).toMatch(/^sprout "Sprout": .* catalog$/m);
    expect(listed.stdout).toMatch(/^devo "Devo": .* catalog, built in$/m);
    expect(listed.stdout).toContain('sim-pet "petsim": shows devo; 8 of 8 slots free; devo (built-in), mochi (built-in), ember (built-in)');

    const sent = await world.cli(["pets", "send", "copy", "--use", "--wait", "10"]);
    expect(sent.stderr).toBe("");
    expect(sent.stdout).toContain("sim-pet installed copy rev 2 and shows it.");
    expect(await s.next("pets", (e) => e.op === "install")).toMatchObject({ op: "install", pet: "copy", ok: true, selected: "copy" });

    const used = await world.cli(["pets", "use", "ember", "--device", "sim-pet"]);
    expect(used.stdout).toContain("sim-pet shows ember.");

    // The device's own answer decides (protocol 14.8 `petsResult`): a failed re-install of the
    // rev it already has is a failure, and an error comes back at once, not after --wait.
    const realFetch = globalThis.fetch;
    const failPack = vi.spyOn(globalThis, "fetch").mockImplementation((input, init) =>
      String(input instanceof Request ? input.url : input).endsWith("/api/pets/copy/pack")
        ? Promise.resolve(new Response("", { status: 503 }))
        : realFetch(input, init),
    );
    try {
      const started = Date.now();
      const resent = await world.cli(["pets", "send", "copy", "--wait", "20"]);
      expect(resent.code).toBe(1);
      expect(resent.stderr).toContain("sim-pet: could not install copy: the download from the server failed");
      expect(Date.now() - started).toBeLessThan(10_000);
    } finally {
      failPack.mockRestore();
    }
    const missing = await world.cli(["pets", "use", "nothere", "--device", "sim-pet", "--wait", "20"]);
    expect(missing.code).toBe(1);
    expect(missing.stderr).toContain("sim-pet: could not switch to nothere: the device does not have that pet");
    const devices = (await world.t.fetch("/api/devices", { token: world.token }).then((r) => r.json())) as { devices: Device[] };
    expect(devices.devices.find((d) => d.id === "sim-pet")?.petsResult).toMatchObject({ op: "select", pet: "nothere", ok: false, error: "unknown" });

    // Devo is built in: the server refuses before the device is asked.
    const devo = await world.cli(["pets", "send", "devo"]);
    expect(devo.code).toBe(1);
    expect(devo.stderr).toContain("devo is built into sim-pet");
    const refused = await world.t.fetch("/api/devices/sim-pet/pet", { method: "POST", token: world.token, json: { op: "install", pet: "devo" } });
    expect(refused.status).toBe(409);
    expect(await refused.json()).toEqual({ error: "built-in pet", code: "conflict" });

    const removed = await world.cli(["pets", "rm", "copy"]);
    expect(removed.code).toBe(0);
    const again = await world.cli(["pets", "rm", "copy"]);
    expect(again.code).toBe(1);
    expect(again.stderr).toContain("no uploaded pet copy");
  });

  it("pets add takes a JSON source and reports the codec's message", async () => {
    requireBuiltCli();
    world = await makeWorld();
    const good = join(world.box.dir, "pet.pet.json");
    await writeFile(good, JSON.stringify({ ...devoSource(), id: "fromjson" }));
    const ok = await world.cli(["pets", "add", good]);
    expect(ok.stdout).toContain('Added fromjson "Devo"');
    const bad = join(world.box.dir, "bad.pet.json");
    await writeFile(bad, JSON.stringify({ ...devoSource(), id: "bad", rev: 300 }));
    const refused = await world.cli(["pets", "add", bad]);
    expect(refused.code).toBe(1);
    expect(refused.stderr).toContain("rev: 300 is not an integer in 1..255");
  });
});

describe("the sim's pet store", () => {
  function store(serve: (id: string) => Response | Promise<Response>) {
    const sent: PetsMessage[] = [];
    const pets = new SimPets({ fetchPack: async (id) => serve(id), send: (m) => sent.push(m), emit: () => undefined });
    const last = () => sent.at(-1)!;
    return { pets, sent, last };
  }
  const packResponse = (bytes: Uint8Array, length = bytes.length, status = 200) =>
    new Response(new Uint8Array(bytes).buffer, { status, headers: { "Content-Length": String(length) } });
  const install = (pack: Uint8Array, extra: Record<string, unknown> = {}) => ({
    type: "pet_cmd",
    op: "install",
    pet: "copy",
    rev: pack[5],
    size: pack.length,
    crc32: crcOf(pack),
    select: false,
    ...extra,
  });

  it("installs, selects and removes, keeping the selection rules", async () => {
    const pack = packOf("copy", 3);
    const { pets, last } = store(() => packResponse(pack));
    pets.report();
    expect(last()).toEqual({
      type: "pets",
      selected: "devo",
      slots: 8,
      free: 8,
      installed: [
        { id: "devo", rev: 4, builtin: true },
        { id: "mochi", rev: 1, builtin: true },
        { id: "ember", rev: 1, builtin: true },
      ],
    });
    await pets.onCommand(install(pack, { select: true }));
    expect(last()).toMatchObject({ selected: "copy", free: 7, result: { op: "install", pet: "copy", ok: true } });
    expect(last().installed.at(-1)).toEqual({ id: "copy", rev: 3 });
    await pets.onCommand({ type: "pet_cmd", op: "remove", pet: "copy" });
    expect(last()).toMatchObject({ selected: "devo", free: 8, result: { op: "remove", pet: "copy", ok: true } });
    await pets.onCommand({ type: "pet_cmd", op: "select", pet: "copy" });
    expect(last().result).toEqual({ op: "select", pet: "copy", ok: false, error: "unknown" });
    await pets.onCommand({ type: "pet_cmd", op: "remove", pet: "ember" });
    expect(last().result).toEqual({ op: "remove", pet: "ember", ok: false, error: "builtin" });
  });

  it("answers crc for a damaged pack, mismatch for another rev and builtin for devo", async () => {
    const pack = packOf("copy", 2);
    const damaged = pack.slice();
    damaged[60] ^= 0x01;
    const crc = store(() => packResponse(damaged));
    await crc.pets.onCommand(install(pack));
    expect(crc.last().result).toEqual({ op: "install", pet: "copy", ok: false, error: "crc" });
    expect(crc.last().installed.map((p) => p.id)).toEqual(["devo", "mochi", "ember"]);

    const mismatch = store(() => packResponse(pack));
    await mismatch.pets.onCommand(install(pack, { rev: 3 }));
    expect(mismatch.last().result).toEqual({ op: "install", pet: "copy", ok: false, error: "mismatch" });
    const otherId = store(() => packResponse(packOf("other", 2)));
    await otherId.pets.onCommand(install(packOf("other", 2)));
    expect(otherId.last().result).toMatchObject({ error: "mismatch" });

    let fetched = false;
    const builtin = store(() => {
      fetched = true;
      return packResponse(pack);
    });
    await builtin.pets.onCommand(install(pack, { pet: "devo" }));
    expect(builtin.last().result).toEqual({ op: "install", pet: "devo", ok: false, error: "builtin" });
    expect(fetched).toBe(false);
  });

  it("answers http, size, format, unknown, full and busy like the firmware", async () => {
    const pack = packOf("copy");
    const http = store(() => new Response("{}", { status: 404 }));
    await http.pets.onCommand(install(pack));
    expect(http.last().result?.error).toBe("http");

    const size = store(() => packResponse(pack, pack.length + 1));
    await size.pets.onCommand(install(pack));
    expect(size.last().result?.error).toBe("size");

    const notAPack = new Uint8Array(pack.length);
    notAPack.set([1, 2, 3]);
    const sealed = notAPack.slice();
    const c = crc32(sealed.subarray(0, sealed.length - 4));
    sealed.set([c, c >>> 8, c >>> 16, c >>> 24], sealed.length - 4);
    const format = store(() => packResponse(sealed));
    await format.pets.onCommand(install(sealed, { rev: 1 }));
    expect(format.last().result?.error).toBe("format");

    const malformed = store(() => packResponse(pack));
    for (const bad of [{ pet: "../etc" }, { rev: 0 }, { size: 9000 }, { crc32: "ABCDEF12" }, { op: "wear" }]) {
      await malformed.pets.onCommand(install(pack, bad));
      expect(malformed.last().result?.error).toBe("unknown");
    }
    expect(malformed.last().result).toMatchObject({ op: "wear", pet: "copy" });
    await malformed.pets.onCommand(install(pack, { pet: "../etc" }));
    expect(malformed.last().result?.pet).toBe("");

    const full = store((id) => packResponse(packOf(id)));
    for (let i = 0; i < 8; i++) {
      const p = packOf(`p${i}`);
      await full.pets.onCommand(install(p, { pet: `p${i}` }));
      expect(full.last().result?.ok).toBe(true);
    }
    await full.pets.onCommand(install(packOf("p8"), { pet: "p8" }));
    expect(full.last()).toMatchObject({ free: 0, result: { ok: false, error: "full" } });

    let release!: () => void;
    const slow = store(() => new Promise<Response>((resolve) => (release = () => resolve(packResponse(pack)))));
    const first = slow.pets.onCommand(install(pack));
    await slow.pets.onCommand(install(pack));
    expect(slow.last().result?.error).toBe("busy");
    release();
    await first;
    expect(slow.last().result?.ok).toBe(true);
  });
});
