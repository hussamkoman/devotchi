// Pet packs, format 1 (docs/architecture.md 7.2; the details the firmware adds
// to that table are in pets/README.md). One codec for the server, the CLI, the
// sim and the dashboard: JSON source <-> binary pack. It produces the same
// bytes as firmware/tools/petpack.py and refuses every pack that the
// firmware's pet_pack_validate() refuses, so nothing the server stores or
// serves can fail on a device for a format reason. No Node APIs: the
// dashboard bundles it.

export const PET_FORMAT = 1;
export const PET_MAX_BYTES = 2048;
/** Header, palette, face, aux rows, one rrect part, CRC. */
export const PET_MIN_BYTES = 175;
/** A JSON source on POST /api/pets (protocol 14.8). */
export const PET_SOURCE_MAX_BYTES = 16 * 1024;
export const PET_MAX_PARTS = 28;
export const PET_MAX_OVERRIDES = 48;
export const PET_MAX_OSCILLATORS = 32;
export const PET_NAME_MAX = 12;
/** The `lid` channel value meaning "no lid". */
export const PET_LID_OFF = 1000;
export const PET_OSC_UNIT_MS = 50;

const HEADER_BYTES = 32;
const PALETTE_AT = 32;
const FACE_AT = 88;
const AUX_AT = 100;
const VAR_AT = 164;
const FLAG_SHIFT = 0x01;
const ROLE_NO_OUTLINE = 0x10;
const ROLE_ANCHOR = 0x20;
const ROLE_RING = 0x40;
const ROLE_SQUASH = 0x80;
const WHOLE = 7;
const REACT_BASE = 16;

// The format-1 numbers: a name's index is its number (append-only).
export const PET_MOODS = [
  "happy",
  "sad",
  "excited",
  "anxious",
  "sleeping",
  "angry",
  "proud",
  "idle",
  "focused",
  "working",
  "thinking",
] as const;
export type PetMood = (typeof PET_MOODS)[number];
/** Numbered from 16 in override and oscillator `mood` bytes. */
export const PET_REACTIONS = ["pat", "poke", "dizzy", "wake", "attention"] as const;
export type PetReaction = (typeof PET_REACTIONS)[number];
export const PET_CHANNELS = [
  "eye_open",
  "eye_size",
  "pupil_size",
  "pupil_dx",
  "pupil_dy",
  "brow_angle",
  "mouth_curve",
  "mouth_open",
  "bob_amp_px",
  "bob_period_ms",
  "blink_min_ms",
  "blink_max_ms",
  "jitter_px",
  "accessory",
  "glance",
  "lid",
  "brow_l",
  "brow_r",
  "brow_lift",
  "mouth_dx",
  "mouth_w",
] as const;
export type PetChannel = (typeof PET_CHANNELS)[number];
export const PET_CHANNEL_RANGES: readonly (readonly [number, number])[] = [
  [0, 100], [4, 48], [0, 100], [-100, 100], [-100, 100], [-60, 60], [-100, 100], [0, 100], [0, 12],
  [0, 20000], [0, 30000], [0, 30000], [0, 6], [0, 6], [0, 2], [-60, 60], [-60, 60], [-60, 60],
  [-20, 20], [-30, 30], [0, 60],
];
export const PET_ACCESSORIES = ["none", "zzz", "sweat", "tear", "sparkles", "dots", "tongue"] as const;
export type PetAccessory = (typeof PET_ACCESSORIES)[number];
export const PET_GLANCES = ["off", "look", "scan"] as const;
export const PET_ROLES = [
  "body",
  "dark",
  "light",
  "cheek",
  "accent",
  "accent-dark",
  "cream",
  "ink",
  "white",
  "flame",
  "surface",
  "fog",
  "yellow",
] as const;
export type PetRole = (typeof PET_ROLES)[number];
export const PET_SHAPES = ["rrect", "line", "tri"] as const;
export type PetShape = (typeof PET_SHAPES)[number];
export const PET_LAYERS = ["behind", "body", "head", "front"] as const;
export type PetLayer = (typeof PET_LAYERS)[number];
export const PET_DRIVER_OPS = ["move_x", "move_y", "show_nonzero", "show_eq"] as const;
export type PetDriverOp = (typeof PET_DRIVER_OPS)[number];
export const PET_WAVES = ["sine", "bob", "scan", "jitter"] as const;
export type PetWave = (typeof PET_WAVES)[number];
export const PET_EYES = ["round", "slit", "dot"] as const;
export const PET_MOUTHS = ["plain", "snout"] as const;

// --- JSON source (pets/README.md) ---

export type PetColor = string;

export interface PetSourcePalette {
  mode: "table" | "shift";
  base: PetColor;
  accent: PetColor;
  cream: PetColor;
  sclera: PetColor;
  /** Table: a colour per mood. Shift: [target, t] with t in 0..1. */
  moods: Record<PetMood, PetColor | [PetColor, number]>;
}

export interface PetSourceFace {
  eyes: (typeof PET_EYES)[number];
  mouth: (typeof PET_MOUTHS)[number];
  brows: boolean;
  eye_dx: number;
  eye_y: number;
  mouth_y: number;
  mouth_hw: number;
  sleep_drop?: number;
  replaces?: PetAccessory[];
  eye_sq?: number;
  mouth_sq?: number;
  sweat_y?: number;
}

