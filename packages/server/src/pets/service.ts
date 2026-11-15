// Pets (protocol 14.8): the catalog, room uploads, the `pets` state each
// device reports and the `pet_cmd` sent to it. Every pack is checked with
// the shared codec before it is stored and again before it is served.

import {
  PET_ID_RE,
  PET_MAX_BYTES,
  PET_OPS,
  PetSourceError,
  ROOM_PETS_MAX,
  buildError,
  buildPetCmd,
  buildPets,
  encodePet,
  validatePet,
  type DevicePets,
  type DevicePetsResult,
  type PetCmdMessage,
  type PetInfo,
  type PetListEntry,
  type PetOp,
  type PetsMessage,
} from "@devotchi/shared";
import type { Db } from "../db/index.js";
import { findDevice } from "../db/devices.js";
import { countRoomPets, deleteRoomPet, findRoomPet, listRoomPets, putRoomPet } from "../db/pets.js";
import { HttpError, badRequest, notFound } from "../http/errors.js";
import type { Logger } from "../log.js";
import type { Conn } from "../rooms/conn.js";
import type { Room, RoomRegistry } from "../rooms/registry.js";
import type { CatalogPet } from "./catalog.js";

export interface PetServiceDeps {
  db: Db;
  rooms: RoomRegistry;
  log: Logger;
  catalog: CatalogPet[];
}

export interface StoredPet {
  info: PetInfo;
  bytes: Uint8Array;
  source: PetListEntry["source"];
  builtin?: boolean;
}

const conflict = (message: string) => new HttpError(409, "conflict", message);
const entry = (pet: StoredPet): PetListEntry => ({ ...pet.info, source: pet.source, ...(pet.builtin === true ? { builtin: true } : {}) });

/** A pet_cmd body after the checks of POST /api/devices/:id/pet. */
interface PetCommand {
  op: PetOp;
  pet: string;
  select: boolean;
}

export function parsePetCommand(body: unknown): PetCommand {
  if (typeof body !== "object" || body === null || Array.isArray(body)) throw badRequest("Body must be a JSON object");
  const fields = body as Record<string, unknown>;
  // The id goes into a device URL, so it is checked before anything else.
  if (typeof fields.pet !== "string" || !PET_ID_RE.test(fields.pet)) throw badRequest("pet must match [a-z0-9]{1,8}");
  if (typeof fields.op !== "string" || !(PET_OPS as readonly string[]).includes(fields.op)) {
    throw badRequest(`op must be one of ${PET_OPS.join(", ")}`);
  }
  for (const key of Object.keys(fields)) {
    if (key !== "op" && key !== "pet" && !(key === "select" && fields.op === "install")) throw badRequest(`Unknown field "${key.slice(0, 40)}"`);
  }
  if (fields.select !== undefined && typeof fields.select !== "boolean") throw badRequest("select must be a boolean");
  return { op: fields.op as PetOp, pet: fields.pet, select: fields.select === true };
}

export class PetService {
  private readonly catalog = new Map<string, StoredPet>();

  constructor(private readonly deps: PetServiceDeps) {
    for (const pet of deps.catalog) this.catalog.set(pet.info.id, { ...pet, source: "catalog" });
  }

  list(roomId: string): PetListEntry[] {
    const room = this.roomPets(roomId).map(entry);
    return [...[...this.catalog.values()].map(entry), ...room];
  }

  /** The pack served as GET /api/pets/:id/pack; undefined when the room cannot see it. */
  find(roomId: string, id: string): StoredPet | undefined {
    const shipped = this.catalog.get(id);
    if (shipped) return shipped;
    const row = findRoomPet(this.deps.db, roomId, id);
    return row ? this.checked(roomId, row.bytes) : undefined;
  }

  /**
   * POST /api/pets: a pack or its JSON source. Returns the list entry and
   * whether a new pack was stored (false when the same pack was uploaded again).
   */
  upload(roomId: string, body: { pack: Uint8Array } | { source: unknown }, now = Date.now()): { pet: PetListEntry; created: boolean } {
    let bytes: Uint8Array;
    if ("pack" in body) {
      bytes = body.pack;
    } else {
      try {
        bytes = encodePet(body.source);
      } catch (err) {
        if (err instanceof PetSourceError) throw badRequest(`Pet source refused: ${err.message}`);
        throw err;
      }
    }
    if (bytes.length > PET_MAX_BYTES) throw new HttpError(413, "payload_too_large", `A pack has at most ${PET_MAX_BYTES} bytes`);
    const result = validatePet(bytes);
    if (!result.ok) throw badRequest(`Pack refused (${result.error}): ${result.message}`);
    const info = result.pet;
    if (this.catalog.has(info.id)) throw conflict(`${info.id} is a catalog pet; give the pack another id`);
    const { db } = this.deps;
    let created = true;
    db.transaction(() => {
      const existing = findRoomPet(db, roomId, info.id);
      if (existing) {
        if (Buffer.from(existing.bytes).equals(Buffer.from(bytes))) {
          created = false;
          return;
        }
        if (info.rev <= existing.rev) throw conflict(`${info.id} is stored with rev ${existing.rev}; a changed pack needs a higher rev`);
      } else if (countRoomPets(db, roomId) >= ROOM_PETS_MAX) {
        throw conflict(`This room already has ${ROOM_PETS_MAX} pets; delete one first`);
      }
      putRoomPet(db, roomId, { id: info.id, rev: info.rev, bytes }, now);
    });
    return { pet: { ...info, source: "room" }, created };
  }

