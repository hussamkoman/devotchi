// Writes the preview sheet of each pet (renderPetSheetSvg) as <out>/<id>.svg,
// and as <out>/<id>.png with --png (needs ImageMagick's `magick`).
//
//   node packages/shared/scripts/pet-sheet.mjs [--out DIR] [--png] [<id> | <file.pet.json>]...
//
// Without names it draws every pets/*.pet.json. Needs the shared build.

import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import process from "node:process";
import { URL, fileURLToPath } from "node:url";
import { encodePet, parsePet, renderPetSheetSvg } from "../dist/index.js";

const petsDir = fileURLToPath(new URL("../../../pets/", import.meta.url));
const args = process.argv.slice(2);
let out = join(tmpdir(), "devotchi-pet-sheets");
let png = false;
const names = [];
for (let i = 0; i < args.length; i++) {
  if (args[i] === "--out") out = resolve(args[++i] ?? "");
  else if (args[i] === "--png") png = true;
  else names.push(args[i]);
}
const files = names.length
  ? names.map((n) => (n.endsWith(".json") ? resolve(n) : join(petsDir, `${n}.pet.json`)))
  : readdirSync(petsDir)
      .filter((f) => f.endsWith(".pet.json") && !f.startsWith("_"))
      .sort()
      .map((f) => join(petsDir, f));

mkdirSync(out, { recursive: true });
for (const file of files) {
  let pack;
  try {
    pack = encodePet(JSON.parse(readFileSync(file, "utf8")));
  } catch (err) {
    process.stderr.write(`${file}: ${err.message}\n`);
    process.exitCode = 1;
    continue;
  }
  const { id } = parsePet(pack);
  const svg = join(out, `${id}.svg`);
  writeFileSync(svg, renderPetSheetSvg(pack));
  process.stdout.write(`${svg}\n`);
  if (png) {
    execFileSync("magick", ["-background", "none", svg, join(out, `${id}.png`)], { stdio: "inherit" });
    process.stdout.write(`${join(out, `${id}.png`)}\n`);
  }
}
