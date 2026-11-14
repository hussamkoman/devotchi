// `devotchi mode`, `devotchi hold` and `devotchi notify`.

import { HOLD_MAX_MS, HOLD_MIN_MS, MOODS, NOTIFY_EVENT_RE, isMood, type NotifyRequest, type RoomMode } from "@devotchi/shared";
import { connectServer, printJson, type Io } from "../context.js";
import { UsageError } from "../errors.js";

interface ServerOption {
  server?: string;
  json?: boolean;
}

const MODE_TEXT: Record<RoomMode, string> = {
  ask: "permission requests go to your devices",
  desk: "you are at the keyboard; requests go straight to the terminal",
};

export async function runMode(mode: string | undefined, options: ServerOption, io: Io): Promise<number> {
  if (mode !== undefined && mode !== "ask" && mode !== "desk") throw new UsageError(`mode must be ask or desk, not "${mode}"`);
  const { api } = await connectServer(options.server, io);
  const settings = mode === undefined ? (await api.room()).settings : await api.updateRoomSettings({ mode });
  if (options.json === true) printJson(io, { mode: settings.mode });
  else io.out(`Mode: ${settings.mode} (${MODE_TEXT[settings.mode]})`);
  return 0;
}

export function parseHoldSeconds(value: string): number {
  const min = HOLD_MIN_MS / 1000;
  const max = HOLD_MAX_MS / 1000;
  if (!/^\d+$/.test(value) || Number(value) < min || Number(value) > max) {
    throw new UsageError(`hold time must be a whole number of seconds from ${min} to ${max}`);
  }
  return Number(value);
}

export async function runHold(seconds: string | undefined, options: ServerOption, io: Io): Promise<number> {
  const holdMs = seconds === undefined ? undefined : parseHoldSeconds(seconds) * 1000;
  const { api } = await connectServer(options.server, io);
  const settings = holdMs === undefined ? (await api.room()).settings : await api.updateRoomSettings({ holdMs });
  if (options.json === true) printJson(io, { holdMs: settings.holdMs });
  else {
    io.out(
      `Hold time: ${settings.holdMs / 1000} s (how long a request waits for an answer on a device before the terminal prompt takes over)`,
    );
  }
  return 0;
}

export interface NotifyOptions extends ServerOption {
  mood?: string;
  urgent?: boolean;
  urgency?: number;
  source?: string;
}

/** --urgent means toast + vibration (urgency 2); --urgency sets the level explicitly. */
export function notifyBody(event: string, detail: string | undefined, options: NotifyOptions): NotifyRequest {
  if (!NOTIFY_EVENT_RE.test(event) || Buffer.byteLength(event) > 40) {
    throw new UsageError(`event "${event}" must be lower-case letters, digits and underscores (max 40)`);
  }
  if (options.mood !== undefined && !isMood(options.mood)) {
    throw new UsageError(`mood must be one of ${MOODS.join(", ")}`);
  }
  if (options.urgent === true && options.urgency !== undefined) throw new UsageError("use either --urgent or --urgency");
  const body: NotifyRequest = { event };
  if (detail !== undefined) body.detail = detail;
  if (options.mood !== undefined) body.mood = options.mood as NotifyRequest["mood"];
  if (options.source !== undefined) body.source = options.source;
  const urgency = options.urgent === true ? 2 : options.urgency;
  if (urgency !== undefined) body.urgency = urgency;
  return body;
}

export async function runNotify(event: string, detail: string | undefined, options: NotifyOptions, io: Io): Promise<number> {
  const body = notifyBody(event, detail, options);
  const { api } = await connectServer(options.server, io);
  const result = await api.notify(body);
  if (options.json === true) printJson(io, result);
  else io.out(`Sent ${event} to ${result.delivered} consumer${result.delivered === 1 ? "" : "s"}.`);
  return 0;
}