  /** DELETE /api/pets/:id. */
  remove(roomId: string, id: string): void {
    if (this.catalog.has(id)) throw conflict(`${id} is a catalog pet and cannot be deleted`);
    if (!PET_ID_RE.test(id) || !deleteRoomPet(this.deps.db, roomId, id)) throw notFound("No such pet in this room");
  }

  /** A `pets` message from a device or sim: kept per socket and forwarded to the room's dashboards. */
  onPets(conn: Conn, msg: PetsMessage, room: Room): void {
    const registration = conn.registration;
    if (!registration || (registration.kind !== "device" && registration.kind !== "sim")) {
      conn.send(buildError("NOT_ALLOWED", "only devices and sims send pets"));
      return;
    }
    if (conn.pets.hit(Date.now()) > 0) {
      conn.send(buildError("RATE_LIMITED", "too many pets messages"));
      return;
    }
    const { type: _type, device: _device, result, ...state } = msg;
    conn.lastPets = state;
    if (result) conn.lastPetsResult = { result, at: Date.now() };
    const forward = buildPets({ device: registration.identity, ...state, ...(result ? { result } : {}) });
    for (const consumer of room.consumers) {
      if (consumer.registration?.kind === "dashboard") consumer.send(forward);
    }
  }

  /** POST /api/devices/:id/pet; returns the time (ms) the command was sent. */
  command(roomId: string, deviceId: string, cmd: PetCommand): number {
    const row = findDevice(this.deps.db, roomId, deviceId);
    if (!row) throw notFound("No such device");
    const room = this.deps.rooms.get(roomId);
    const conns = room ? [...room.consumers].filter((conn) => conn.tokenId === row.token_id) : [];
    if (conns.length === 0) throw conflict("device offline");
    const capable = conns.filter((conn) => conn.caps.has("pets"));
    if (capable.length === 0) throw conflict("device does not support pets");
    if (cmd.op !== "select" && capable.some((conn) => conn.lastPets?.installed.some((p) => p.id === cmd.pet && p.builtin === true))) {
      throw conflict("built-in pet");
    }
    let msg: PetCmdMessage;
    if (cmd.op === "install") {
      const pet = this.find(roomId, cmd.pet);
      if (!pet) throw notFound("No such pet");
      // rev, size and crc32 come from the stored pack, never from the request.
      msg = buildPetCmd({ op: "install", pet: pet.info.id, rev: pet.info.rev, size: pet.info.size, crc32: pet.info.crc32, select: cmd.select });
    } else {
      msg = buildPetCmd({ op: cmd.op, pet: cmd.pet });
    }
    const at = Date.now();
    let sent = 0;
    for (const conn of capable) if (conn.send(msg)) sent++;
    if (sent === 0) throw conflict("device offline");
    return at;
  }

  /** The newest `pets` state of a device's sockets, for GET /api/devices. */
  static stateOf(conns: readonly Conn[]): DevicePets | undefined {
    let out: DevicePets | undefined;
    for (const conn of conns) if (conn.lastPets) out = conn.lastPets;
    return out;
  }

  /** The newest `pets` result of a device's sockets, for GET /api/devices. */
  static resultOf(conns: readonly Conn[]): DevicePetsResult | undefined {
    let latest: NonNullable<Conn["lastPetsResult"]> | null = null;
    for (const conn of conns) if (conn.lastPetsResult && (latest === null || conn.lastPetsResult.at >= latest.at)) latest = conn.lastPetsResult;
    return latest === null ? undefined : { ...latest.result, at: new Date(latest.at).toISOString() };
  }

  private roomPets(roomId: string): StoredPet[] {
    const out: StoredPet[] = [];
    for (const row of listRoomPets(this.deps.db, roomId)) {
      const pet = this.checked(roomId, row.bytes);
      if (pet) out.push(pet);
    }
    return out;
  }

  private checked(roomId: string, bytes: Uint8Array): StoredPet | undefined {
    const result = validatePet(bytes);
    if (result.ok) return { info: result.pet, bytes, source: "room" };
    this.deps.log.warn("stored pet pack is invalid", { room: roomId, error: result.message });
    return undefined;
  }
}
