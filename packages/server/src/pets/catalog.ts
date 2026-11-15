// The packs shipped with the server (protocol 14.8 `source: "catalog"`):
// every pets/*.pet.json of the repository, built into dist/pets.json by
// scripts/build-pets.mjs with the firmware's built-ins flagged. Every pack is checked
// with the shared codec on load, so the server never offers one a device
// would refuse.

import { readFileSync } from "node:fs";
import { validatePet, type PetInfo } from "@devotchi/shared";
import { StartupError } from "../startup-error.js";

export interface CatalogPet {
  info: PetInfo;
  bytes: Uint8Array;
  /** Listed in pets/builtin.txt: the firmware built from this repository compiles it in. */
  builtin: boolean;
}

/** Set by the client's bundler, which ships the server without its dist/ directory. */
declare const __DEVOTCHI_PET_CATALOG__: string | undefined;

/** dist/pets.json, from src/pets/ (tests) and dist/pets/ (the build) alike. */
const CATALOG_FILE = new URL("../../dist/pets.json", import.meta.url);

export function parseCatalog(text: string, where: string): CatalogPet[] {
  let entries: unknown;
  try {
    entries = JSON.parse(text);
  } catch (err) {
    throw new StartupError(`${where} is not JSON: ${(err as Error).message}`);
  }
  if (!Array.isArray(entries)) throw new StartupError(`${where} must be an array`);
  return entries.map((entry: { id?: unknown; pack?: unknown; builtin?: unknown }, i) => {
    if (typeof entry?.pack !== "string") throw new StartupError(`${where}[${i}] has no pack`);
    if (entry.builtin !== undefined && entry.builtin !== true) throw new StartupError(`${where}[${i}].builtin must be true when present`);
    const bytes = new Uint8Array(Buffer.from(entry.pack, "base64"));
    const result = validatePet(bytes);
    if (!result.ok) throw new StartupError(`${where}[${i}]: ${result.message}`);
    if (result.pet.id !== entry.id) throw new StartupError(`${where}[${i}] is listed as ${String(entry.id)} but is ${result.pet.id}`);
    return { info: result.pet, bytes, builtin: entry.builtin === true };
  });
}

export function loadCatalog(): CatalogPet[] {
  if (typeof __DEVOTCHI_PET_CATALOG__ === "string") return parseCatalog(__DEVOTCHI_PET_CATALOG__, "the bundled pet catalog");
  let text: string;
  try {
    text = readFileSync(CATALOG_FILE, "utf8");
  } catch (err) {
    throw new StartupError(`cannot read the pet catalog ${CATALOG_FILE.pathname} (run the server build): ${(err as Error).message}`);
  }
  return parseCatalog(text, CATALOG_FILE.pathname);
}
