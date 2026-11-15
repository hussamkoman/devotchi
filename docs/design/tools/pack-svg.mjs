// Prints every frame of a pet's golden pack as SVG, drawn by the dashboard's
// renderer, for the design mockups: {"<mood|reaction>": {plain, outline, gray}}.
// Clip ids use the prefix XPFX for the caller to make unique. Needs the shared build.
//
//   node docs/design/tools/pack-svg.mjs <id>

import { readFileSync } from "node:fs";
import process from "node:process";
import { PET_MOODS, PET_REACTIONS, renderPetSvg } from "../../../packages/shared/dist/index.js";

const id = process.argv[2] ?? "";
if (!/^[a-z0-9]{1,8}$/.test(id)) throw new Error("usage: pack-svg.mjs <id>");
const pack = new Uint8Array(readFileSync(new URL(`../../../pets/golden/${id}.pet`, import.meta.url)));
const frames = {};
for (const frame of [...PET_MOODS, ...PET_REACTIONS]) {
  frames[frame] = {
    plain: renderPetSvg(pack, frame, { idPrefix: "XPFX" }),
    outline: renderPetSvg(pack, frame, { outline: true, idPrefix: "XPFX" }),
    gray: renderPetSvg(pack, frame, { gray: true, idPrefix: "XPFX" }),
  };
}
process.stdout.write(JSON.stringify(frames));
