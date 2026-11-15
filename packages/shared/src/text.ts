// Byte-oriented text helpers. Every protocol limit is in UTF-8 bytes
// (docs/protocol.md section 2), and the device fonts are ASCII only.

const REPLACEMENT = "�";

function isHighSurrogate(unit: number): boolean {
  return unit >= 0xd800 && unit <= 0xdbff;
}

function isLowSurrogate(unit: number): boolean {
  return unit >= 0xdc00 && unit <= 0xdfff;
}

/**
 * Number of bytes `str` occupies in UTF-8. A lone surrogate counts as the
 * 3-byte U+FFFD it is encoded as, so the result equals
 * `new TextEncoder().encode(str).length`.
 */
export function utf8ByteLength(str: string): number {
  let bytes = 0;
  for (let i = 0; i < str.length; i++) {
    const unit = str.charCodeAt(i);
    if (unit < 0x80) bytes += 1;
    else if (unit < 0x800) bytes += 2;
    else if (isHighSurrogate(unit) && i + 1 < str.length && isLowSurrogate(str.charCodeAt(i + 1))) {
      bytes += 4;
      i++;
    } else bytes += 3;
  }
  return bytes;
}

/**
 * Cuts `str` to at most `maxBytes` UTF-8 bytes on a code point boundary,
 * without an ellipsis. Lone surrogates are replaced by U+FFFD so the result
 * is well-formed: JSON.stringify would otherwise emit them as `\udXXX`
 * escapes that strict parsers such as cJSON reject.
 */
export function utf8Truncate(str: string, maxBytes: number): string {
  const limit = Math.max(0, Math.floor(maxBytes));
  let bytes = 0;
  let out = "";
  let wellFormedUpTo = 0;
  for (let i = 0; i < str.length; i++) {
    const unit = str.charCodeAt(i);
    let size: number;
    let units = 1;
    let lone = false;
    if (unit < 0x80) size = 1;
    else if (unit < 0x800) size = 2;
    else if (isHighSurrogate(unit) && i + 1 < str.length && isLowSurrogate(str.charCodeAt(i + 1))) {
      size = 4;
      units = 2;
    } else {
      size = 3;
      lone = isHighSurrogate(unit) || isLowSurrogate(unit);
    }
    if (bytes + size > limit) {
      return out + str.slice(wellFormedUpTo, i);
    }
    bytes += size;
    if (lone) {
      out += str.slice(wellFormedUpTo, i) + REPLACEMENT;
      wellFormedUpTo = i + 1;
    }
    i += units - 1;
  }
  return wellFormedUpTo === 0 ? str : out + str.slice(wellFormedUpTo);
}

/**
 * Restricts `str` to printable ASCII (0x20..0x7E) plus `\n`. Every other code
 * point, including a surrogate pair or a lone surrogate, becomes one `?`.
 */
export function sanitizeAscii(str: string): string {
  let out = "";
  for (let i = 0; i < str.length; i++) {
    const unit = str.charCodeAt(i);
    if ((unit >= 0x20 && unit <= 0x7e) || unit === 0x0a) {
      out += str[i];
      continue;
    }
    if (isHighSurrogate(unit) && i + 1 < str.length && isLowSurrogate(str.charCodeAt(i + 1))) i++;
    out += "?";
  }
  return out;
}

// --- Device charset (protocol 14.1) ---

const DEVICE_EXTRA = new Set([..."–—‘’“”•…←↑→↓✓×−"].map((c) => c.codePointAt(0)!));
const COMBINING_MARK = /\p{M}/gu;

/** True for a code point the device fonts can draw. */
export function isDeviceChar(codePoint: number): boolean {
  return (
    (codePoint >= 0x20 && codePoint <= 0x7e) || (codePoint >= 0xa0 && codePoint <= 0xff) || DEVICE_EXTRA.has(codePoint)
  );
}

export interface DeviceText {
  text: string;
  /** Characters that became `?`. */
  replaced: number;
  /** Code points of the input, line breaks and tabs included. */
  chars: number;
}

/**
 * Maps `str` to the device charset of protocol 14.1: line feed, carriage
 * return and tab become one space each (`keepNewlines` keeps `\n` for
 * `permission.cmd`), a character outside the charset is decomposed (NFKD)
 * without its combining marks, and whatever is still outside becomes `?`.
 * Only the `?` replacements are counted in `replaced`.
 */
export function toDeviceText(str: string, options: { keepNewlines?: boolean } = {}): DeviceText {
  let text = "";
  let replaced = 0;
  let chars = 0;
  // NFC first so that a base letter followed by a combining accent stays one drawable letter.
  for (const ch of str.normalize("NFC")) {
    chars++;
    const cp = ch.codePointAt(0)!;
    if (cp === 0x0a && options.keepNewlines) text += "\n";
    else if (cp === 0x0a || cp === 0x0d || cp === 0x09) text += " ";
    else if (isDeviceChar(cp)) text += ch;
    else {
      for (const part of ch.normalize("NFKD").replace(COMBINING_MARK, "")) {
        if (isDeviceChar(part.codePointAt(0)!)) text += part;
        else {
          text += "?";
          replaced++;
        }
      }
    }
  }
  return { text, replaced, chars };
}

/** Cuts `str` to at most `maxBytes` UTF-8 bytes; a shortened result ends in "…" (3 bytes). */
export function shortenWithEllipsis(str: string, maxBytes: number): string {
  if (utf8ByteLength(str) <= maxBytes) return str;
  if (maxBytes < 3) return "";
  return utf8Truncate(str, maxBytes - 3).trimEnd() + "…";
}
