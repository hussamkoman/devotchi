// Static SVG of a pet in one mood: the drawing rules of the firmware rig
// (core/pet_pose.c, ui/pet_view.c; drawn first in docs/design/tools/pet.py)
// at rest: no bob, blink, glance, jitter, oscillators or reactions. Parts are
// drawn back to front, then eyes, brows, mouth and the mood's accessory.
// Everything in the SVG comes from numbers; no pack text reaches the markup.

import { PET_LID_OFF, PET_MOODS, parsePet, petMix, petMoodColor, type PetDef, type PetMood } from "./petpack.js";

const INK = 0x1a1d26;
const WHITE = 0xffffff;
const CORAL = 0xff6b5e;
const FLAME = 0xff8a3d;
const SURFACE = 0x161a22;
const FOG = 0xa7aebb;
const YELLOW = 0xffc53d;
const CREAM = 0xf3eee3;
const TONGUE = 0xff8a80;
const SPARKLE = 0xffe9a8;
const DROP = 0xb3e5fc;
const DARK_T = 71;
const LIGHT_T = 102;
const CHEEK_T = 107;
const ACC_DARK_T = 64;
const EYE_SHUT_OPEN = 15;
const BROW_MIN_ANGLE = 4;

const ACC_ZZZ = 1;
const ACC_SWEAT = 2;
const ACC_TEAR = 3;
const ACC_SPARKLES = 4;
const ACC_DOTS = 5;
const ACC_TONGUE = 6;

/** The shared pose of a mood (pet-animations 1 and 3), channels in format order. */
interface Pose {
  eyeOpen: number;
  eyeSize: number;
  pupilSize: number;
  pupilDx: number;
  pupilDy: number;
  browAngle: number;
  mouthCurve: number;
  mouthOpen: number;
  accessory: number;
  lid: number;
  browL: number;
  browR: number;
  browLift: number;
  mouthDx: number;
  mouthW: number;
}

// eye_open, eye_size, pupil_size, pupil_dx, pupil_dy, brow, curve, open, accessory, lid
const BASE: Record<PetMood, number[]> = {
  happy: [100, 26, 45, 0, 0, -5, 80, 0, 0, PET_LID_OFF],
  sad: [65, 24, 45, 0, 40, -20, -70, 0, ACC_TEAR, PET_LID_OFF],
  excited: [100, 32, 40, 0, 0, -12, 100, 60, ACC_SPARKLES, PET_LID_OFF],
  anxious: [100, 28, 22, 0, 0, -18, -25, 15, ACC_SWEAT, PET_LID_OFF],
  sleeping: [0, 26, 45, 0, 0, 0, 15, 0, ACC_ZZZ, PET_LID_OFF],
  angry: [60, 26, 50, 0, 0, 28, -55, 0, 0, PET_LID_OFF],
  proud: [65, 26, 45, 0, -45, -8, 65, 0, ACC_SPARKLES, PET_LID_OFF],
  idle: [100, 25, 46, 0, 0, 0, 42, 0, 0, PET_LID_OFF],
  focused: [62, 26, 52, 0, 20, 0, 16, 0, 0, 0],
  working: [88, 24, 46, 0, 45, 0, 34, 0, ACC_TONGUE, PET_LID_OFF],
  thinking: [92, 24, 45, 60, -60, 0, -4, 0, ACC_DOTS, PET_LID_OFF],
};
const EXTRA: Partial<Record<PetMood, Partial<Pose>>> = {
  focused: { mouthW: 11 },
  working: { mouthW: 13 },
  thinking: { browL: -4, browR: -16, browLift: 5, mouthDx: 9, mouthW: 8 },
};
/** Format channel number -> pose field; channels a static frame does not use are absent. */
const CHANNEL_FIELD: Partial<Record<number, keyof Pose>> = {
  0: "eyeOpen",
  1: "eyeSize",
  2: "pupilSize",
  3: "pupilDx",
  4: "pupilDy",
  5: "browAngle",
  6: "mouthCurve",
  7: "mouthOpen",
  13: "accessory",
  15: "lid",
  16: "browL",
  17: "browR",
  18: "browLift",
  19: "mouthDx",
  20: "mouthW",
};

