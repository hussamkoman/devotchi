// Builds the pet catalog (protocol 14.8 `source: "catalog"`) into
// dist/pets.json: every pets/<id>.pet.json of the repository, encoded with
// the shared codec, the ids of pets/builtin.txt (the pets the firmware
// compiles in) flagged `builtin`. Files starting with _ (the template) are
// not pets. The built-ins come first in the firmware's order (the first is
// the default pet), then the rest by id, with fixed formatting, so the same
// sources always give the same file and the same CRCs.

import { Buffer } from "node:buffer";
import { mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import process from "node:process";
import { URL } from "node:url";
import { encodePet, validatePet } from "@devotchi/shared";

const dir = new URL("../../../pets/", import.meta.url);
const builtins = readFileSync(new URL("builtin.txt", dir), "utf8")
  .split("\n")
  .map((line) => line.replace(/#.*/, "").trim())
  .filter((id) => id !== "");
const packs = new Map();
for (const file of readdirSync(dir).filter((f) => f.endsWith(".pet.json") && !f.startsWith("_")).sort()) {
  const path = new URL(file, dir);
  let pack;
  try {
    pack = encodePet(JSON.parse(readFileSync(path, "utf8")));
  } catch (err) {
    throw new Error(`${path.pathname}: ${err.message}`, { cause: err });
  }
  const result = validatePet(pack);
  if (!result.ok) throw new Error(`${path.pathname}: ${result.message}`);
  if (`${result.pet.id}.pet.json` !== file) throw new Error(`${path.pathname}: the id ${result.pet.id} does not match the file name`);
  packs.set(result.pet.id, Buffer.from(pack).toString("base64"));
}
for (const id of builtins) if (!packs.has(id)) throw new Error(`pets/builtin.txt names ${id}, which has no pets/${id}.pet.json`);
const ids = [...builtins, ...[...packs.keys()].filter((id) => !builtins.includes(id)).sort()];
const catalog = ids.map((id) => ({ id, pack: packs.get(id), ...(builtins.includes(id) ? { builtin: true } : {}) }));
mkdirSync(new URL("../dist/", import.meta.url), { recursive: true });
writeFileSync(new URL("../dist/pets.json", import.meta.url), JSON.stringify(catalog, null, 1) + "\n");
process.stdout.write(`pet catalog: ${catalog.map((p) => (p.builtin ? `${p.id} (built in)` : p.id)).join(", ")}\n`);
