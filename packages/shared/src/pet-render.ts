// Static SVG of a pet in one mood or reaction: the drawing rules of the
// firmware rig (core/pet_pose.c, ui/pet_view.c) at rest: no bob, blink,
// glance, jitter or oscillators; a reaction is drawn at its peak on top of
// idle. Parts are drawn back to front, then eyes, brows, mouth, the mood's
// accessory and the reaction's effect. The outline option draws the night
// clock's style (fills become 2 px borders on black). Everything in the SVG
// comes from numbers; no pack text reaches the markup.

import {
  PET_LID_OFF,
  PET_MOODS,
  PET_REACTIONS,
  parsePet,
  petMix,
  petMoodColor,
  type PetDef,
  type PetMood,
  type PetReaction,
} from "./petpack.js";

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
const HEART = 0xff8fa3;
const SLATE = 0x7c8597;
const NIGHT = 0x9a4a26;
const BLACK = 0x000000;
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

/** The pose of a mood or reaction at rest (pet-animations 1 and 3): the face channels a static frame uses. */
export interface PetPose {
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
const EXTRA: Partial<Record<PetMood, Partial<PetPose>>> = {
  focused: { mouthW: 11 },
  working: { mouthW: 13 },
  thinking: { browL: -4, browR: -16, browLift: 5, mouthDx: 9, mouthW: 8 },
};
// The channels each reaction sets on the mood's pose; the brows reset to the mood's angle.
const REACT: Record<PetReaction, Partial<PetPose>> = {
  pat: { mouthCurve: 90, mouthOpen: 0, browAngle: 0, accessory: 0, mouthDx: 0, mouthW: 0 },
  poke: { eyeOpen: 25, mouthCurve: -20, mouthOpen: 0, browAngle: -14, lid: PET_LID_OFF, accessory: 0, mouthDx: 0 },
  dizzy: { mouthCurve: -10, mouthOpen: 20, browAngle: 0, accessory: 0, mouthDx: 0 },
  wake: { eyeOpen: 38, mouthCurve: 0, mouthOpen: 70, browAngle: -16, eyeSize: 26, lid: PET_LID_OFF, accessory: 0, mouthDx: 0 },
  attention: {
    eyeOpen: 100,
    eyeSize: 30,
    pupilSize: 34,
    pupilDy: -30,
    pupilDx: 0,
    mouthCurve: 0,
    mouthOpen: 22,
    browAngle: -14,
    lid: PET_LID_OFF,
    accessory: 0,
    mouthDx: 0,
  },
};
/** Override and oscillator number of the first reaction (format 1). */
const REACT_BASE = 16;
/** Format channel number -> pose field; channels a static frame does not use are absent. */
const CHANNEL_FIELD: Partial<Record<number, keyof PetPose>> = {
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

function applyOverrides(def: PetDef, number: number, pose: PetPose): PetPose {
  for (const o of def.overrides) {
    const field = CHANNEL_FIELD[o.channel];
    if (o.mood === number && field !== undefined) pose[field] = o.value;
  }
  return pose;
}

function poseFor(def: PetDef, mood: number): PetPose {
  const name = PET_MOODS[mood];
  const b = BASE[name];
  const pose: PetPose = {
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
  return applyOverrides(def, mood, pose);
}

/** The pose a frame shows: the shared table with the pack's overrides; a reaction on top of idle. */
export function petPose(pet: PetDef | Uint8Array, frame: PetMood | PetReaction): PetPose {
  const def = pet instanceof Uint8Array ? parsePet(pet) : pet;
  const r = (PET_REACTIONS as readonly string[]).indexOf(frame);
  if (r < 0) return poseFor(def, Math.max(0, PET_MOODS.indexOf(frame as PetMood)));
  const pose = { ...poseFor(def, PET_MOODS.indexOf("idle")), browL: 0, browR: 0, browLift: 0, ...REACT[frame as PetReaction] };
  return applyOverrides(def, REACT_BASE + r, pose);
}

export interface PetSvgOptions {
  /** Pixel size of the SVG; default the 220x190 body-unit box. */
  width?: number;
  height?: number;
  /** Greyscale (pet-animations 5 rule 7). */
  gray?: boolean;
  /** The night clock's outline style: parts marked `outline: false`, brows, lids and effects are left out. */
  outline?: boolean;
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

function heart(svg: Svg, x: number, y: number, s: number): void {
  svg.circle(x - s / 2, y - (s * 3) / 10, (s * 55) / 100, HEART);
  svg.circle(x + s / 2, y - (s * 3) / 10, (s * 55) / 100, HEART);
  svg.poly([[x - (s * 105) / 100, y - s / 10], [x + (s * 105) / 100, y - s / 10], [x, y + s]], HEART);
}

/** A reaction's effect at the moment its pose is full (ui/pet_view.c build_fx). */
function effect(svg: Svg, reaction: PetReaction): void {
  if (reaction === "pat") {
    heart(svg, 78, -62, 8);
    heart(svg, 96, -38, 5);
  } else if (reaction === "poke") {
    for (const [x0, y0, x1, y1] of [[-84, -34, -96, -44], [-90, -14, -104, -14], [84, -34, 96, -44], [90, -14, 104, -14]]) {
      svg.line([[x0, y0], [x1, y1]], 3, CREAM);
    }
  } else if (reaction === "attention") {
    svg.circle(80, -62, 14, YELLOW);
    svg.rect(80, -66, 5, 13, 2.5, INK);
    svg.circle(80, -54, 3, INK);
  } else if (reaction === "wake") {
    svg.line([[66, -56], [75, -56], [66, -47], [75, -47]], 3, SLATE);
  }
}

/**
 * The pet in `mood` (or at the peak of a reaction) as an SVG document
 * string. `pet` is a pack or a decoded pack; an invalid pack throws
 * PetPackError.
 */
export function renderPetSvg(pet: PetDef | Uint8Array, mood: PetMood | PetReaction | number, options: PetSvgOptions = {}): string {
  const def = pet instanceof Uint8Array ? parsePet(pet) : pet;
  const reactIndex = typeof mood === "string" ? (PET_REACTIONS as readonly string[]).indexOf(mood) : -1;
  const reaction = reactIndex >= 0 ? PET_REACTIONS[reactIndex] : undefined;
  const m = reaction !== undefined ? PET_MOODS.indexOf("idle") : typeof mood === "number" ? mood : PET_MOODS.indexOf(mood as PetMood);
  const moodIndex = m >= 0 && m < PET_MOODS.length ? m : PET_MOODS.indexOf("idle");
  const pose = reaction !== undefined ? petPose(def, reaction) : poseFor(def, moodIndex);
  const night = options.outline === true;
  const ink = night ? NIGHT : INK;
  const stroke = night ? 2 : 4;
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
  const aux = def.aux[reaction !== undefined ? PET_MOODS.length + reactIndex : moodIndex];
  const squish = reaction === undefined && PET_MOODS[moodIndex] === "sleeping" ? face.sleepDrop : 0;
  const headDy = (squish * face.eyeSq) / 16;
  const mouthDy = (squish * face.mouthSq) / 16;
  const svg = new Svg(options.idPrefix ?? `pet-${def.id}-${reaction ?? moodIndex}`);
  /** A filled rounded rect, or in outline mode a 2 px border on black. */
  const fillRect = (cx: number, cy: number, w: number, h: number, r: number, color: number) => {
    if (!night) return svg.rect(cx, cy, w, h, r, color);
    svg.rect(cx, cy, w, h, r, BLACK);
    svg.ring(cx, cy, w, h, r, NIGHT, 2);
  };

  // Parts, back to front; a mirrored part is drawn again at -x.
  const anchorEnd: [number, number][][] = def.parts.map(() => [[0, 0], [0, 0]]);
  def.parts.forEach((part, index) => {
    const shown =
      !(night && part.noOutline) && part.drivers.every((d) => (d.op === 2 ? aux[d.aux] !== 0 : d.op === 3 ? aux[d.aux] === d.arg : true));
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
      const color = night ? NIGHT : roleColor(part.role);
      if (part.shape === 0) {
        let h = part.h;
        let cy = p[0][1];
        if (part.squash) {
          h -= squish;
          cy += squish / 2;
        }
        if (part.ring && !night) svg.ring(p[0][0], cy, part.w, h, part.r, color, 2.5);
        else fillRect(p[0][0], cy, part.w, h, part.r, color);
      } else if (part.shape === 1) {
        svg.line(p, night ? 2 : part.width, color);
      } else if (night) {
        svg.line([...p, p[0]], 2, NIGHT);
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
    if (reaction === "pat") {
      svg.quad([x - w / 2, y + 4], [x, y - 12], [x + w / 2, y + 4], stroke, ink);
      continue;
    }
    if (reaction === "dizzy") {
      const spiral: [number, number][] = [];
      for (let i = 0; i < 12; i++) {
        const ang = (i * 756) / 11;
        const r = 1 + (i * (w / 2 - 1)) / 11;
        spiral.push([x + cosDeg(ang) * r, y + sinDeg(ang) * r]);
      }
      svg.line(spiral, 3, ink);
      continue;
    }
    if (eo < EYE_SHUT_OPEN) {
      svg.quad([x - w / 2, y], [x, y + 9], [x + w / 2, y], stroke, ink);
      continue;
    }
    let lid = pose.lid;
    if (lid === PET_LID_OFF && !face.brows && eo < 100) lid = pose.browAngle;
    const hasLid = lid !== PET_LID_OFF && !night;
    const h = Math.max(4, hasLid ? hf : (hf * eo) / 100);
    const r = Math.min(w, h) / 2;
    if (night) {
      fillRect(x, y + hf / 2 - h / 2, w, h, r, NIGHT);
      continue;
    }
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
  if (face.brows && !night && eo >= EYE_SHUT_OPEN && reaction !== "pat" && reaction !== "dizzy") {
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
    if (curve > 40 && !night) {
      const hh = 8 + (pose.mouthOpen * 16) / 100;
      svg.quad([cx - hw, y0 - 2], [cx, y0 + 2 * hh], [cx + hw, y0 - 2], 4, INK, INK);
      svg.rect(cx, y0 + hh - 1, 14, 7, 3.5, TONGUE);
    } else {
      const mh = 6 + pose.mouthOpen / 5;
      fillRect(cx, my, 14 + pose.mouthOpen / 10, mh, Math.min(mh / 2, 7), INK);
    }
  } else {
    if (!night && pose.accessory === ACC_TONGUE && !own(ACC_TONGUE)) svg.rect(cx + hw - 4, y0 + (sag * 9) / 10 + 3.5, 10, 9, 4.5, TONGUE);
    svg.quad([cx - hw, y0], [cx, y0 + 2 * sag], [cx + hw, y0], stroke, ink);
    if (face.mouthSet === 1 && !night) {
      const fy = y0 + (sag * 45) / 100;
      for (const s of [-9, 9]) svg.poly([[cx + s - 3.5, fy], [cx + s, fy + 7], [cx + s + 3.5, fy]], WHITE);
    }
  }

  // The mood's accessory, unless the pet draws its own; at night only the zzz.
  const acc = pose.accessory;
  if (acc > 0 && !own(acc) && (!night || acc === ACC_ZZZ)) {
    if (acc === ACC_ZZZ) {
      for (const [x, y, s] of [[62, -52, 8], [76, -70, 10], [90, -92, 13]]) {
        svg.line([[x, y], [x + s, y], [x, y + s], [x + s, y + s]], night ? 2 : 3, night ? NIGHT : FOG);
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
  if (reaction !== undefined && !night) effect(svg, reaction);

  const width = options.width ?? 220;
  const height = options.height ?? 190;
  const filter = options.gray === true ? ' style="filter:grayscale(1)"' : "";
  return (
    `<svg xmlns="http://www.w3.org/2000/svg" width="${n(width)}" height="${n(height)}" viewBox="-110 -107 220 190"${filter}>` +
    svg.out.join("") +
    "</svg>"
  );
}
