import { describe, expect, it } from "vitest";
import { sanitizeAscii, utf8ByteLength, utf8Truncate } from "../src/text.js";

const encoder = new TextEncoder();

describe("utf8ByteLength", () => {
  it.each([
    ["", 0],
    ["abc", 3],
    ["é", 2],
    ["€", 3],
    ["日本", 6],
    ["😀", 4],
    ["a😀b", 6],
    ["\ud800", 3],
    ["\udc00x", 4],
    ["\ud83d\ud83d", 6],
  ])("%j is %i bytes", (text, bytes) => {
    expect(utf8ByteLength(text)).toBe(bytes);
    expect(utf8ByteLength(text)).toBe(encoder.encode(text).length);
  });
});

describe("utf8Truncate", () => {
  it("returns short strings unchanged", () => {
    expect(utf8Truncate("hello", 5)).toBe("hello");
    expect(utf8Truncate("hello", 100)).toBe("hello");
  });

  it("cuts ASCII exactly and appends nothing", () => {
    expect(utf8Truncate("hello world", 5)).toBe("hello");
    expect(utf8Truncate("hello", 0)).toBe("");
    expect(utf8Truncate("hello", -3)).toBe("");
  });

  it("never splits a multi-byte character", () => {
    expect(utf8Truncate("aé", 2)).toBe("a");
    expect(utf8Truncate("aé", 3)).toBe("aé");
    expect(utf8Truncate("日本語", 5)).toBe("日");
    expect(utf8Truncate("日本語", 6)).toBe("日本");
  });

  it("never splits a surrogate pair (emoji)", () => {
    expect(utf8Truncate("😀😀", 4)).toBe("😀");
    expect(utf8Truncate("😀😀", 7)).toBe("😀");
    expect(utf8Truncate("a😀", 4)).toBe("a");
    expect(utf8Truncate("👍🏽", 4)).toBe("👍");
  });

  it("replaces lone surrogates so the result is well-formed", () => {
    expect(utf8Truncate("a\ud800b", 10)).toBe("a�b");
    expect(utf8Truncate("\udc00", 10)).toBe("�");
    expect(utf8Truncate("x\ud83d", 3)).toBe("x");
    expect(utf8Truncate("x\ud83d", 4)).toBe("x�");
    expect(JSON.stringify(utf8Truncate("\ud800\ud800", 100))).toBe('"��"');
  });

  it("keeps every result within the byte limit for every cut point", () => {
    const sample = "aé日😀\ud800b€\udc00z";
    for (let limit = 0; limit <= 30; limit++) {
      const out = utf8Truncate(sample, limit);
      expect(encoder.encode(out).length).toBeLessThanOrEqual(limit);
      expect(new TextDecoder().decode(encoder.encode(out))).toBe(out);
    }
  });
});

describe("sanitizeAscii", () => {
  it("keeps printable ASCII and newline", () => {
    const printable = Array.from({ length: 0x7f - 0x20 }, (_, i) => String.fromCharCode(0x20 + i)).join("");
    expect(sanitizeAscii(printable)).toBe(printable);
    expect(sanitizeAscii("a\nb")).toBe("a\nb");
  });

  it("replaces control characters, DEL and non-ASCII with one ? per code point", () => {
    expect(sanitizeAscii("a\tb\rc\x00d\x7f")).toBe("a?b?c?d?");
    expect(sanitizeAscii("héllo")).toBe("h?llo");
    expect(sanitizeAscii("日本")).toBe("??");
    expect(sanitizeAscii("😀")).toBe("?");
    expect(sanitizeAscii("👍🏽")).toBe("??");
    expect(sanitizeAscii("\ud800x\udc00")).toBe("?x?");
  });
});