function poseFor(def: PetDef, mood: number): Pose {
  const name = PET_MOODS[mood];
  const b = BASE[name];
  const pose: Pose = {
    eyeOpen: b[0],
    eyeSize: b[1],
    pupilSize: b[2],
    pupilDx: b[3],
    pupilDy: b[4],
    browAngle: b[5],
    mouthCurve: b[6],
    mouthOpen: b[7],
    accessory: b[8],
    lid: b[9],
    browL: 0,
    browR: 0,
    browLift: 0,
    mouthDx: 0,
    mouthW: 0,
    ...EXTRA[name],
  };
  for (const o of def.overrides) {
    const field = CHANNEL_FIELD[o.channel];
    if (o.mood === mood && field !== undefined) pose[field] = o.value;
  }
  return pose;
}

export interface PetSvgOptions {
  /** Pixel size of the SVG; default the 220x190 body-unit box. */
  width?: number;
  height?: number;
  /** Greyscale (pet-animations 5 rule 7). */
  gray?: boolean;
  /** Prefix of the clip-path ids; must differ between SVGs inlined in one document. */
  idPrefix?: string;
}

const n = (v: number) => String(Math.round(v * 100) / 100);
const hex = (rgb: number) => "#" + rgb.toString(16).toUpperCase().padStart(6, "0");
const pts = (p: [number, number][]) => p.map(([x, y]) => `${n(x)},${n(y)}`).join(" ");

class Svg {
  readonly out: string[] = [];
  private clips = 0;
  constructor(private readonly prefix: string) {}

  rect(cx: number, cy: number, w: number, h: number, r: number, fill: number, clip?: string): void {
    if (w <= 0 || h <= 0) return;
    const rr = Math.min(r, Math.min(w, h) / 2);
    this.out.push(
      `<rect x="${n(cx - w / 2)}" y="${n(cy - h / 2)}" width="${n(w)}" height="${n(h)}" rx="${n(rr)}" fill="${hex(fill)}"${clip ? ` clip-path="url(#${clip})"` : ""}/>`,
    );
  }

  /** A border inside the rect, as LVGL draws it. */
  ring(cx: number, cy: number, w: number, h: number, r: number, color: number, width: number): void {
    const iw = w - width;
    const ih = h - width;
    if (iw <= 0 || ih <= 0) return;
    const rr = Math.max(0, Math.min(r, Math.min(w, h) / 2) - width / 2);
    this.out.push(
      `<rect x="${n(cx - iw / 2)}" y="${n(cy - ih / 2)}" width="${n(iw)}" height="${n(ih)}" rx="${n(rr)}" fill="none" stroke="${hex(color)}" stroke-width="${n(width)}"/>`,
    );
  }

  circle(cx: number, cy: number, r: number, fill: number, clip?: string): void {
    if (r <= 0) return;
    this.out.push(`<circle cx="${n(cx)}" cy="${n(cy)}" r="${n(r)}" fill="${hex(fill)}"${clip ? ` clip-path="url(#${clip})"` : ""}/>`);
  }

  line(p: [number, number][], width: number, color: number): void {
    this.out.push(
      `<polyline points="${pts(p)}" fill="none" stroke="${hex(color)}" stroke-width="${n(Math.max(2, Math.min(16, width)))}" stroke-linecap="round" stroke-linejoin="round"/>`,
    );
  }

  /** Quadratic curve p0 -> p2 with control c, stroked. */
  quad(p0: [number, number], c: [number, number], p2: [number, number], width: number, color: number, closedFill?: number): void {
    const d = `M${n(p0[0])},${n(p0[1])} Q${n(c[0])},${n(c[1])} ${n(p2[0])},${n(p2[1])}${closedFill !== undefined ? " Z" : ""}`;
    const fill = closedFill !== undefined ? hex(closedFill) : "none";
    this.out.push(`<path d="${d}" fill="${fill}" stroke="${hex(color)}" stroke-width="${n(width)}" stroke-linecap="round" stroke-linejoin="round"/>`);
  }

