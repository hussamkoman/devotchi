// `devotchi pets`: the room's pet gallery (protocol 14.8) and the pets on its devices.

import { readFile } from "node:fs/promises";
import {
  PET_ID_RE,
  PET_MAX_BYTES,
  PET_SOURCE_MAX_BYTES,
  petResultText,
  type Device,
  type DevicePetsResult,
  type PetListEntry,
} from "@devotchi/shared";
import { ApiError, type ApiClient } from "../api.js";
import { connectServer, printJson, type Io } from "../context.js";
import { CliError, UsageError } from "../errors.js";

interface ServerOption {
  server?: string;
}

export interface PetSendOptions extends ServerOption {
  device?: string;
  use?: boolean;
  /** Seconds to wait for the device to report the change; 0 does not wait. */
  wait?: number;
}

const POLL_MS = 500;
const DEFAULT_WAIT_S = 20;

function checkId(id: string): void {
  if (!PET_ID_RE.test(id)) throw new UsageError(`"${id}" is not a pet id ([a-z0-9]{1,8})`);
}

export function petLine(pet: PetListEntry): string {
  return `${pet.id} "${pet.name}": rev ${pet.rev}, ${pet.size} bytes, crc32 ${pet.crc32}, ${pet.source === "catalog" ? "catalog" : "uploaded to this room"}${pet.builtin === true ? ", built in" : ""}`;
}

export function devicePetsLine(device: Device): string {
  const pets = device.pets;
  if (!pets) return `${device.id} "${device.name}": ${device.online ? "does not report pets" : "offline"}`;
  const installed = pets.installed.map((p) => (p.builtin ? `${p.id} (built-in)` : `${p.id} rev ${p.rev}`)).join(", ");
  return `${device.id} "${device.name}": shows ${pets.selected}; ${pets.free} of ${pets.slots} slots free; ${installed}`;
}

export async function listPets(options: ServerOption & { json?: boolean }, io: Io): Promise<number> {
  const { api } = await connectServer(options.server, io);
  const [pets, devices] = await Promise.all([api.pets(), api.devices()]);
  if (options.json === true) {
    printJson(io, { pets, devices: devices.filter((d) => d.pets).map((d) => ({ id: d.id, name: d.name, pets: d.pets })) });
    return 0;
  }
  for (const pet of pets) io.out(petLine(pet));
  const reporting = devices.filter((d) => d.online);
  if (reporting.length > 0) io.out("");
  for (const device of reporting) io.out(devicePetsLine(device));
  return 0;
}

export async function addPet(file: string, options: ServerOption, io: Io): Promise<number> {
  let data: Buffer;
  try {
    data = await readFile(file);
  } catch (err) {
    throw new CliError(`cannot read ${file}: ${(err as Error).message}`);
  }
  const json = file.endsWith(".json") || data.subarray(0, 1).toString() === "{";
  if (data.length > (json ? PET_SOURCE_MAX_BYTES : PET_MAX_BYTES)) {
    throw new CliError(`${file} is ${data.length} bytes; a ${json ? "JSON source" : "pack"} has at most ${json ? PET_SOURCE_MAX_BYTES : PET_MAX_BYTES}`);
  }
  let body: Uint8Array | object = new Uint8Array(data);
  if (json) {
    try {
      body = JSON.parse(data.toString("utf8")) as object;
    } catch (err) {
      throw new CliError(`${file} is not valid JSON: ${(err as Error).message}`);
    }
  }
  const { api } = await connectServer(options.server, io);
  let pet: PetListEntry;
  let created: boolean;
  try {
    ({ pet, created } = await api.uploadPet(body));
  } catch (err) {
    if (err instanceof ApiError && (err.status === 400 || err.status === 409 || err.status === 413)) throw new CliError(`${file}: ${err.reason}`);
    throw err;
  }
  const what = `${pet.id} "${pet.name}" rev ${pet.rev} (${pet.size} bytes)`;
  io.out(`${created ? `Added ${what} to this room` : `${what} is already in this room, unchanged`}. Send it with: devotchi pets send ${pet.id}`);
  return 0;
}

export async function removePet(id: string, options: ServerOption, io: Io): Promise<number> {
  checkId(id);
  const { api } = await connectServer(options.server, io);
  try {
    await api.deletePet(id);
  } catch (err) {
    if (err instanceof ApiError && err.status === 404) throw new CliError(`no uploaded pet ${id} in this room (see devotchi pets)`);
    if (err instanceof ApiError && err.status === 409) throw new CliError(`${id} ships with the server and cannot be removed`);
    throw err;
  }
  io.out(`Removed ${id} from this room. Devices that installed it keep their copy until you remove it there.`);
  return 0;
}