export interface PetSourceDriver {
  op: PetDriverOp;
  aux: number;
  point?: number | "whole";
  scale?: number;
  k?: number;
}

export interface PetSourcePart {
  name?: string;
  shape: PetShape;
  layer: PetLayer;
  role: PetRole;
  mirror?: boolean;
  outline?: boolean;
  ring?: boolean;
  squash?: boolean;
  anchor?: string;
  x?: number;
  y?: number;
  w?: number;
  h?: number;
  r?: number;
  points?: [number, number][];
  width?: number;
  drivers?: PetSourceDriver[];
}

export interface PetSourceOscillator {
  mood: PetMood | PetReaction;
  aux: number;
  wave: PetWave;
  amp: number;
  period_ms?: number;
}

export interface PetSource {
  id: string;
  name: string;
  rev?: number;
  /** The device picker's line under the name of a built-in pet; not part of the pack. */
  tagline?: string;
  palette: PetSourcePalette;
  face: PetSourceFace;
  aux_names?: string[];
  aux?: Partial<Record<PetMood | PetReaction, [number, number, number, number]>>;
  overrides?: Partial<Record<PetMood | PetReaction, Partial<Record<PetChannel, number | string>>>>;
  oscillators?: PetSourceOscillator[];
  parts: PetSourcePart[];
}

// --- Decoded pack (numbers, as the firmware's pet_def_t) ---

export interface PetFace {
  eyeStyle: number;
  mouthSet: number;
  brows: boolean;
  eyeDx: number;
  eyeY: number;
  mouthY: number;
  mouthHw: number;
  sleepDrop: number;
  accMask: number;
  eyeSq: number;
  mouthSq: number;
  sweatY: number;
}

export interface PetDriver {
  aux: number;
  /** Index into PET_DRIVER_OPS. */
  op: number;
  /** Point index (7 = the whole part) for a move, k for show_eq. */
  arg: number;
  scale: number;
}

export interface PetPart {
  shape: number;
  layer: number;
  role: number;
  mirror: boolean;
  noOutline: boolean;
  ring: boolean;
  squash: boolean;
  /** Index of an earlier line part, or -1. */
  anchor: number;
  /** rrect: one point (the centre) and w, h, r; line and tri: the points. */
  points: [number, number][];
  w: number;
  h: number;
  r: number;
  width: number;
  drivers: PetDriver[];
}

export interface PetOverride {
  mood: number;
  channel: number;
  value: number;
}

export interface PetOscillator {
  mood: number;
  aux: number;
  wave: number;
  amp: number;
  periodMs: number;
}

export interface PetDef {
  id: string;
  name: string;
  rev: number;
  size: number;
  /** The trailer: CRC-32 of every byte before it. */
  crc32: number;
  shift: boolean;
  base: number;
  accent: number;
  cream: number;
  sclera: number;
  /** Per mood (format order): the colour (table) or the shift target. */
  moodRgb: number[];
  /** Shift mode: t/255 per mood; 0 in table mode. */
  moodT: number[];
  face: PetFace;
  /** 16 rows of 4: the 11 moods, then the 5 reactions. */
  aux: number[][];
  overrides: PetOverride[];
  oscillators: PetOscillator[];
  parts: PetPart[];
}

/** The identity of a valid pack (the entry of GET /api/pets without `source`). */
export interface PetInfo {
  id: string;
  name: string;
  rev: number;
  size: number;
  /** 8 lowercase hex digits. */
  crc32: string;
}

/** The firmware's names for a refused pack (protocol 14.8 `error`). */
export type PetWireError = "crc" | "format";

export class PetPackError extends Error {
  constructor(
    readonly wire: PetWireError,
    message: string,
  ) {
    super(message);
    this.name = "PetPackError";
  }
}

/** A JSON source that cannot be encoded; the message names the field. */
export class PetSourceError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "PetSourceError";
  }
}

// --- CRC-32 (IEEE 802.3, as zlib.crc32) ---

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  return table;
})();

