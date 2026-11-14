import { describe, expect, it } from "vitest";
import {
  PAIRING_CODE_ALPHABET,
  PAIRING_CODE_LENGTH,
  formatPairingCode,
  generatePairingCode,
  isValidPairingCode,
  normalizePairingCode,
} from "../src/pairing.js";

describe("pairing code alphabet", () => {
  it("has 31 unique symbols without look-alikes", () => {
    expect(PAIRING_CODE_ALPHABET).toHaveLength(31);
    expect(new Set(PAIRING_CODE_ALPHABET).size).toBe(31);
    for (const confusable of ["0", "O", "I", "L", "1"]) expect(PAIRING_CODE_ALPHABET).not.toContain(confusable);
  });
});

describe("generatePairingCode", () => {
  it("produces valid 6-symbol codes", () => {
    for (let i = 0; i < 500; i++) {
      const code = generatePairingCode();
      expect(code).toHaveLength(PAIRING_CODE_LENGTH);
      expect(isValidPairingCode(code)).toBe(true);
    }
  });

  it("uses every symbol with a roughly uniform distribution", () => {
    const counts = new Map<string, number>();
    const codes = 6000;
    for (let i = 0; i < codes; i++) {
      for (const ch of generatePairingCode()) counts.set(ch, (counts.get(ch) ?? 0) + 1);
    }
    expect(counts.size).toBe(31);
    const expected = (codes * PAIRING_CODE_LENGTH) / 31;
    // About 8 standard deviations; a biased generator (e.g. byte % 31 without
    // rejection) is still caught by the chi-square bound below.
    for (const count of counts.values()) {
      expect(count).toBeGreaterThan(expected * 0.75);
      expect(count).toBeLessThan(expected * 1.25);
    }
    let chiSquare = 0;
    for (const count of counts.values()) chiSquare += (count - expected) ** 2 / expected;
    // 30 degrees of freedom: p < 1e-6 above 80.
    expect(chiSquare).toBeLessThan(80);
  });

  it("uses the CSPRNG, not Math.random", () => {
    const original = Math.random;
    Math.random = () => {
      throw new Error("Math.random must not be used");
    };
    try {
      expect(isValidPairingCode(generatePairingCode())).toBe(true);
    } finally {
      Math.random = original;
    }
  });
});

describe("isValidPairingCode", () => {
  it.each(["K7M2QX", "ABCDEF", "234567"])("accepts %s", (code) => expect(isValidPairingCode(code)).toBe(true));
  it.each(["k7m2qx", "K7M 2QX", "K7M2Q", "K7M2QXA", "K0M2QX", "KOM2QX", "KIM2QX", "KLM2QX", "K1M2QX", "", 123456, null])(
    "rejects %j",
    (code) => expect(isValidPairingCode(code)).toBe(false),
  );
});

describe("normalizePairingCode", () => {
  it.each([
    ["K7M2QX", "K7M2QX"],
    ["k7m2qx", "K7M2QX"],
    ["K7M 2QX", "K7M2QX"],
    [" k7m-2qx\n", "K7M2QX"],
  ])("%j -> %s", (input, out) => expect(normalizePairingCode(input)).toBe(out));

  it.each(["K7M2Q", "K7M2QO", "", "K7M_2QX", 42, undefined])("rejects %j", (input) =>
    expect(normalizePairingCode(input)).toBeNull(),
  );
});

describe("formatPairingCode", () => {
  it("splits into two groups", () => {
    expect(formatPairingCode("K7M2QX")).toBe("K7M 2QX");
  });
});
