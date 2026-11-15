// The sim's pet store (protocol 14.8): the device side of `pet_cmd` with the
// firmware's checks in the firmware's order, in memory. Built-in pets are
// Devo and Ember, as compiled into the firmware.

import {
  PET_ID_RE,
  PET_MAX_BYTES,
  buildPets,
  crc32,
  crc32Hex,
  validatePet,
  type PetResultError,
  type PetsEntry,
  type PetsMessage,
  type PetsResult,
} from "@devotchi/shared";

export const SIM_BUILTIN_PETS: readonly PetsEntry[] = [
  { id: "devo", rev: 1, builtin: true },
  { id: "ember", rev: 1, builtin: true },
];
export const SIM_PET_SLOTS = 8;
const DEFAULT_PET = "devo";
const MIN_PACK_BYTES = 36;
const CRC_RE = /^[0-9a-f]{8}$/;

export interface SimPetsDeps {
  /** GET /api/pets/<id>/pack on the configured server with the sim's token. */
  fetchPack: (id: string) => Promise<Response>;
  send: (msg: PetsMessage) => void;
  emit: (event: string, fields: Record<string, unknown>) => void;
}

type Outcome = { ok: true } | { ok: false; error: PetResultError };
const failed = (error: PetResultError): Outcome => ({ ok: false, error });

export class SimPets {
  private readonly stored = new Map<string, { rev: number; bytes: Uint8Array }>();
  private selected = DEFAULT_PET;
  private fetching = false;

  constructor(private readonly deps: SimPetsDeps) {}

  get selectedPet(): string {
    return this.selected;
  }

  /** The `pets` state; sent once after register_ack and after every change. */
  report(result?: PetsResult): void {
    const installed: PetsEntry[] = [...SIM_BUILTIN_PETS, ...[...this.stored].map(([id, p]) => ({ id, rev: p.rev }))];
    this.deps.send(
      buildPets({
        selected: this.selected,
        slots: SIM_PET_SLOTS,
        free: SIM_PET_SLOTS - this.stored.size,
        installed,
        ...(result ? { result } : {}),
      }),
    );
  }

  /** Handles one `pet_cmd` frame and always answers with a `pets` result. */
  async onCommand(msg: Record<string, unknown>): Promise<void> {
    const op = typeof msg.op === "string" && /^[a-z_]{1,16}$/.test(msg.op) ? msg.op : "unknown";
    const pet = typeof msg.pet === "string" && PET_ID_RE.test(msg.pet) ? msg.pet : "";
    this.deps.emit("pet_cmd", { op: msg.op, pet: msg.pet });
    const outcome = await this.run(op, pet, msg);
    const result: PetsResult = { op, pet, ok: outcome.ok };
    if (!outcome.ok) result.error = outcome.error;
    this.deps.emit("pets", { ...result, selected: this.selected, stored: [...this.stored.keys()] });
    this.report(result);
  }

  private async run(op: string, pet: string, msg: Record<string, unknown>): Promise<Outcome> {
    // A malformed command is refused before any URL is built or the store is touched.
    if (pet === "") return failed("unknown");
    const builtin = SIM_BUILTIN_PETS.some((p) => p.id === pet);
    switch (op) {
      case "select":
        if (!builtin && !this.stored.has(pet)) return failed("unknown");
        this.selected = pet;
        return { ok: true };
      case "remove":
        if (builtin) return failed("builtin");
        if (!this.stored.delete(pet)) return failed("unknown");
        if (this.selected === pet) this.selected = DEFAULT_PET;
        return { ok: true };
      case "install":
        return this.install(pet, builtin, msg);
      default:
        return failed("unknown");
    }
  }

  private async install(pet: string, builtin: boolean, msg: Record<string, unknown>): Promise<Outcome> {
    const { rev, size, crc32: crc, select } = msg;
    if (typeof rev !== "number" || !Number.isInteger(rev) || rev < 1 || rev > 255) return failed("unknown");
    if (typeof size !== "number" || !Number.isInteger(size) || size < MIN_PACK_BYTES || size > PET_MAX_BYTES) return failed("unknown");
    if (typeof crc !== "string" || !CRC_RE.test(crc)) return failed("unknown");
    if (select !== undefined && typeof select !== "boolean") return failed("unknown");
    if (builtin) return failed("builtin");
    if (this.fetching) return failed("busy");
    if (!this.stored.has(pet) && this.stored.size >= SIM_PET_SLOTS) return failed("full");
    this.fetching = true;
    let body: Uint8Array;
    try {
      const res = await this.deps.fetchPack(pet);
      if (res.status !== 200) {
        await res.body?.cancel();
        return failed("http");
      }
      if (res.headers.get("content-length") !== String(size)) {
        await res.body?.cancel();
        return failed("size");
      }
      body = new Uint8Array(await res.arrayBuffer());
    } catch {
      return failed("http");
    } finally {
      this.fetching = false;
    }
    if (body.length !== size) return failed("http");
    const trailer = (body[size - 4] | (body[size - 3] << 8) | (body[size - 2] << 16) | (body[size - 1] << 24)) >>> 0;
    const computed = crc32(body.subarray(0, size - 4));
    if (crc32Hex(computed) !== crc || computed !== trailer) return failed("crc");
    const check = validatePet(body);
    if (!check.ok) return failed(check.error);
    if (check.pet.id !== pet || check.pet.rev !== rev) return failed("mismatch");
    this.stored.set(pet, { rev, bytes: body });
    if (select === true) this.selected = pet;
    return { ok: true };
  }
}