export function crc32(bytes: Uint8Array): number {
  let c = 0xffffffff;
  for (const b of bytes) c = CRC_TABLE[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

export const crc32Hex = (value: number) => value.toString(16).padStart(8, "0");

export const PET_CRC_RE = /^[0-9a-f]{8}$/;
const PET_ID_CHARS = /^[a-z0-9]{1,8}$/;

/** RGB mix as the firmware's pet_pack_mix: t in 0..255. */
export function petMix(a: number, b: number, t: number): number {
  const k = Math.min(255, Math.max(0, t));
  let out = 0;
  for (const shift of [0, 8, 16]) {
    const ca = (a >> shift) & 0xff;
    const cb = (b >> shift) & 0xff;
    out |= Math.floor((ca * (255 - k) + cb * k + 127) / 255) << shift;
  }
  return out >>> 0;
}

export function petMoodColor(def: PetDef, mood: number): number {
  const m = mood >= 0 && mood < PET_MOODS.length ? mood : PET_MOODS.indexOf("idle");
  return def.shift ? petMix(def.base, def.moodRgb[m], def.moodT[m]) : def.moodRgb[m];
}

// --- Decoding: the checks of pet_pack_validate, in its order ---

const fail = (wire: PetWireError, message: string): never => {
  throw new PetPackError(wire, message);
};

const s8 = (v: number) => (v > 127 ? v - 256 : v);
const u16 = (b: Uint8Array, at: number) => b[at] | (b[at + 1] << 8);
const u32 = (b: Uint8Array, at: number) => (b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24)) >>> 0;
const rgbAt = (b: Uint8Array, at: number) => (b[at] << 16) | (b[at + 1] << 8) | b[at + 2];
const isPackMood = (m: number) => m < PET_MOODS.length || (m >= REACT_BASE && m < REACT_BASE + PET_REACTIONS.length);

function textField(raw: Uint8Array, isId: boolean, what: string): string {
  let n = raw.indexOf(0);
  if (n < 0) n = raw.length;
  if (n === 0) fail("format", `${what} is empty`);
  for (let i = n; i < raw.length; i++) if (raw[i] !== 0) fail("format", `${what} has bytes after its NUL padding`);
  let out = "";
  for (let i = 0; i < n; i++) {
    const c = raw[i];
    const ok = isId ? (c >= 97 && c <= 122) || (c >= 48 && c <= 57) : c >= 0x20 && c <= 0x7e;
    if (!ok) fail("format", `${what} has a character outside ${isId ? "[a-z0-9]" : "printable ASCII"}`);
    out += String.fromCharCode(c);
  }
  return out;
}

/** Decodes a pack after every check; throws PetPackError. */
export function parsePet(bytes: Uint8Array): PetDef {
  const len = bytes.length;
  if (len < PET_MIN_BYTES || len > PET_MAX_BYTES) fail("format", `pack is ${len} bytes; a pack has ${PET_MIN_BYTES}..${PET_MAX_BYTES}`);
  const stored = u32(bytes, len - 4);
  const computed = crc32(bytes.subarray(0, len - 4));
  if (stored !== computed) fail("crc", `CRC-32 is ${crc32Hex(computed)} but the pack says ${crc32Hex(stored)}`);
  if (bytes[0] !== 0x44 || bytes[1] !== 0x50 || bytes[2] !== 0x45 || bytes[3] !== 0x54) fail("format", 'pack does not start with "DPET"');
  if (bytes[4] !== PET_FORMAT) fail("format", `pack format ${bytes[4]} is not ${PET_FORMAT}`);
  const rev = bytes[5];
  if (rev === 0) fail("format", "rev must be 1..255");
  const size = u16(bytes, 6);
  if (size !== len) fail("format", `header size ${size} differs from the pack length ${len}`);
  const id = textField(bytes.subarray(8, 16), true, "id");
  const name = textField(bytes.subarray(16, 28), false, "name");
  const flags = bytes[28];
  const nParts = bytes[29];
  const nOver = bytes[30];
  const nOsc = bytes[31];
  if (flags & ~FLAG_SHIFT) fail("format", "reserved flag bits are set");
  if (nParts < 1 || nParts > PET_MAX_PARTS) fail("format", `${nParts} parts; a pet has 1..${PET_MAX_PARTS}`);
  if (nOver > PET_MAX_OVERRIDES) fail("format", `${nOver} overrides; at most ${PET_MAX_OVERRIDES}`);
  if (nOsc > PET_MAX_OSCILLATORS) fail("format", `${nOsc} oscillators; at most ${PET_MAX_OSCILLATORS}`);
  const shift = (flags & FLAG_SHIFT) !== 0;

  const moodRgb: number[] = [];
  const moodT: number[] = [];
  for (let i = 0; i < PET_MOODS.length; i++) {
    const at = PALETTE_AT + 12 + 4 * i;
    moodRgb.push(rgbAt(bytes, at));
    if (!shift && bytes[at + 3] !== 0) fail("format", `palette entry ${PET_MOODS[i]} has a non-zero reserved byte`);
    moodT.push(shift ? bytes[at + 3] : 0);
  }

  const f = bytes.subarray(FACE_AT, FACE_AT + 12);
  if (f[0] > 2 || f[1] > 1 || f[2] > 1 || f[3] < 4 || f[3] > 60 || f[6] < 4 || f[6] > 60 || f[7] > 30 || f[9] > 16 || f[10] > 16) {
    fail("format", "a face value is out of range");
  }
  if (f[8] & ~0x7e) fail("format", "reserved acc_mask bits are set");
  const face: PetFace = {
    eyeStyle: f[0],
    mouthSet: f[1],
    brows: f[2] === 1,
    eyeDx: f[3],
    eyeY: s8(f[4]),
    mouthY: s8(f[5]),
    mouthHw: f[6],
    sleepDrop: f[7],
    accMask: f[8],
    eyeSq: f[9],
    mouthSq: f[10],
    sweatY: s8(f[11]),
  };
  const aux: number[][] = [];
  for (let r = 0; r < PET_MOODS.length + PET_REACTIONS.length; r++) {
    aux.push([0, 1, 2, 3].map((k) => s8(bytes[AUX_AT + 4 * r + k])));
  }

  let p = VAR_AT;
  const end = len - 4;
  const take = (n: number): Uint8Array => {
    if (end - p < n) fail("format", "pack ends inside a record");
    const out = bytes.subarray(p, p + n);
    p += n;
    return out;
  };

  const overrides: PetOverride[] = [];
  for (let i = 0; i < nOver; i++) {
    const b = take(4);
    const value = (b[2] | (b[3] << 8)) << 16 >> 16;
    if (!isPackMood(b[0]) || b[1] >= PET_CHANNELS.length) fail("format", `override ${i} names an unknown mood or channel`);
    const [lo, hi] = PET_CHANNEL_RANGES[b[1]];
    const lidOff = PET_CHANNELS[b[1]] === "lid" && value === PET_LID_OFF;
    if (!lidOff && (value < lo || value > hi)) fail("format", `override ${i}: ${PET_CHANNELS[b[1]]} ${value} is not in ${lo}..${hi}`);
    overrides.push({ mood: b[0], channel: b[1], value });
  }
  const oscillators: PetOscillator[] = [];
  for (let i = 0; i < nOsc; i++) {
    const b = take(4);
    if (!isPackMood(b[0])) fail("format", `oscillator ${i} names an unknown mood`);
    if (b[1] & 0xf0) fail("format", `oscillator ${i} has reserved bits set`);
    const wave = (b[1] >> 2) & 3;
    if ((wave === 0 || wave === 3) && b[3] === 0) fail("format", `oscillator ${i} needs a period`);
    oscillators.push({ mood: b[0], aux: b[1] & 3, wave, amp: s8(b[2]), periodMs: b[3] * PET_OSC_UNIT_MS });
  }
  const parts: PetPart[] = [];
  for (let i = 0; i < nParts; i++) {
    const [kind, role] = take(2);
    const shape = kind & 3;
    const nDrv = (kind >> 4) & 7;
    if (shape > 2 || nDrv > 4) fail("format", `part ${i} has an unknown shape or more than 4 drivers`);
    if ((role & 0x0f) >= PET_ROLES.length) fail("format", `part ${i} has an unknown colour role`);
    if (role & (ROLE_RING | ROLE_SQUASH) && shape !== 0) fail("format", `part ${i}: ring and squash are for rrect parts only`);
    const part: PetPart = {
      shape,
      layer: (kind >> 2) & 3,
      role: role & 0x0f,
      mirror: (kind & 0x80) !== 0,
      noOutline: (role & ROLE_NO_OUTLINE) !== 0,
      ring: (role & ROLE_RING) !== 0,
      squash: (role & ROLE_SQUASH) !== 0,
      anchor: -1,
      points: [],
      w: 0,
      h: 0,
      r: 0,
      width: 0,
      drivers: [],
    };
    if (role & ROLE_ANCHOR) {
      const a = take(1)[0];
      if (a >= i || parts[a].shape !== 1) fail("format", `part ${i} is anchored to ${a}, which is not an earlier line`);
      part.anchor = a;
    }
    if (shape === 0) {
      const [x, y, w, h, r] = take(5);
      if (w === 0 || h === 0 || r > Math.floor(Math.min(w, h) / 2)) fail("format", `part ${i}: rrect ${w}x${h} r ${r} is not drawable`);
      part.points = [[s8(x), s8(y)]];
      Object.assign(part, { w, h, r });
    } else {
      let nPts = 3;
      if (shape === 1) {
        const lb = take(1)[0];
        nPts = lb & 7;
        part.width = lb >> 3;
        if (nPts < 2 || nPts > 4 || part.width === 0) fail("format", `part ${i}: a line has 2..4 points and a width`);
      }
      const raw = take(2 * nPts);
      for (let k = 0; k < nPts; k++) part.points.push([s8(raw[2 * k]), s8(raw[2 * k + 1])]);
    }
    for (let d = 0; d < nDrv; d++) {
      const [ob, sb] = take(2);
      const drv: PetDriver = { aux: ob & 3, op: (ob >> 2) & 3, arg: (ob >> 4) & 7, scale: s8(sb) };
      if (drv.op <= 1) {
        if (drv.arg !== WHOLE && (shape === 0 || drv.arg >= part.points.length)) fail("format", `part ${i} driver ${d} moves a point it does not have`);
      } else if (drv.scale !== 0 || (drv.op === 2 && drv.arg !== 0)) {
        fail("format", `part ${i} driver ${d} has reserved bits set`);
      }
      part.drivers.push(drv);
    }
    parts.push(part);
  }
  if (p !== end) fail("format", `${end - p} bytes follow the last part`);
  return {
    id,
    name,
    rev,
    size,
    crc32: stored,
    shift,
    base: rgbAt(bytes, PALETTE_AT),
    accent: rgbAt(bytes, PALETTE_AT + 3),
    cream: rgbAt(bytes, PALETTE_AT + 6),
    sclera: rgbAt(bytes, PALETTE_AT + 9),
    moodRgb,
    moodT,
    face,
    aux,
    overrides,
    oscillators,
    parts,
  };
}

export type PetValidation = { ok: true; pet: PetInfo } | { ok: false; error: PetWireError; message: string };

/** The firmware's check of a pack, as a result. */
export function validatePet(bytes: Uint8Array): PetValidation {
  try {
    const def = parsePet(bytes);
    return { ok: true, pet: petInfo(def) };
  } catch (err) {
    if (err instanceof PetPackError) return { ok: false, error: err.wire, message: err.message };
    throw err;
  }
}

export function petInfo(def: PetDef): PetInfo {
  return { id: def.id, name: def.name, rev: def.rev, size: def.size, crc32: crc32Hex(def.crc32) };
}

const hex6 = (rgb: number) => "#" + rgb.toString(16).toUpperCase().padStart(6, "0");
const moodName = (m: number): PetMood | PetReaction => (m < REACT_BASE ? PET_MOODS[m] : PET_REACTIONS[m - REACT_BASE]);

/** The pack as its JSON source (the inverse of encodePet); throws PetPackError. */
export function decodePet(bytes: Uint8Array): PetSource {
  const def = parsePet(bytes);
  const moods = {} as PetSourcePalette["moods"];
  PET_MOODS.forEach((m, i) => {
    moods[m] = def.shift ? [hex6(def.moodRgb[i]), Math.round((def.moodT[i] / 255) * 1000) / 1000] : hex6(def.moodRgb[i]);
  });
  const f = def.face;
  const aux: NonNullable<PetSource["aux"]> = {};
  [...PET_MOODS, ...PET_REACTIONS].forEach((m, i) => {
    aux[m] = def.aux[i] as [number, number, number, number];
  });
  const overrides: NonNullable<PetSource["overrides"]> = {};
  for (const o of def.overrides) {
    const channel = PET_CHANNELS[o.channel];
    const row = (overrides[moodName(o.mood)] ??= {});
    row[channel] = channel === "lid" && o.value === PET_LID_OFF ? "off" : o.value;
  }
  const anchored = new Set(def.parts.map((p) => p.anchor).filter((a) => a >= 0));
  const parts = def.parts.map((p, i): PetSourcePart => {
    const out: PetSourcePart = { shape: PET_SHAPES[p.shape], layer: PET_LAYERS[p.layer], role: PET_ROLES[p.role] };
    if (anchored.has(i)) out.name = `p${i}`;
    if (p.mirror) out.mirror = true;
    if (p.noOutline) out.outline = false;
    if (p.ring) out.ring = true;
    if (p.squash) out.squash = true;
    if (p.anchor >= 0) out.anchor = `p${p.anchor}`;
    if (p.shape === 0) Object.assign(out, { x: p.points[0][0], y: p.points[0][1], w: p.w, h: p.h, r: p.r });
    else {
      if (p.shape === 1) out.width = p.width;
      out.points = p.points.map(([x, y]) => [x, y]);
    }
    if (p.drivers.length > 0) {
      out.drivers = p.drivers.map((d): PetSourceDriver => {
        const op = PET_DRIVER_OPS[d.op];
        if (d.op <= 1) return { op, aux: d.aux, point: d.arg === WHOLE ? "whole" : d.arg, scale: d.scale };
        return op === "show_eq" ? { op, aux: d.aux, k: d.arg } : { op, aux: d.aux };
      });
    }
    return out;
  });
  return {
    id: def.id,
    name: def.name,
    rev: def.rev,
    palette: {
      mode: def.shift ? "shift" : "table",
      base: hex6(def.base),
      accent: hex6(def.accent),
      cream: hex6(def.cream),
      sclera: hex6(def.sclera),
      moods,
    },
    face: {
      eyes: PET_EYES[f.eyeStyle],
      mouth: PET_MOUTHS[f.mouthSet],
      brows: f.brows,
      eye_dx: f.eyeDx,
      eye_y: f.eyeY,
      mouth_y: f.mouthY,
      mouth_hw: f.mouthHw,
      sleep_drop: f.sleepDrop,
      replaces: PET_ACCESSORIES.filter((_, i) => (f.accMask & (1 << i)) !== 0),
      eye_sq: f.eyeSq,
      mouth_sq: f.mouthSq,
      sweat_y: f.sweatY,
    },
    aux,
    overrides,
    oscillators: def.oscillators.map((o) => ({
      mood: moodName(o.mood),
      aux: o.aux,
      wave: PET_WAVES[o.wave],
      amp: o.amp,
      period_ms: o.periodMs,
    })),
    parts,
  };
}

// --- Encoding: JSON source -> pack ---

type Fields = Record<string, unknown>;

const srcFail = (path: string, message: string): never => {
  throw new PetSourceError(`${path}: ${message}`);
};

const describe = (v: unknown) => (typeof v === "string" ? JSON.stringify(v.slice(0, 40)) : JSON.stringify(v) ?? String(v));

function obj(v: unknown, path: string): Fields {
  if (typeof v !== "object" || v === null || Array.isArray(v)) srcFail(path, "must be an object");
  return v as Fields;
}

function onlyKeys(o: Fields, keys: readonly string[], path: string): void {
  for (const k of Object.keys(o)) if (!keys.includes(k)) srcFail(`${path}.${k}`, `unknown field (known: ${keys.join(", ")})`);
}

function int(v: unknown, lo: number, hi: number, path: string): number {
  if (typeof v !== "number" || !Number.isInteger(v) || v < lo || v > hi) srcFail(path, `${describe(v)} is not an integer in ${lo}..${hi}`);
  return v as number;
}

function optInt(v: unknown, lo: number, hi: number, dflt: number, path: string): number {
  return v === undefined ? dflt : int(v, lo, hi, path);
}

function bool(v: unknown, path: string, dflt = false): boolean {
  if (v === undefined) return dflt;
  if (typeof v !== "boolean") srcFail(path, `${describe(v)} is not true or false`);
  return v as boolean;
}

function pick<T extends string>(v: unknown, names: readonly T[], path: string): number {
  const i = typeof v === "string" ? (names as readonly string[]).indexOf(v) : -1;
  if (i < 0) srcFail(path, `${describe(v)} is not one of ${names.join(", ")}`);
  return i;
}

function color(v: unknown, path: string): number {
  if (typeof v !== "string" || !/^#[0-9a-fA-F]{6}$/.test(v)) srcFail(path, `${describe(v)} is not a #RRGGBB colour`);
  return parseInt((v as string).slice(1), 16);
}

function moodNumber(v: unknown, path: string): number {
  if (typeof v === "string") {
    const m = (PET_MOODS as readonly string[]).indexOf(v);
    if (m >= 0) return m;
    const r = (PET_REACTIONS as readonly string[]).indexOf(v);
    if (r >= 0) return REACT_BASE + r;
  }
  return srcFail(path, `${describe(v)} is not a mood (${PET_MOODS.join(" ")}) or reaction (${PET_REACTIONS.join(" ")})`);
}

/** Python's round(): half to even, so the shift bytes match petpack.py. */
function roundHalfEven(x: number): number {
  const f = Math.floor(x);
  const d = x - f;
  if (d > 0.5) return f + 1;
  if (d < 0.5) return f;
  return f % 2 === 0 ? f : f + 1;
}

class Writer {
  private buf = new Uint8Array(PET_MAX_BYTES + 256);
  length = 0;
  u8(...values: number[]): void {
    for (const v of values) {
      if (this.length >= this.buf.length) {
        const bigger = new Uint8Array(this.buf.length * 2);
        bigger.set(this.buf);
        this.buf = bigger;
      }
      this.buf[this.length++] = v & 0xff;
    }
  }
  bytes(): Uint8Array {
    return this.buf.slice(0, this.length);
  }
}

function overrideValue(channel: number, v: unknown, path: string): number {
  const name = PET_CHANNELS[channel];
  if (name === "lid" && v === "off") return PET_LID_OFF;
  if (name === "accessory" && typeof v === "string") return pick(v, PET_ACCESSORIES, path);
  if (name === "glance" && typeof v === "string") return pick(v, PET_GLANCES, path);
  const [lo, hi] = PET_CHANNEL_RANGES[channel];
  return int(v, lo, hi, path);
}

function encodePart(raw: unknown, i: number, names: Map<string, { index: number; shape: number }>, w: Writer): void {
  const path = `parts[${i}]`;
  const p = obj(raw, path);
  const shape = pick(p.shape, PET_SHAPES, `${path}.shape`);
  const geometry = shape === 0 ? ["x", "y", "w", "h", "r"] : shape === 1 ? ["points", "width"] : ["points"];
  onlyKeys(p, ["name", "shape", "layer", "role", "mirror", "outline", "ring", "squash", "anchor", "drivers", ...geometry], path);
  const layer = pick(p.layer, PET_LAYERS, `${path}.layer`);
  let role = pick(p.role, PET_ROLES, `${path}.role`);
  const mirror = bool(p.mirror, `${path}.mirror`);
  if (!bool(p.outline, `${path}.outline`, true)) role |= ROLE_NO_OUTLINE;
  if (bool(p.ring, `${path}.ring`)) role |= ROLE_RING;
  if (bool(p.squash, `${path}.squash`)) role |= ROLE_SQUASH;
  if (role & (ROLE_RING | ROLE_SQUASH) && shape !== 0) srcFail(path, "ring and squash are for rrect parts only");
  let anchor = -1;
  if (p.anchor !== undefined) {
    const target = typeof p.anchor === "string" ? names.get(p.anchor) : undefined;
    if (!target || target.shape !== 1) srcFail(`${path}.anchor`, `${describe(p.anchor)} is not the name of an earlier line part`);
    anchor = target!.index;
    role |= ROLE_ANCHOR;
  }
  const drivers = p.drivers === undefined ? [] : p.drivers;
  if (!Array.isArray(drivers) || drivers.length > 4) srcFail(`${path}.drivers`, "must be a list of at most 4 drivers");
  const kind = shape | (layer << 2) | ((drivers as unknown[]).length << 4) | (mirror ? 0x80 : 0);
  w.u8(kind, role);
  if (anchor >= 0) w.u8(anchor);
  let nPts = 1;
  if (shape === 0) {
    const pw = int(p.w, 1, 255, `${path}.w`);
    const ph = int(p.h, 1, 255, `${path}.h`);
    const r = int(p.r, 0, Math.floor(Math.min(pw, ph) / 2), `${path}.r`);
    w.u8(int(p.x, -128, 127, `${path}.x`), int(p.y, -128, 127, `${path}.y`), pw, ph, r);
  } else {
    const want = shape === 1 ? "2..4" : "3";
    const pts = p.points;
    if (!Array.isArray(pts) || (shape === 1 ? pts.length < 2 || pts.length > 4 : pts.length !== 3)) {
      srcFail(`${path}.points`, `must be a list of ${want} [x, y] points`);
    }
    nPts = (pts as unknown[]).length;
    if (shape === 1) w.u8(nPts | (int(p.width, 1, 31, `${path}.width`) << 3));
    (pts as unknown[]).forEach((pt, k) => {
      if (!Array.isArray(pt) || pt.length !== 2) srcFail(`${path}.points[${k}]`, "must be [x, y]");
      const xy = pt as unknown[];
      w.u8(int(xy[0], -128, 127, `${path}.points[${k}][0]`), int(xy[1], -128, 127, `${path}.points[${k}][1]`));
    });
  }
  (drivers as unknown[]).forEach((rawDrv, d) => {
    const dp = `${path}.drivers[${d}]`;
    const drv = obj(rawDrv, dp);
    const op = pick(drv.op, PET_DRIVER_OPS, `${dp}.op`);
    onlyKeys(drv, op <= 1 ? ["op", "aux", "point", "scale"] : op === 3 ? ["op", "aux", "k"] : ["op", "aux"], dp);
    const aux = int(drv.aux, 0, 3, `${dp}.aux`);
    let arg = 0;
    let scale = 0;
    if (op <= 1) {
      if (drv.point === undefined || drv.point === "whole") arg = WHOLE;
      else if (shape === 0) srcFail(`${dp}.point`, 'a rrect moves as a whole ("whole")');
      else arg = int(drv.point, 0, nPts - 1, `${dp}.point`);
      scale = optInt(drv.scale, -128, 127, 16, `${dp}.scale`);
    } else if (op === 3) {
      arg = int(drv.k, 0, 7, `${dp}.k`);
    }
    w.u8(aux | (op << 2) | (arg << 4), scale);
  });
  if (p.name !== undefined) {
    if (typeof p.name !== "string" || p.name === "") srcFail(`${path}.name`, "must be a non-empty string");
    if (names.has(p.name as string)) srcFail(`${path}.name`, `${describe(p.name)} is used by an earlier part`);
    names.set(p.name as string, { index: i, shape });
  }
}

/** Checks a JSON source and returns it typed; throws PetSourceError naming the field. */
export function validatePetSource(json: unknown): PetSource {
  encodePet(json);
  return json as PetSource;
}

/** JSON source -> pack, byte for byte as petpack.py. Throws PetSourceError. */
export function encodePet(json: unknown): Uint8Array {
  const src = obj(json, "pet");
  onlyKeys(src, ["id", "name", "rev", "tagline", "palette", "face", "aux_names", "aux", "overrides", "oscillators", "parts"], "pet");
  if (typeof src.id !== "string" || !PET_ID_CHARS.test(src.id)) srcFail("id", `${describe(src.id)} is not [a-z0-9]{1,8}`);
  const id = src.id as string;
  if (typeof src.name !== "string" || !/^[\x20-\x7e]{1,12}$/.test(src.name)) {
    srcFail("name", `${describe(src.name)} is not 1..12 printable ASCII characters`);
  }
  const name = src.name as string;
  const rev = optInt(src.rev, 1, 255, 1, "rev");

  const pal = obj(src.palette, "palette");
  onlyKeys(pal, ["mode", "base", "accent", "cream", "sclera", "moods"], "palette");
  const shift = pick(pal.mode, ["table", "shift"], "palette.mode") === 1;
  const palette = new Writer();
  for (const key of ["base", "accent", "cream", "sclera"]) {
    const c = color(pal[key], `palette.${key}`);
    palette.u8(c >> 16, c >> 8, c);
  }
  const moods = obj(pal.moods, "palette.moods");
  onlyKeys(moods, PET_MOODS, "palette.moods");
  for (const m of PET_MOODS) {
    const path = `palette.moods.${m}`;
    const e = moods[m];
    if (e === undefined) srcFail(path, "is missing (every mood needs an entry)");
    if (shift) {
      if (!Array.isArray(e) || e.length !== 2) srcFail(path, 'must be ["#RRGGBB", t] in shift mode');
      const [target, t] = e as unknown[];
      const c = color(target, `${path}[0]`);
      if (typeof t !== "number" || !(t >= 0 && t <= 1)) srcFail(`${path}[1]`, `${describe(t)} is not a number in 0..1`);
      palette.u8(c >> 16, c >> 8, c, roundHalfEven((t as number) * 255));
    } else {
      const c = color(e, path);
      palette.u8(c >> 16, c >> 8, c, 0);
    }
  }

  const f = obj(src.face, "face");
  onlyKeys(f, ["eyes", "mouth", "brows", "eye_dx", "eye_y", "mouth_y", "mouth_hw", "sleep_drop", "replaces", "eye_sq", "mouth_sq", "sweat_y"], "face");
  if (typeof f.brows !== "boolean") srcFail("face.brows", `${describe(f.brows)} is not true or false`);
  let accMask = 0;
  const replaces = f.replaces === undefined ? [] : f.replaces;
  if (!Array.isArray(replaces)) srcFail("face.replaces", "must be a list of accessory names");
  (replaces as unknown[]).forEach((a, i) => {
    const n = pick(a, PET_ACCESSORIES, `face.replaces[${i}]`);
    if (n === 0) srcFail(`face.replaces[${i}]`, '"none" cannot be replaced');
    accMask |= 1 << n;
  });
  const face = [
    pick(f.eyes, PET_EYES, "face.eyes"),
    pick(f.mouth, PET_MOUTHS, "face.mouth"),
    f.brows ? 1 : 0,
    int(f.eye_dx, 4, 60, "face.eye_dx"),
    int(f.eye_y, -128, 127, "face.eye_y"),
    int(f.mouth_y, -128, 127, "face.mouth_y"),
    int(f.mouth_hw, 4, 60, "face.mouth_hw"),
    optInt(f.sleep_drop, 0, 30, 0, "face.sleep_drop"),
    accMask,
    optInt(f.eye_sq, 0, 16, 16, "face.eye_sq"),
    optInt(f.mouth_sq, 0, 16, 16, "face.mouth_sq"),
    optInt(f.sweat_y, -128, 127, -46, "face.sweat_y"),
  ];

  if (src.tagline !== undefined && (typeof src.tagline !== "string" || !/^[\x20-\x7e]{0,32}$/.test(src.tagline))) {
    srcFail("tagline", `${describe(src.tagline)} is not 0..32 printable ASCII characters`);
  }
  if (src.aux_names !== undefined) {
    if (!Array.isArray(src.aux_names) || src.aux_names.length > 4 || src.aux_names.some((n) => typeof n !== "string")) {
      srcFail("aux_names", "must be a list of at most 4 strings");
    }
  }
  const auxRows = src.aux === undefined ? {} : obj(src.aux, "aux");
  onlyKeys(auxRows, [...PET_MOODS, ...PET_REACTIONS], "aux");
  const aux = new Writer();
  for (const m of [...PET_MOODS, ...PET_REACTIONS]) {
    const row = auxRows[m] ?? [0, 0, 0, 0];
    if (!Array.isArray(row) || row.length !== 4) srcFail(`aux.${m}`, "needs 4 values");
    (row as unknown[]).forEach((v, k) => aux.u8(int(v, -128, 127, `aux.${m}[${k}]`)));
  }

  const over = new Writer();
  let nOver = 0;
  const overRows = src.overrides === undefined ? {} : obj(src.overrides, "overrides");
  for (const [m, rawRow] of Object.entries(overRows)) {
    const mood = moodNumber(m, `overrides.${m}`);
    for (const [ch, v] of Object.entries(obj(rawRow, `overrides.${m}`))) {
      const channel = pick(ch, PET_CHANNELS, `overrides.${m}.${ch}`);
      const value = overrideValue(channel, v, `overrides.${m}.${ch}`);
      over.u8(mood, channel, value, value >> 8);
      nOver++;
    }
  }
  if (nOver > PET_MAX_OVERRIDES) srcFail("overrides", `${nOver} overrides; at most ${PET_MAX_OVERRIDES}`);

  const osc = new Writer();
  const oscList = src.oscillators === undefined ? [] : src.oscillators;
  if (!Array.isArray(oscList) || oscList.length > PET_MAX_OSCILLATORS) {
    srcFail("oscillators", `must be a list of at most ${PET_MAX_OSCILLATORS} oscillators`);
  }
  (oscList as unknown[]).forEach((rawOsc, i) => {
    const path = `oscillators[${i}]`;
    const o = obj(rawOsc, path);
    onlyKeys(o, ["mood", "aux", "wave", "amp", "period_ms"], path);
    const wave = pick(o.wave, PET_WAVES, `${path}.wave`);
    const period = optInt(o.period_ms, 0, 255 * PET_OSC_UNIT_MS, 0, `${path}.period_ms`);
    if (period % PET_OSC_UNIT_MS !== 0) srcFail(`${path}.period_ms`, `${period} is not a multiple of ${PET_OSC_UNIT_MS} ms`);
    if ((wave === 0 || wave === 3) && period === 0) srcFail(`${path}.period_ms`, `a ${PET_WAVES[wave]} oscillator needs a period`);
    osc.u8(moodNumber(o.mood, `${path}.mood`), int(o.aux, 0, 3, `${path}.aux`) | (wave << 2), int(o.amp, -128, 127, `${path}.amp`), period / PET_OSC_UNIT_MS);
  });

  if (!Array.isArray(src.parts) || src.parts.length < 1 || src.parts.length > PET_MAX_PARTS) {
    srcFail("parts", `must be a list of 1..${PET_MAX_PARTS} parts`);
  }
  const parts = new Writer();
  const names = new Map<string, { index: number; shape: number }>();
  (src.parts as unknown[]).forEach((p, i) => encodePart(p, i, names, parts));

  const size = HEADER_BYTES + palette.length + face.length + aux.length + over.length + osc.length + parts.length + 4;
  if (size > PET_MAX_BYTES) srcFail("pet", `the pack would be ${size} bytes; at most ${PET_MAX_BYTES}`);
  const out = new Uint8Array(size);
  out.set([0x44, 0x50, 0x45, 0x54, PET_FORMAT, rev, size & 0xff, size >> 8]);
  for (let i = 0; i < id.length; i++) out[8 + i] = id.charCodeAt(i);
  for (let i = 0; i < name.length; i++) out[16 + i] = name.charCodeAt(i);
  out.set([shift ? FLAG_SHIFT : 0, (src.parts as unknown[]).length, nOver, osc.length / 4], 28);
  let at = HEADER_BYTES;
  for (const chunk of [palette.bytes(), Uint8Array.from(face, (v) => v & 0xff), aux.bytes(), over.bytes(), osc.bytes(), parts.bytes()]) {
    out.set(chunk, at);
    at += chunk.length;
  }
  const crc = crc32(out.subarray(0, size - 4));
  out.set([crc, crc >>> 8, crc >>> 16, crc >>> 24], size - 4);
  try {
    parsePet(out);
  } catch (err) {
    // The source checks above mirror the decoder, so this only fires if they disagree.
    if (err instanceof PetPackError) srcFail("pet", `encoded pack is invalid: ${err.message}`);
    throw err;
  }
  return out;
}