/** The --device given, or the only online device that reports pets. */
async function targetDevice(api: ApiClient, given: string | undefined): Promise<Device> {
  const devices = await api.devices();
  if (given !== undefined) {
    const device = devices.find((d) => d.id === given);
    if (!device) throw new CliError(`no device ${given} in this room (see devotchi devices)`);
    return device;
  }
  const candidates = devices.filter((d) => d.online && d.pets);
  if (candidates.length === 1) return candidates[0];
  if (candidates.length === 0) throw new CliError("no online device reports pets; pass --device <id> (see devotchi devices)");
  throw new CliError(`several devices are online (${candidates.map((d) => d.id).join(", ")}); pass --device <id>`);
}

async function deviceError(err: unknown, device: Device, id: string): Promise<never> {
  if (err instanceof ApiError && err.status === 409) {
    if (err.reason === "built-in pet") throw new CliError(`${id} is built into ${device.id}; use it with: devotchi pets use ${id}`);
    throw new CliError(`${device.id}: ${err.reason}`);
  }
  if (err instanceof ApiError && err.status === 404) throw new CliError(`${err.reason} (see devotchi pets and devotchi devices)`);
  throw err;
}

/**
 * The device's `pets.result` for the command sent at `sentAt` (protocol 14.8):
 * same op and pet, arrived at or after the send. A server that does not
 * report results (no `at` in the POST answer, no `petsResult`) gives none.
 */
function answerTo(device: Device, op: string, pet: string, sentAt: string | undefined): DevicePetsResult | undefined {
  const result = device.petsResult;
  if (sentAt === undefined || result === undefined || result.op !== op || result.pet !== pet) return undefined;
  return Date.parse(result.at) >= Date.parse(sentAt) ? result : undefined;
}

/**
 * Polls GET /api/devices until the device answers the command, or (for a
 * server without results) until `done` holds, or the wait is over. A failed
 * answer is a CliError at once.
 */
async function waitFor(
  api: ApiClient,
  deviceId: string,
  seconds: number,
  command: { op: string; pet: string; sentAt: string | undefined },
  done: (device: Device) => boolean,
): Promise<boolean> {
  const until = Date.now() + seconds * 1000;
  while (Date.now() < until) {
    const device = (await api.devices()).find((d) => d.id === deviceId);
    const answer = device && answerTo(device, command.op, command.pet, command.sentAt);
    if (answer) {
      if (answer.ok) return true;
      throw new CliError(`${deviceId}: ${petResultText(answer)}`);
    }
    if (device && command.sentAt === undefined && done(device)) return true;
    await new Promise((resolve) => setTimeout(resolve, POLL_MS));
  }
  return false;
}

export async function sendPet(id: string, options: PetSendOptions, io: Io): Promise<number> {
  checkId(id);
  const { api } = await connectServer(options.server, io);
  const device = await targetDevice(api, options.device);
  const pet = (await api.pets()).find((p) => p.id === id);
  if (!pet) throw new CliError(`no pet ${id} in the catalog or this room (see devotchi pets)`);
  const sent = await api.devicePet(device.id, { op: "install", pet: id, select: options.use === true }).catch((err: unknown) => deviceError(err, device, id));
  const wait = options.wait ?? DEFAULT_WAIT_S;
  if (wait === 0) {
    io.out(`Sent ${id} to ${device.id}.`);
    return 0;
  }
  const ok = await waitFor(api, device.id, wait, { op: "install", pet: id, sentAt: sent.at }, (d) => {
    const installed = d.pets?.installed.some((p) => p.id === id && p.rev === pet.rev) === true;
    return installed && (options.use !== true || d.pets?.selected === id);
  });
  if (!ok) {
    throw new CliError(`${device.id} did not report ${id} rev ${pet.rev} within ${wait} s; the dashboard's Pets page shows the device's answer`);
  }
  io.out(`${device.id} installed ${id} rev ${pet.rev}${options.use === true ? " and shows it" : ""}.`);
  return 0;
}

export async function usePet(id: string, options: PetSendOptions, io: Io): Promise<number> {
  checkId(id);
  const { api } = await connectServer(options.server, io);
  const device = await targetDevice(api, options.device);
  const sent = await api.devicePet(device.id, { op: "select", pet: id }).catch((err: unknown) => deviceError(err, device, id));
  const wait = options.wait ?? DEFAULT_WAIT_S;
  if (wait > 0 && !(await waitFor(api, device.id, wait, { op: "select", pet: id, sentAt: sent.at }, (d) => d.pets?.selected === id))) {
    throw new CliError(`${device.id} did not switch to ${id} within ${wait} s; is it installed there? (devotchi pets)`);
  }
  io.out(wait > 0 ? `${device.id} shows ${id}.` : `Asked ${device.id} to show ${id}.`);
  return 0;
}
