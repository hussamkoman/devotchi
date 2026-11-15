// Local time for devices (protocol 14.9): IANA zone validation and the
// offset of a zone at an instant.

import { UTC_OFFSET_MAX, UTC_OFFSET_MIN } from "./protocol.js";

/** Longest zone name accepted in the room setting. */
export const TIME_ZONE_MAX_BYTES = 64;

/** The zone of this process. */
export function processTimeZone(): string {
  return new Intl.DateTimeFormat().resolvedOptions().timeZone;
}

/**
 * The zone name to store for `zone`, or null when the runtime does not know
 * it. The case is fixed (`europe/berlin` -> `Europe/Berlin`), but a valid
 * alias keeps its own name: ICU resolves `Asia/Kolkata` to the legacy
 * `Asia/Calcutta`, which is not what the person chose.
 */
export function canonicalTimeZone(zone: string): string | null {
  if (zone === "" || zone.length > TIME_ZONE_MAX_BYTES || !/^[A-Za-z0-9_+\-/]+$/.test(zone)) return null;
  try {
    const resolved = new Intl.DateTimeFormat("en-US", { timeZone: zone }).resolvedOptions().timeZone;
    return resolved.toLowerCase() === zone.toLowerCase() ? resolved : zone;
  } catch {
    return null;
  }
}

/**
 * Minutes east of UTC in `zone` at `at` (ms since the epoch), from the
 * zone's wall clock; `""` is the process zone. Clamped to -720..840.
 */
export function utcOffsetMinutes(zone: string, at: number): number {
  let timeZone = zone === "" ? processTimeZone() : zone;
  // A stored zone the runtime no longer knows falls back to the process zone.
  if (canonicalTimeZone(timeZone) === null) timeZone = processTimeZone();
  const format = new Intl.DateTimeFormat("en-US", {
    timeZone,
    hourCycle: "h23",
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
  const parts: Record<string, number> = {};
  for (const part of format.formatToParts(at)) if (part.type !== "literal") parts[part.type] = Number(part.value);
  const wall = Date.UTC(parts.year!, parts.month! - 1, parts.day!, parts.hour!, parts.minute!, parts.second!);
  const offset = Math.round((wall - Math.floor(at / 1000) * 1000) / 60_000);
  return Math.min(UTC_OFFSET_MAX, Math.max(UTC_OFFSET_MIN, offset));
}
