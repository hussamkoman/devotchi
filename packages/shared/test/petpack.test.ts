import { execFileSync } from "node:child_process";
import { readFileSync, readdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import {
  PET_MAX_BYTES,
  PET_MIN_BYTES,
  PetSourceError,
  crc32,
  crc32Hex,
  decodePet,
  encodePet,
  parsePet,
  petBodyColor,
  petDark,
  PET_MOODS,
  petPose,
  renderPetSheetSvg,
  renderPetSvg,
  validatePet,
  validatePetSource,
  type PetSource,
} from "../src/index.js";

const repo = new URL("../../../", import.meta.url);
const read = (path: string) => readFileSync(new URL(path, repo));
const source = (id: string): PetSource => JSON.parse(read(`pets/${id}.pet.json`).toString("utf8")) as PetSource;
const golden = (id: string) => new Uint8Array(read(`pets/golden/${id}.pet`));
const clone = <T>(v: T): T => structuredClone(v);
/** Every pet of pets/ (files starting with _ are templates). */
const PET_IDS = readdirSync(new URL("pets/", repo))
  .filter((f) => f.endsWith(".pet.json") && !f.startsWith("_"))
  .map((f) => f.slice(0, -".pet.json".length));

/** Re-seals a modified pack so only the structure checks see the change. */
function reseal(pack: Uint8Array): Uint8Array {
  const out = pack.slice();
  const crc = crc32(out.subarray(0, out.length - 4));
  out.set([crc, crc >>> 8, crc >>> 16, crc >>> 24], out.length - 4);
  return out;
}

/**
 * What a pack means. Two packs can differ in bytes and mean the same: bit 7 of
 * a driver's op byte is ignored by the firmware (and by this codec; the
 * encoder writes 0), and the JSON source keeps one override per mood and
 * channel.
 */
function semantic(pack: Uint8Array): Omit<ReturnType<typeof parsePet>, "crc32" | "size" | "overrides"> & { overrides: string[] } {
  const { crc32: _crc, size: _size, overrides, ...rest } = parsePet(pack);
  // Overrides apply in order, so a later entry for the same mood and channel wins.
  const effective = new Map(overrides.map((o) => [`${o.mood}/${o.channel}`, o.value]));
  return { ...rest, overrides: [...effective].map(([k, v]) => `${k}=${v}`).sort() };
}

function lcg(seed: number): () => number {
  let s = seed >>> 0;
  return () => {
    s = (Math.imul(s, 1664525) + 1013904223) >>> 0;
    return s;
  };
}

describe("crc32", () => {
  it("is the zlib polynomial", () => {
    expect(crc32Hex(crc32(new TextEncoder().encode("123456789")))).toBe("cbf43926");
    expect(crc32(new Uint8Array(0))).toBe(0);
    expect(crc32Hex(crc32(new TextEncoder().encode("The quick brown fox jumps over the lazy dog")))).toBe("414fa339");
  });
});

describe.each(PET_IDS)("golden pack %s", (id) => {
  it("encodes byte for byte as petpack.py", () => {
    const pack = encodePet(source(id));
    expect(Buffer.from(pack).equals(Buffer.from(golden(id)))).toBe(true);
    expect(crc32Hex(crc32(pack.subarray(0, pack.length - 4)))).toBe(read(`pets/golden/${id}.crc32`).toString("ascii").trim());
  });

  it("validates and reports the header", () => {
    const result = validatePet(golden(id));
    expect(result).toEqual({
      ok: true,
      pet: { id, name: source(id).name, rev: source(id).rev, size: golden(id).length, crc32: read(`pets/golden/${id}.crc32`).toString().trim() },
    });
  });

  it("round-trips through decodePet", () => {
    const decoded = decodePet(golden(id));
    expect(Buffer.from(encodePet(decoded)).equals(Buffer.from(golden(id)))).toBe(true);
    expect(Buffer.from(encodePet(JSON.parse(JSON.stringify(decoded)))).equals(Buffer.from(golden(id)))).toBe(true);
  });

  it("refuses every truncation and every extension", () => {
    const pack = golden(id);
    for (let len = 0; len < pack.length; len++) {
      const result = validatePet(pack.subarray(0, len));
      expect(result.ok, `length ${len}`).toBe(false);
      // A truncated pack resealed with a valid CRC is still refused for its structure.
      if (len >= PET_MIN_BYTES + 4) expect(validatePet(reseal(pack.subarray(0, len))).ok, `resealed ${len}`).toBe(false);
    }
    const longer = new Uint8Array(pack.length + 1);
    longer.set(pack);
    expect(validatePet(longer).ok).toBe(false);
    expect(validatePet(reseal(longer)).ok).toBe(false);
  });

  it("refuses every single bit flip with crc, and resealed flips never decode inconsistently", () => {
    const pack = golden(id);
    for (let i = 0; i < pack.length; i++) {
      for (let bit = 0; bit < 8; bit++) {
        const flipped = pack.slice();
        flipped[i] ^= 1 << bit;
        const result = validatePet(flipped);
        expect(result.ok).toBe(false);
        if (i < pack.length - 4 && !result.ok) expect(result.error).toBe("crc");
        // With a fresh CRC the structure checks decide; whatever they accept must re-encode to the same bytes.
        const sealed = reseal(flipped);
        const again = validatePet(sealed);
        if (again.ok) expect(semantic(encodePet(decodePet(sealed)))).toEqual(semantic(sealed));
        else expect(again.error).toBe("format");
      }
    }
  });
});

describe("validatePet", () => {
  it("names the failing check", () => {
    const pack = golden("devo");
    const cases: [Uint8Array, string | RegExp][] = [
      [reseal(Uint8Array.from(pack, (b, i) => (i === 0 ? 0x58 : b))), /DPET/],
      [reseal(Uint8Array.from(pack, (b, i) => (i === 4 ? 2 : b))), /format 2/],
      [reseal(Uint8Array.from(pack, (b, i) => (i === 5 ? 0 : b))), /rev/],
      [reseal(Uint8Array.from(pack, (b, i) => (i === 6 ? b + 1 : b))), /header size/],
      [reseal(Uint8Array.from(pack, (b, i) => (i === 8 ? 0x41 : b))), /id has a character/],
      [reseal(Uint8Array.from(pack, (b, i) => (i === 28 ? 4 : b))), /reserved flag/],
      [reseal(Uint8Array.from(pack, (b, i) => (i === 28 ? 3 : b))), /both shift and glow/],
      [reseal(Uint8Array.from(pack, (b, i) => (i === 88 ? 4 : b))), /face value/],
      [reseal(Uint8Array.from(pack, (b, i) => (i === 29 ? 0 : b))), /parts/],
      [reseal(Uint8Array.from(pack, (b, i) => (i === 47 ? 1 : b))), /reserved byte/],
      [reseal(Uint8Array.from(pack, (b, i) => (i === 96 ? 1 : b))), /acc_mask/],
    ];
    for (const [bytes, message] of cases) {
      const result = validatePet(bytes);
      expect(result.ok).toBe(false);
      if (!result.ok) {
        expect(result.error).toBe("format");
        expect(result.message).toMatch(message);
      }
    }
    expect(validatePet(new Uint8Array(PET_MAX_BYTES + 1))).toMatchObject({ ok: false, error: "format" });
  });

  it("survives random input", () => {
    const next = lcg(7);
    for (let n = 0; n < 2000; n++) {
      const len = next() % (PET_MAX_BYTES + 64);
      const bytes = Uint8Array.from({ length: len }, () => next() & 0xff);
      if (n % 2 === 0 && len >= 8) bytes.set([0x44, 0x50, 0x45, 0x54, 1, 1, len & 0xff, len >> 8]);
      const input = n % 4 === 0 && len >= 4 ? reseal(bytes) : bytes;
      if (validatePet(input).ok) expect(semantic(encodePet(decodePet(input)))).toEqual(semantic(input));
    }
  });
});

describe("encodePet source checks", () => {
  const base = () => source("mochi") as unknown as Record<string, unknown>;
  const fails = (mutate: (s: Record<string, unknown>) => void, message: RegExp) => {
    const s = clone(base());
    mutate(s);
    expect(() => encodePet(s)).toThrow(PetSourceError);
    expect(() => encodePet(s)).toThrow(message);
  };

  it("gives useful messages", () => {
    fails((s) => (s.id = "Devo"), /^id: "Devo" is not \[a-z0-9\]\{1,8\}/);
    fails((s) => (s.name = "A name that is far too long"), /^name: /);
    fails((s) => (s.rev = 0), /^rev: 0 is not an integer in 1\.\.255/);
    fails((s) => (s.tagline = "x".repeat(33)), /^tagline: "x+" is not 0\.\.32 printable ASCII characters/);
    fails((s) => (s.tagline = 7), /^tagline: 7 is not/);
    fails((s) => ((s.palette as Record<string, unknown>).base = "red"), /^palette\.base: "red" is not a #RRGGBB colour/);
    fails((s) => ((s.palette as Record<string, unknown>).mode = "neon"), /^palette\.mode: "neon" is not one of table, shift, glow/);
    fails((s) => delete ((s.palette as Record<string, Record<string, unknown>>).moods.sad), /^palette\.moods\.sad: is missing/);
    fails((s) => ((s.face as Record<string, unknown>).eyes = "square"), /^face\.eyes: "square" is not one of round, slit, dot/);
    fails((s) => ((s.face as Record<string, unknown>).eye_dx = 61), /^face\.eye_dx: 61 is not an integer in 4\.\.60/);
    fails((s) => ((s.face as Record<string, unknown>).replaces = ["none"]), /cannot be replaced/);
    fails((s) => ((s.aux as Record<string, unknown>).happy = [1, 2, 3]), /^aux\.happy: needs 4 values/);
    fails((s) => ((s.aux as Record<string, unknown>).hungry = [0, 0, 0, 0]), /^aux\.hungry: unknown field/);
    fails((s) => (s.overrides = { idle: { eye_open: 101 } }), /^overrides\.idle\.eye_open: 101 is not an integer in 0\.\.100/);
    fails((s) => (s.overrides = { idle: { wink: 1 } }), /^overrides\.idle\.wink: "wink" is not one of eye_open/);
    fails((s) => (s.overrides = { lunch: { eye_open: 1 } }), /^overrides\.lunch: "lunch" is not a mood/);
    fails((s) => (s.oscillators = [{ mood: "idle", aux: 0, wave: "sine", amp: 3 }]), /^oscillators\[0\]\.period_ms: a sine oscillator needs a period/);
    fails((s) => (s.oscillators = [{ mood: "idle", aux: 0, wave: "sine", amp: 3, period_ms: 125 }]), /not a multiple of 50/);
    fails((s) => ((s.parts as Record<string, unknown>[])[0].r = 7), /^parts\[0\]\.r: 7 is not an integer in 0\.\.6/);
    fails((s) => ((s.parts as Record<string, unknown>[])[2].points = [[0, 0]]), /^parts\[2\]\.points: must be a list of 2\.\.4/);
    fails((s) => ((s.parts as Record<string, unknown>[])[2].points = [[0, 0], [0, 200]]), /^parts\[2\]\.points\[1\]\[1\]: 200 is not an integer in -128\.\.127/);
    fails((s) => ((s.parts as Record<string, unknown>[])[2].ring = true), /ring and squash are for rrect parts only/);
    fails((s) => ((s.parts as Record<string, unknown>[])[6].anchor = "nope"), /^parts\[6\]\.anchor: "nope" is not the name of an earlier line part/);
    fails((s) => ((s.parts as Record<string, unknown>[])[6].drivers = [{ op: "move_x", aux: 0, point: 0 }]), /a rrect moves as a whole/);
    fails((s) => ((s.parts as Record<string, unknown>[])[0].colour = "red"), /^parts\[0\]\.colour: unknown field/);
    fails((s) => (s.parts = []), /^parts: must be a list of 1\.\.28 parts/);
    fails((s) => (s.extra = 1), /^pet\.extra: unknown field/);
  });

  it("refuses more overrides than format 1 holds", () => {
    const s = clone(base());
    const row = Object.fromEntries(["eye_open", "eye_size", "pupil_size", "pupil_dx", "pupil_dy", "brow_angle", "mouth_curve"].map((c) => [c, 10]));
    s.overrides = Object.fromEntries(["happy", "sad", "excited", "anxious", "sleeping", "angry", "proud"].map((m) => [m, row]));
    expect(() => encodePet(s)).toThrow(/^overrides: 49 overrides; at most 48/);
  });

  it("validatePetSource returns the source", () => {
    const s = source("ember");
    expect(validatePetSource(s)).toBe(s);
  });

  it("lists every pet with its golden pack, and the template is a valid source", () => {
    expect(PET_IDS).toEqual(expect.arrayContaining(["devo", "mochi", "ember", "sprout"]));
    const template = JSON.parse(read("pets/_template.pet.json").toString("utf8")) as unknown;
    const pack = encodePet(template);
    expect(pack.length).toBeLessThan(PET_MAX_BYTES);
    expect(parsePet(pack).parts.length).toBeLessThanOrEqual(28);
  });
});

describe("renderPetSvg", () => {
  it.each(["devo", "mochi", "ember"])("draws %s idle, happy and sleeping", (id) => {
    for (const mood of ["idle", "happy", "sleeping"] as const) {
      expect(renderPetSvg(golden(id), mood)).toMatchSnapshot(`${id}-${mood}`);
    }
  });

  it("uses the mood colour, the derived shades and only numbers", () => {
    const devo = renderPetSvg(golden("devo"), "happy");
    expect(devo).toContain('rx="50" fill="#2A3550"');
    expect(devo).toContain('fill="#FFD98C"');
    expect(devo.startsWith('<svg xmlns="http://www.w3.org/2000/svg" width="220" height="190"')).toBe(true);
    expect(devo).not.toMatch(/Devo/);
    const ember = renderPetSvg(golden("ember"), "idle", { width: 110, height: 95, gray: true, idPrefix: "x" });
    expect(ember).toContain('fill="#2FAE9C"');
    expect(ember).toContain("grayscale(1)");
    expect(ember).toContain('id="x-c0"');
  });

  it("draws the face lines in cream on a body too dark for ink", () => {
    expect(petDark(0x2a3550)).toBe(true);
    expect(petDark(0x3f3f3f)).toBe(true);
    expect(petDark(0x404040)).toBe(false);
    for (const id of ["mochi", "ember", "sprout"]) {
      const def = parsePet(golden(id));
      for (let m = 0; m < PET_MOODS.length; m++) expect(petDark(petBodyColor(def, m)), `${id} ${PET_MOODS[m]}`).toBe(false);
    }
    const line = (color: string) => `stroke="${color}" stroke-width="4"`;
    // Angry Devo: brows and mouth in cream, pupils still ink inside the white eyes with a glint.
    const dark = renderPetSvg(golden("devo"), "angry");
    expect(dark.match(new RegExp(line("#F4F1EA"), "g"))).toHaveLength(4);
    expect(dark).not.toContain(line("#1A1D26"));
    expect(dark.match(/<circle [^>]*fill="#1A1D26"/g)).toHaveLength(2);
    expect(dark.match(/<circle [^>]*fill="#FFFFFF"/g)).toHaveLength(2);
    // The same pack on a light body goes back to ink.
    const src = source("devo");
    const light = renderPetSvg(encodePet({ ...src, palette: { ...src.palette, base: "#F4F1EA", cream: "#2A3550" } }), "angry");
    expect(light.match(new RegExp(line("#1A1D26"), "g"))).toHaveLength(3);
    // The lid edge of focused is the light shade on the dark body.
    expect(renderPetSvg(golden("devo"), "focused")).toContain(line("#7F8696"));
  });

  it("applies aux drivers: the shown antenna stem follows the mood", () => {
    // Mochi's stems are variants chosen by aux 2: 0 curve (idle), 3 straight (focused).
    const idle = renderPetSvg(golden("mochi"), "idle");
    const focused = renderPetSvg(golden("mochi"), "focused");
    expect(idle).toContain('points="0,-55 0,-67 4.38,-77 10,-81"');
    expect(focused).toContain('points="0,-55 0,-88"');
  });

  it("lights Devo's chip while aux 3 is nonzero and moves the antenna tip", () => {
    const light = (color: string) => `<rect x="23" y="25" width="8" height="10" rx="2" fill="${color}"/>`;
    expect(renderPetSvg(golden("devo"), "idle")).toContain(light("#B9AEFA"));
    expect(renderPetSvg(golden("devo"), "sleeping")).not.toContain(light("#7C8597"));
    expect(renderPetSvg(golden("devo"), "sad")).toContain('points="0,-66 6,-77 16,-76"');
  });

  it("keeps a glow palette's body on the base and gives the mood colour to the mood role", () => {
    const def = parsePet(golden("devo"));
    expect(def).toMatchObject({ glow: true, shift: false, base: 0x2a3550 });
    expect(decodePet(golden("devo")).palette.mode).toBe("glow");
    for (const mood of ["happy", "angry", "sleeping"] as const) {
      const svg = renderPetSvg(def, mood);
      const color = (source("devo").palette.moods[mood] as string).toUpperCase();
      // The dome keeps the base inside its cream rim; the ball takes the mood colour, the traces stay cream.
      const dome = mood === "sleeping" ? 'y="-62" width="108" height="114"' : 'y="-70" width="108" height="122"';
      const inner = mood === "sleeping" ? 'y="-58" width="100" height="106"' : 'y="-66" width="100" height="114"';
      expect(svg).toContain(`<rect x="-54" ${dome} rx="54" fill="#F4F1EA"/>`);
      expect(svg).toContain(`<rect x="-50" ${inner} rx="50" fill="#2A3550"/>`);
      expect(svg).toContain(`width="14" height="14" rx="7" fill="${color}"/>`);
      expect(svg).toContain('points="19,34 6,34 6,54" fill="none" stroke="#F4F1EA"');
      expect(svg).not.toContain(`rx="50" fill="${color}"`);
    }
    // The same parts in a table palette colour the body instead.
    const table = parsePet(encodePet({ ...source("devo"), palette: { ...source("devo").palette, mode: "table" } }));
    expect(table.glow).toBe(false);
    expect(renderPetSvg(table, "happy")).toContain('rx="50" fill="#FFD98C"');
    for (const id of ["mochi", "ember", "sprout"]) expect(parsePet(golden(id)).glow).toBe(false);
  });

  it("draws ring eyes as an ink ring around the sclera with a small pupil and no glint", () => {
    const devo = parsePet(golden("devo"));
    expect(devo.face.eyeStyle).toBe(0);
    const def = { ...devo, face: { ...devo.face, eyeStyle: 3 } };
    const ring = renderPetSvg(def, "idle");
    // Idle eyes are 25 wide and 30 high at (21, -22): the ring is 25/7 wide, drawn inside the eye.
    expect(ring).toContain('<rect x="8.5" y="-37" width="25" height="30" rx="12.5" fill="#FFFFFF"/>');
    expect(ring).toContain('<rect x="10.29" y="-35.21" width="21.43" height="26.43" rx="10.71" fill="none" stroke="#1A1D26" stroke-width="3.57"/>');
    expect(ring).toContain('<circle cx="21" cy="-22" r="3.45" fill="#1A1D26"');
    const round = renderPetSvg({ ...def, face: { ...def.face, eyeStyle: 0 } }, "idle");
    expect(round).toContain('<circle cx="21" cy="-22" r="5.75" fill="#1A1D26"');
    expect(round).not.toContain('stroke-width="3.57"');
    expect(round.match(/fill="#FFFFFF"/g)!.length).toBe(ring.match(/fill="#FFFFFF"/g)!.length + 2);
  });
});

describe("reactions, outline and the preview sheet", () => {
  const hasPython = (() => {
    try {
      execFileSync("python3", ["--version"], { stdio: "ignore" });
      return true;
    } catch {
      return false;
    }
  })();

  it.skipIf(!hasPython)("rests in the same pose as petpack.py's lint model, for every pet", () => {
    const camel = (k: string) => k.replace(/_([a-z])/g, (_, c: string) => c.toUpperCase());
    for (const id of PET_IDS) {
      const out = execFileSync("python3", ["-I", "firmware/tools/petpack.py", "poses", `pets/${id}.pet.json`], { cwd: fileURLToPath(repo) });
      const poses = JSON.parse(out.toString("utf8")) as Record<string, Record<string, number>>;
      for (const mood of PET_MOODS) {
        const py = Object.fromEntries(Object.entries(poses[mood]).map(([k, v]) => [camel(k), v]));
        expect(petPose(golden(id), mood), `${id} ${mood}`).toEqual(py);
      }
    }
  });

  it("draws a reaction on top of idle with its effect and the pack's overrides", () => {
    expect(renderPetSvg(golden("devo"), "pat")).toContain('fill="#FF8FA3"');
    expect(renderPetSvg(golden("devo"), "attention")).toContain('<circle cx="80" cy="-62" r="14" fill="#FFC53D"/>');
    expect(petPose(golden("devo"), "attention")).toMatchObject({ eyeSize: 30, pupilDy: -30, mouthOpen: 22, browL: 0 });
    const pet = parsePet(golden("ember"));
    pet.overrides.push({ mood: 16, channel: 6, value: -40 });
    expect(petPose(pet, "pat").mouthCurve).toBe(-40);
    expect(petPose(pet, "poke").mouthCurve).toBe(-20);
  });

  it("draws the night outline in the night colour without hidden parts or face colour", () => {
    const night = renderPetSvg(golden("devo"), "idle", { outline: true });
    expect(night).toContain('stroke="#9A4A26"');
    // Devo's silhouette is made of night-role lines: the outline draws them, the filled pet never does.
    const edge = 'points="0,-68 26,-61 45,-42 52,-16"';
    expect(night).toContain(`${edge} fill="none" stroke="#9A4A26" stroke-width="2"`);
    expect(renderPetSvg(golden("devo"), "idle")).not.toContain(edge);
    expect(parsePet(golden("devo")).parts.filter((p) => p.role === 14)).toHaveLength(4);
    expect(night).not.toContain("#F4F1EA");
    expect(night).not.toContain("#2A3550");
    expect(night).not.toContain("#B9AEFA");
    expect(night).not.toContain("#1A1D26");
    const ember = renderPetSvg(golden("ember"), "idle", { outline: true });
    expect(ember).not.toContain("#F6E7C1");
  });

  it("puts 16 frames in colour, the outline and 16 in greyscale on one sheet", () => {
    const sheet = renderPetSheetSvg(golden("ember"));
    expect(sheet.match(/<svg /g)).toHaveLength(1 + 16 + 1 + 16);
    expect(sheet).toContain(">Ember</text>");
    expect(sheet).toContain("crc32 8f684d5d");
    const gray = sheet.slice(sheet.indexOf("Greyscale")).replace(/<text[^>]*>[^<]*<\/text>/g, "");
    for (const [, hex] of gray.matchAll(/#([0-9A-F]{6})/g)) expect(hex.slice(0, 2).repeat(3)).toBe(hex);
  });
});
