// `devotchi devices` and `devotchi pair`.

import { formatPairingCode, normalizePairingCode } from "@devotchi/shared";
import { ApiError } from "../api.js";
import { connectServer, printJson, type Io } from "../context.js";
import { CliError, UsageError } from "../errors.js";
import { deviceLine, deviceLiveness } from "../format.js";
import { adoptTimeZone } from "../timezone.js";

interface ServerOption {
  server?: string;
}

function notFound(err: unknown, id: string): never {
  if (err instanceof ApiError && err.status === 404) throw new CliError(`no device ${id} in this room (see devotchi devices)`);
  throw err;
}

export async function listDevices(options: ServerOption & { json?: boolean }, io: Io): Promise<number> {
  const { api } = await connectServer(options.server, io);
  const devices = await api.devices();
  const now = Date.now();
  if (options.json === true) {
    printJson(io, { devices: devices.map((device) => ({ ...device, liveness: deviceLiveness(device, now) })) });
    return 0;
  }
  if (devices.length === 0) {
    io.out("No devices paired. Pair one with `devotchi pair <CODE>` (the code is on the device screen).");
    return 0;
  }
  for (const device of devices) io.out(deviceLine(device, now));
  return 0;
}

export async function renameDevice(id: string, name: string, options: ServerOption, io: Io): Promise<number> {
  const { api } = await connectServer(options.server, io);
  const device = await api.renameDevice(id, name).catch((err: unknown) => notFound(err, id));
  io.out(`Renamed ${device.id} to "${device.name}".`);
  return 0;
}

export async function identifyDevice(id: string, options: ServerOption & { duration?: number }, io: Io): Promise<number> {
  const { api } = await connectServer(options.server, io);
  try {
    await api.identifyDevice(id, options.duration);
  } catch (err) {
    if (err instanceof ApiError && err.status === 409) throw new CliError(`${id} is offline; it cannot identify itself`);
    notFound(err, id);
  }
  io.out(`${id} is showing its name.`);
  return 0;
}

export async function removeDevice(id: string, options: ServerOption & { yes?: boolean }, io: Io): Promise<number> {
  if (options.yes !== true) {
    throw new UsageError(`removing ${id} revokes its token and it has to be paired again; repeat with --yes to confirm`);
  }
  const { api } = await connectServer(options.server, io);
  await api.removeDevice(id).catch((err: unknown) => notFound(err, id));
  io.out(`Removed ${id}; its token is revoked and the device returns to pairing.`);
  return 0;
}

export interface PairOptions extends ServerOption {
  new?: boolean;
  name?: string;
  json?: boolean;
}

export async function pair(code: string | undefined, options: PairOptions, io: Io): Promise<number> {
  if ((code === undefined) === (options.new !== true)) throw new UsageError("give either a code or --new");
  if (options.new === true && options.name !== undefined) throw new UsageError("--name only applies when confirming a code");
  let normalised: string | null = null;
  if (code !== undefined) {
    normalised = normalizePairingCode(code);
    if (normalised === null) throw new UsageError(`"${code}" is not a pairing code (6 letters and digits, e.g. K7M2QX)`);
  }
  const { api } = await connectServer(options.server, io);
  const zone = await adoptTimeZone(api).catch(() => null);
  if (zone !== null && options.json !== true) io.out(`Room time zone: ${zone} (from this PC)`);
  if (normalised === null) {
    const created = await api.createPairCode();
    if (options.json === true) printJson(io, created);
    else {
      io.out(`Pairing code: ${formatPairingCode(created.code)} (valid ${created.expiresInS} s, until ${created.expiresAt})`);
      io.out(`On the device console: pair ${created.code}`);
    }
    return 0;
  }
  try {
    const paired = await api.confirmPairing(normalised, options.name);
    if (options.json === true) printJson(io, paired);
    else io.out(`Paired ${paired.deviceId} as "${paired.name}".`);
    return 0;
  } catch (err) {
    if (!(err instanceof ApiError)) throw err;
    if (err.status === 404) throw new CliError(`no device is showing code ${formatPairingCode(normalised)}; check the screen`);
    if (err.status === 410) {
      throw new CliError(`code ${formatPairingCode(normalised)} has expired or was already used; the device shows a new one`);
    }
    if (err.status === 429) {
      throw new CliError(`too many pairing attempts; wait ${err.retryAfterS ?? 60} s and try again`);
    }
    throw err;
  }
}
