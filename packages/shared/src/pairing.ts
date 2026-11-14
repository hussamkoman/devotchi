// Pairing codes (protocol section 9.1): 6 symbols from a 31-symbol alphabet
// without the look-alikes 0 O I L 1, shown as two groups of three.

export const PAIRING_CODE_ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
export const PAIRING_CODE_LENGTH = 6;

const CODE_RE = new RegExp(`^[${PAIRING_CODE_ALPHABET}]{${PAIRING_CODE_LENGTH}}$`);

// The largest multiple of the alphabet size that fits in a byte; bytes at or
// above it are rejected so every symbol is equally likely.
const UNBIASED_LIMIT = 256 - (256 % PAIRING_CODE_ALPHABET.length);

/** A uniformly random pairing code from the platform CSPRNG. */
export function generatePairingCode(): string {
  let code = "";
  const bytes = new Uint8Array(PAIRING_CODE_LENGTH * 2);
  while (code.length < PAIRING_CODE_LENGTH) {
    globalThis.crypto.getRandomValues(bytes);
    for (const byte of bytes) {
      if (byte >= UNBIASED_LIMIT) continue;
      code += PAIRING_CODE_ALPHABET[byte % PAIRING_CODE_ALPHABET.length];
      if (code.length === PAIRING_CODE_LENGTH) break;
    }
  }
  return code;
}

/** True for a code in canonical form (upper case, no separators). */
export function isValidPairingCode(code: unknown): code is string {
  return typeof code === "string" && CODE_RE.test(code);
}

/**
 * Canonical form of user input ("k7m 2qx", "K7M-2QX" -> "K7M2QX"), or null
 * when the result is not a valid code.
 */
export function normalizePairingCode(input: unknown): string | null {
  if (typeof input !== "string") return null;
  const code = input.replace(/[\s-]/g, "").toUpperCase();
  return isValidPairingCode(code) ? code : null;
}

/** Display form with a space between the two groups: "K7M 2QX". */
export function formatPairingCode(code: string): string {
  const half = PAIRING_CODE_LENGTH / 2;
  return `${code.slice(0, half)} ${code.slice(half)}`;
}
