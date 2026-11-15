// The preview sheet of one pet (pet-animations 5): the 11 moods and 5
// reactions in colour, the night outline at clock size, and every frame
// again in greyscale, drawn with renderPetSvg from the same pack the
// firmware shows. One self-contained SVG document.

import { PET_MOODS, PET_REACTIONS, crc32Hex, parsePet, type PetDef } from "./petpack.js";
import { renderPetSvg } from "./pet-render.js";

const COLS = 6;
const CELL_W = 165;
const CELL_H = 143;
const LABEL_H = 18;
const HEAD_H = 44;
const SECTION_H = 26;
const PAD = 12;
const BG = "#0C0E14";
const TEXT = "#F3EEE3";
const SUB = "#A7AEBB";
/** The clock face draws the pet at 9/16. */
const NIGHT_SCALE = 9 / 16;

const esc = (s: string) => s.replace(/[&<>"]/g, (c) => `&#${c.charCodeAt(0)};`);

/** Rec. 709 luma of every #RRGGBB in an SVG, so the greyscale rows need no filter support. */
function grayscale(svg: string): string {
  return svg.replace(/#([0-9A-F]{6})\b/gi, (_, hex: string) => {
    const v = parseInt(hex, 16);
    const y = Math.round(0.2126 * (v >> 16) + 0.7152 * ((v >> 8) & 0xff) + 0.0722 * (v & 0xff));
    return "#" + y.toString(16).padStart(2, "0").repeat(3).toUpperCase();
  });
}

/** The sheet as an SVG document; `pet` is a pack or a decoded pack. */
export function renderPetSheetSvg(pet: PetDef | Uint8Array): string {
  const def = pet instanceof Uint8Array ? parsePet(pet) : pet;
  const frames = [...PET_MOODS, ...PET_REACTIONS];
  const out: string[] = [];
  let y = PAD;
  out.push(
    `<text x="${PAD}" y="${y + 20}" font-size="20" font-weight="bold" fill="${TEXT}">${esc(def.name)}</text>`,
    `<text x="${PAD}" y="${y + 38}" font-size="12" fill="${SUB}">${esc(def.id)} · rev ${def.rev} · ${def.size} bytes · crc32 ${crc32Hex(def.crc32)}</text>`,
  );
  y += HEAD_H;

  const section = (title: string) => {
    out.push(`<text x="${PAD}" y="${y + 17}" font-size="14" fill="${TEXT}">${esc(title)}</text>`);
    y += SECTION_H;
  };
  const cell = (index: number, label: string, svg: string, background?: string) => {
    const x = PAD + (index % COLS) * CELL_W;
    const top = y + Math.floor(index / COLS) * (CELL_H + LABEL_H);
    if (background) out.push(`<rect x="${x}" y="${top}" width="${CELL_W - 4}" height="${CELL_H}" fill="${background}"/>`);
    out.push(`<g transform="translate(${x} ${top})">${svg}</g>`);
    out.push(`<text x="${x + (CELL_W - 4) / 2}" y="${top + CELL_H + 13}" font-size="12" text-anchor="middle" fill="${SUB}">${esc(label)}</text>`);
  };
  const rows = (n: number) => Math.ceil(n / COLS) * (CELL_H + LABEL_H);

  section("Moods and reactions");
  frames.forEach((f, i) => cell(i, f, renderPetSvg(def, f, { width: CELL_W - 4, height: CELL_H, idPrefix: `c-${f}` })));
  const nightW = Math.round(220 * NIGHT_SCALE);
  const nightH = Math.round(190 * NIGHT_SCALE);
  const night = renderPetSvg(def, "idle", { width: nightW, height: nightH, outline: true, idPrefix: "night" });
  const nx = (CELL_W - 4 - nightW) / 2;
  const ny = (CELL_H - nightH) / 2;
  cell(frames.length, "night outline (9/16)", `<rect width="${CELL_W - 4}" height="${CELL_H}" fill="#000000"/><g transform="translate(${nx} ${ny})">${night}</g>`);
  y += rows(frames.length + 1);

  section("Greyscale: every frame must still read");
  frames.forEach((f, i) => cell(i, f, grayscale(renderPetSvg(def, f, { width: CELL_W - 4, height: CELL_H, idPrefix: `g-${f}` }))));
  y += rows(frames.length);

  const width = PAD * 2 + COLS * CELL_W;
  return (
    `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${y + PAD}" viewBox="0 0 ${width} ${y + PAD}" font-family="sans-serif">` +
    `<rect width="100%" height="100%" fill="${BG}"/>` +
    out.join("") +
    "</svg>"
  );
}
