// Builds the pet catalog (protocol 14.8 `source: "catalog"`) into
// dist/pets.json: every pets/*.pet.json of the repository (the compiled-in
// pets) and every packages/server/pets/*.pet.json (gallery-only pets),
// encoded with the shared codec. Sorted by id with fixed formatting, so the
// same sources always give the same file and the same CRCs.

import { Buffer } from "node:buffer";
import { mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import process from "node:process";
import { URL } from "node:url";
import { encodePet, validatePet } from "@devotchi/shared";

const dirs = [new URL("../../../pets/", import.meta.url), new URL("../pets/", import.meta.url)];
const packs = new Map();
for (const dir of dirs) {
  for (const file of readdirSync(dir).filter((f) => f.endsWith(".pet.json")).sort()) {
    const path = new URL(file, dir);
    let pack;
    try {
      pack = encodePet(JSON.parse(readFileSync(path, "utf8")));
    } catch (err) {
      throw new Error(`${path.pathname}: ${err.message}`, { cause: err });
    }
    const result = validatePet(pack);
    if (!result.ok) throw new Error(`${path.pathname}: ${result.message}`);
    if (packs.has(result.pet.id)) throw new Error(`${path.pathname}: pet id ${result.pet.id} is defined twice`);
    packs.set(result.pet.id, Buffer.from(pack).toString("base64"));
  }
}
const catalog = [...packs.keys()].sort().map((id) => ({ id, pack: packs.get(id) }));
mkdirSync(new URL("../dist/", import.meta.url), { recursive: true });
writeFileSync(new URL("../dist/pets.json", import.meta.url), JSON.stringify(catalog, null, 1) + "\n");
process.stdout.write(`pet catalog: ${catalog.map((p) => p.id).join(", ")}\n`);