  poly(p: [number, number][], fill: number): void {
    this.out.push(`<polygon points="${pts(p)}" fill="${hex(fill)}"/>`);
  }

  clip(cx: number, cy: number, w: number, h: number, r: number): string {
    const id = `${this.prefix}-c${this.clips++}`;
    this.out.push(
      `<clipPath id="${id}"><rect x="${n(cx - w / 2)}" y="${n(cy - h / 2)}" width="${n(w)}" height="${n(h)}" rx="${n(Math.min(r, Math.min(w, h) / 2))}"/></clipPath>`,
    );
    return id;
  }
}

const sinDeg = (deg: number) => Math.sin((deg * Math.PI) / 180);
const cosDeg = (deg: number) => Math.cos((deg * Math.PI) / 180);

function drop(svg: Svg, x: number, y: number, s: number): void {
  svg.poly([[x - (s * 7) / 10, y + s / 5], [x, y - s], [x + (s * 7) / 10, y + s / 5]], DROP);
  svg.circle(x, y + (s * 35) / 100, (s * 7) / 10, DROP);
}

function star(svg: Svg, x: number, y: number, r: number): void {
  const k = (r * 28) / 100;
  svg.poly([[x, y - r], [x + k, y - k], [x + r, y], [x + k, y + k], [x, y + r], [x - k, y + k], [x - r, y], [x - k, y - k]], SPARKLE);
}

/**
 * The pet in `mood` as an SVG document string. `pet` is a pack or a decoded
 * pack; an invalid pack throws PetPackError.
 */
