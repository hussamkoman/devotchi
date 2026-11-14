// Validation of JSON request bodies (protocol section 7: wrong types are 400).

import { LIMITS, utf8ByteLength, utf8Truncate } from "@devotchi/shared";
import { badRequest } from "./errors.js";

export type Fields = Record<string, unknown>;

/** The body as an object; an absent body counts as `{}`. */
export function objectBody(body: unknown, allowEmpty = false): Fields {
  if (body === undefined && allowEmpty) return {};
  if (typeof body !== "object" || body === null || Array.isArray(body)) throw badRequest("Body must be a JSON object");
  return body as Fields;
}

export function optionalString(fields: Fields, key: string): string | undefined {
  const value = fields[key];
  if (value === undefined) return undefined;
  if (typeof value !== "string") throw badRequest(`${key} must be a string`);
  return value;
}

export function requiredString(fields: Fields, key: string): string {
  const value = fields[key];
  if (typeof value !== "string") throw badRequest(`${key} must be a string`);
  return value;
}

/** Device metadata (`model`, `fw`): over-long strings are cut, as everywhere in the protocol. */
export function optionalMeta(fields: Fields, key: string): string | undefined {
  const value = optionalString(fields, key);
  return value === undefined ? undefined : utf8Truncate(value, LIMITS.name);
}

/** Names are shown on the device; control characters would break its layout. */
function hasControlCharacter(value: string): boolean {
  for (let i = 0; i < value.length; i++) {
    const unit = value.charCodeAt(i);
    if (unit < 0x20 || unit === 0x7f) return true;
  }
  return false;
}

/** A device name of 1..24 bytes (protocol 7.5). */
export function parseDeviceName(value: unknown): string {
  if (typeof value !== "string") throw badRequest("name must be a string");
  const bytes = utf8ByteLength(value);
  if (bytes < 1 || bytes > LIMITS.name || value.trim() === "" || hasControlCharacter(value)) {
    throw badRequest(`name must be 1..${LIMITS.name} bytes of printable text`);
  }
  return value;
}