export function renderPetSvg(pet: PetDef | Uint8Array, mood: PetMood | number, options: PetSvgOptions = {}): string {
  const def = pet instanceof Uint8Array ? parsePet(pet) : pet;
  const m = typeof mood === "number" ? mood : PET_MOODS.indexOf(mood);
  const moodIndex = m >= 0 && m < PET_MOODS.length ? m : PET_MOODS.indexOf("idle");
  const pose = poseFor(def, moodIndex);
  const face = def.face;
  const body = petMoodColor(def, moodIndex);
  const dark = petMix(body, INK, DARK_T);
  const roleColor = (role: number): number =>
    [
      body,
      dark,
      petMix(body, WHITE, LIGHT_T),
      petMix(body, CORAL, CHEEK_T),
      def.accent,
      petMix(def.accent, INK, ACC_DARK_T),
      def.cream,
      INK,
      WHITE,
      FLAME,
      SURFACE,
      FOG,
      YELLOW,
    ][role] ?? body;
  const aux = def.aux[moodIndex];
  const squish = PET_MOODS[moodIndex] === "sleeping" ? face.sleepDrop : 0;
  const headDy = (squish * face.eyeSq) / 16;
  const mouthDy = (squish * face.mouthSq) / 16;
  const svg = new Svg(options.idPrefix ?? `pet-${def.id}-${moodIndex}`);

  // Parts, back to front; a mirrored part is drawn again at -x.
  const anchorEnd: [number, number][][] = def.parts.map(() => [[0, 0], [0, 0]]);
  def.parts.forEach((part, index) => {
    const shown = part.drivers.every((d) => (d.op === 2 ? aux[d.aux] !== 0 : d.op === 3 ? aux[d.aux] === d.arg : true));
    for (let side = 0; side < (part.mirror ? 2 : 1); side++) {
      const sx = side === 0 ? 1 : -1;
      const p: [number, number][] = part.points.map(([x, y]) => [x * sx, y]);
      for (const d of part.drivers) {
        if (d.op > 1) continue;
        const amount = (aux[d.aux] * d.scale * (d.op === 0 ? sx : 1)) / 16;
        p.forEach((pt, k) => {
          if (d.arg === 7 || d.arg === k) pt[d.op] += amount;
        });
      }
      if (part.anchor >= 0) {
        const a = def.parts[part.anchor];
        const as = a.mirror ? side : 0;
        const ax = a.mirror ? anchorEnd[part.anchor][as][0] : anchorEnd[part.anchor][0][0] * sx;
        for (const pt of p) {
          pt[0] += ax;
          pt[1] += anchorEnd[part.anchor][as][1];
        }
      }
      if (part.shape === 1) anchorEnd[index][side] = [...p[p.length - 1]];
      const dy = part.layer === 2 || part.layer === 3 ? headDy : 0;
      for (const pt of p) pt[1] += dy;
      if (!shown) continue;
      const color = roleColor(part.role);
      if (part.shape === 0) {
        let h = part.h;
        let cy = p[0][1];
        if (part.squash) {
          h -= squish;
          cy += squish / 2;
        }
        if (part.ring) svg.ring(p[0][0], cy, part.w, h, part.r, color, 2.5);
        else svg.rect(p[0][0], cy, part.w, h, part.r, color);
      } else if (part.shape === 1) {
        svg.line(p, part.width, color);
      } else {
        svg.poly(p, color);
      }
    }
  });

  // Eyes.
  const w = pose.eyeSize;
  const hf = (w * 6) / 5;
  const eyeY = face.eyeY + headDy;
  const eo = pose.eyeOpen;
  const pdx = Math.max(-100, Math.min(100, pose.pupilDx));
  const pdy = Math.max(-100, Math.min(100, pose.pupilDy));
  for (const sx of [-1, 1]) {
    const x = sx * face.eyeDx;
    const y = eyeY;
    if (eo < EYE_SHUT_OPEN) {
      svg.quad([x - w / 2, y], [x, y + 9], [x + w / 2, y], 4, INK);
      continue;
    }
    let lid = pose.lid;
    if (lid === PET_LID_OFF && !face.brows && eo < 100) lid = pose.browAngle;
    const hasLid = lid !== PET_LID_OFF;
    const h = Math.max(4, hasLid ? hf : (hf * eo) / 100);
    const r = Math.min(w, h) / 2;
    const dot = face.eyeStyle === 2;
    let clip: string | undefined;
    if (!dot) {
      clip = svg.clip(x, y, w, h, r);
      svg.rect(x, y, w, h, r, def.sclera);
    }
    const visTop = hasLid ? y + hf / 2 - (hf * eo) / 100 : y - h / 2;
    const visH = y + h / 2 - visTop;
    const visCy = (visTop + y + h / 2) / 2;
    if (face.eyeStyle === 1) {
      const pw = Math.max(3.5, ((w * pose.pupilSize) / 100) * 0.46);
      const ph = Math.min(visH - 4, w * 0.86);
      const tx = ((w / 2 - pw / 2 - 2) * pdx) / 100;
      const ty = (Math.max(0, (visH - ph) / 2 - 1) * pdy) / 100;
      svg.rect(x + tx, visCy + ty, pw, ph, pw / 2, INK, clip);
      svg.circle(x + tx + pw / 5 + 2, visCy + ty - ph * 0.26, 1.6, WHITE, clip);
    } else {
      const pr = (w * pose.pupilSize) / 200;
      const tx = ((w / 2 - pr - 2) * pdx) / 100;
      const ty = (Math.max(0, visH / 2 - pr - 2) * pdy) / 100;
      svg.circle(x + tx, visCy + ty, dot ? w / 2 : pr, INK, clip);
      svg.circle(x + tx + pr * 0.35, visCy + ty - pr * 0.35, Math.max(1.5, pr * 0.28), WHITE, clip);
    }
    if (hasLid) {
      const k = (sinDeg(lid) / Math.max(1e-3, cosDeg(lid))) * (w / 2);
      const xi = x - sx * (w / 2 + 1);
      const xo = x + sx * (w / 2 + 1);
      const yi = visTop + k;
      const yo = visTop - k;
      const top = y - hf / 2 - 3;
      svg.poly([[xi, top], [xo, top], [xo, yo], [xi, yi]], body);
      svg.line([[xi, yi], [xo, yo]], 4, dark);
    }
  }

  // Brows.
  if (face.brows && eo >= EYE_SHUT_OPEN) {
    for (const [side, sx] of [[0, -1], [1, 1]] as const) {
      const angle = pose.browAngle + (side === 0 ? pose.browL : pose.browR);
      if (Math.abs(angle) < BROW_MIN_ANGLE) continue;
      const lift = side === 0 ? Math.max(0, -pose.browLift) : Math.max(0, pose.browLift);
      const x = sx * face.eyeDx;
      const by = eyeY - w * 0.6 - 9 - lift;
      const ang = angle * -sx;
      const dx = 11 * cosDeg(ang);
      const dy = 11 * sinDeg(ang);
      svg.line([[x - dx, by - dy], [x + dx, by + dy]], 4, INK);
    }
  }

  // Mouth.
  const own = (acc: number) => (face.accMask & (1 << acc)) !== 0;
  const curve = pose.mouthCurve;
  const hw = pose.mouthW > 0 ? pose.mouthW : face.mouthHw;
  const cx = pose.mouthDx;
  const my = face.mouthY + mouthDy;
  const sag = (curve * hw * 6) / 1000;
  const y0 = my - sag / 2;
  if (pose.mouthOpen > 5) {
    if (curve > 40) {
      const hh = 8 + (pose.mouthOpen * 16) / 100;
      svg.quad([cx - hw, y0 - 2], [cx, y0 + 2 * hh], [cx + hw, y0 - 2], 4, INK, INK);
      svg.rect(cx, y0 + hh - 1, 14, 7, 3.5, TONGUE);
    } else {
      const mh = 6 + pose.mouthOpen / 5;
      svg.rect(cx, my, 14 + pose.mouthOpen / 10, mh, Math.min(mh / 2, 7), INK);
    }
  } else {
    if (pose.accessory === ACC_TONGUE && !own(ACC_TONGUE)) svg.rect(cx + hw - 4, y0 + (sag * 9) / 10 + 3.5, 10, 9, 4.5, TONGUE);
    svg.quad([cx - hw, y0], [cx, y0 + 2 * sag], [cx + hw, y0], 4, INK);
    if (face.mouthSet === 1) {
      const fy = y0 + (sag * 45) / 100;
      for (const s of [-9, 9]) svg.poly([[cx + s - 3.5, fy], [cx + s, fy + 7], [cx + s + 3.5, fy]], WHITE);
    }
  }

  // The mood's accessory, unless the pet draws its own.
  const acc = pose.accessory;
  if (acc > 0 && !own(acc)) {
    if (acc === ACC_ZZZ) {
      for (const [x, y, s] of [[62, -52, 8], [76, -70, 10], [90, -92, 13]]) {
        svg.line([[x, y], [x + s, y], [x, y + s], [x + s, y + s]], 3, FOG);
      }
    } else if (acc === ACC_SWEAT) {
      drop(svg, face.eyeDx + 37, face.sweatY + headDy, 8);
    } else if (acc === ACC_TEAR) {
      drop(svg, -face.eyeDx, face.eyeY + 26 + headDy, 6);
    } else if (acc === ACC_SPARKLES) {
      for (const [x, y, r] of [[-88, -44, 9], [88, -58, 11], [96, 0, 6]]) star(svg, x, y, r);
    } else if (acc === ACC_DOTS) {
      for (let i = 0; i < 3; i++) svg.circle(64 + 13 * i, -66 - 9 * i, 3 + i, CREAM);
    }
  }

  const width = options.width ?? 220;
  const height = options.height ?? 190;
  const filter = options.gray === true ? ' style="filter:grayscale(1)"' : "";
  return (
    `<svg xmlns="http://www.w3.org/2000/svg" width="${n(width)}" height="${n(height)}" viewBox="-110 -107 220 190"${filter}>` +
    svg.out.join("") +
    "</svg>"
  );
}
