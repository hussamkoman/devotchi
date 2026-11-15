// Protocol 14 additions in shared: validators and builders of the new
// messages, the card fields of formatToolCall, frame limits and local time.

import { describe, expect, it } from "vitest";
import { deriveGrant } from "../src/grant.js";
import { CAPS, DECIDE_CHOICES, LIMITS, MAX_FRAME_BYTES } from "../src/protocol.js";
import { buildPlanFrame, buildQuestionFrames, questionSpecs, summarisePlan } from "../src/requests.js";
import { utf8ByteLength } from "../src/text.js";
import { canonicalTimeZone, utcOffsetMinutes } from "../src/time.js";
import { RISK_RULES, formatToolCall } from "../src/tool-format.js";
import {
  buildDeadline,
  buildDecide,
  buildDismiss,
  buildPermission,
  buildRegister,
  buildRegisterAck,
  buildTelemetry,
  buildTime,
  parseInbound,
  serialize,
  type InboundContext,
} from "../src/validate.js";

const ID = "9f2c4e6a8b0d1f3a5c7e9b1d3f5a7c9e";
const consumer: InboundContext = { role: "consumer", tokenKind: "device" };
const CWD = "/home/u/proj";

function parse(msg: unknown, ctx: InboundContext = consumer) {
  return parseInbound(JSON.stringify(msg), ctx);
}

/** Deterministic pseudo-random numbers (mulberry32), so a failing case can be rerun. */
function rng(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const ALPHABET = ['"', "\\", "a", " ", "\n", "\t", "é", "日", "😀", "x", "-", "/", "*", ":", "\u0000", "…"];

function randomText(next: () => number, maxLength: number): string {
  const length = Math.floor(next() * maxLength);
  let out = "";
  for (let i = 0; i < length; i++) out += ALPHABET[Math.floor(next() * ALPHABET.length)];
  return out;
}

describe("register caps", () => {
  const register = (caps: unknown) =>
    parse({ type: "register", role: "consumer", proto: 2, client: { kind: "sim", deviceId: "sim-a", caps } }, {
      role: "unregistered",
      tokenKind: "device",
    });

  it("keeps known caps once and drops unknown entries", () => {
    const result = register(["decide", "ask", "future", "ask", "plan"]);
    expect(result.ok && result.msg.type === "register" && result.msg.client.caps).toEqual(["decide", "ask", "plan"]);
  });

  it("rejects more than 8 entries, entries over 16 bytes and non-strings", () => {
    expect(register(Array(9).fill("ask")).ok).toBe(false);
    expect(register(["a".repeat(17)]).ok).toBe(false);
    expect(register([1]).ok).toBe(false);
    expect(register("ask").ok).toBe(false);
  });

  it("a register without caps is a phase-1 consumer", () => {
    const result = register(undefined);
    expect(result.ok && result.msg.type === "register" && "caps" in result.msg.client).toBe(false);
  });

  it("buildRegister and buildRegisterAck carry caps and the UTC offset", () => {
    const reg = buildRegister({ role: "consumer", client: { kind: "sim", deviceId: "sim-a", caps: ["plan", "plan", "ask"] } });
    expect(reg.client.caps).toEqual(["plan", "ask"]);
    const ack = buildRegisterAck({
      role: "consumer",
      roomId: "0123456789ab",
      serverVersion: "0.1.0",
      serverTime: 1,
      mode: "ask",
      peers: { producers: 0, consumers: 1, devices: 0 },
      caps: [...CAPS],
      utcOffsetMin: 900,
    } as Parameters<typeof buildRegisterAck>[0]);
    expect(ack).toMatchObject({ caps: [...CAPS], utcOffsetMin: 840 });
  });
});

describe("decide, question_response, extend", () => {
  it("accepts every decide choice and rejects any other with the id", () => {
    for (const choice of DECIDE_CHOICES) {
      expect(parse({ type: "decide", id: ID, choice })).toEqual({ ok: true, msg: { type: "decide", id: ID, choice } });
    }
    expect(parse({ type: "decide", id: ID, choice: "allow" })).toMatchObject({ ok: false, code: "INVALID_MESSAGE", id: ID });
  });

  it("question_response keeps only the id and the indexes", () => {
    const result = parse({ type: "question_response", id: ID, answers: [[0], null, [1, 3]], labels: ["evil"], text: "x" });
    expect(result).toEqual({ ok: true, msg: { type: "question_response", id: ID, answers: [[0], null, [1, 3]] } });
  });

  it.each([
    ["no answers", undefined],
    ["empty", []],
    ["five entries", [[0], [0], [0], [0], [0]]],
    ["a string label", [["React"]]],
    ["a negative index", [[-1]]],
    ["an index past 3", [[4]]],
    ["a fraction", [[0.5]]],
    ["an object", [{ 0: true }]],
    ["a bare number", [0]],
  ])("question_response rejects %s with the id", (_name, answers) => {
    expect(parse({ type: "question_response", id: ID, answers })).toMatchObject({ ok: false, code: "INVALID_MESSAGE", id: ID });
  });

  it("extend needs a request id", () => {
    expect(parse({ type: "extend", id: ID })).toEqual({ ok: true, msg: { type: "extend", id: ID } });
    expect(parse({ type: "extend", id: "x" }).ok).toBe(false);
  });

  it("producers may not send the consumer messages", () => {
    const producer: InboundContext = { role: "producer", tokenKind: "cli" };
    expect(parse({ type: "decide", id: ID, choice: "revise" }, producer).ok).toBe(false);
    expect(parse({ type: "extend", id: ID }, producer).ok).toBe(false);
  });
});

describe("telemetry.pet", () => {
  const sample = {
    type: "telemetry",
    battery: { state: "absent", pct: null, mv: 4100 },
    power: "usb",
    rssi: -58,
    uptimeS: 10,
    heapFree: 148392,
    heapMin: 101224,
    resetReason: "poweron",
    fw: "0.2.0",
  };
  const fields = { ...sample, type: undefined } as unknown as Parameters<typeof buildTelemetry>[0];

  it("keeps a valid pet id", () => {
    const result = parse({ ...sample, pet: "devo01" });
    expect(result.ok && result.msg.type === "telemetry" && result.msg.pet).toBe("devo01");
  });

  it.each([["Devo"], ["toolongid"], [""], [7], ["dé"]])("drops %j without rejecting the sample", (pet) => {
    const result = parse({ ...sample, pet });
    expect(result.ok).toBe(true);
    expect(result.ok && "pet" in result.msg).toBe(false);
  });

  it("buildTelemetry drops a malformed id too", () => {
    expect(buildTelemetry({ ...fields, pet: "ok" }).pet).toBe("ok");
    expect("pet" in buildTelemetry({ ...fields, pet: "NO!" })).toBe(false);
  });
});

describe("server message builders", () => {
  it("dismiss carries the choice", () => {
    expect(buildDismiss({ id: ID, reason: "answered", outcome: "deny", by: "sim-a", choice: "deny_stop" })).toEqual({
      type: "dismiss",
      id: ID,
      reason: "answered",
      outcome: "deny",
      by: "sim-a",
      choice: "deny_stop",
    });
    expect("choice" in buildDismiss({ id: ID, reason: "answered", outcome: "allow", by: "x" })).toBe(false);
  });

  it("decide, deadline and time", () => {
    expect(buildDecide(ID, "approve")).toEqual({ type: "decide", id: ID, choice: "approve" });
    expect(() => buildDecide(ID, "nope" as never)).toThrow();
    expect(buildDeadline(ID, 59_999.6)).toEqual({ type: "deadline", id: ID, timeout: 60_000 });
    expect(buildTime(1794668400000, 60)).toEqual({ type: "time", serverTime: 1794668400000, utcOffsetMin: 60 });
    expect(buildTime(0, -800).utcOffsetMin).toBe(-720);
  });
});

describe("formatToolCall card fields", () => {
  it("every risk rule has a reason of at most 40 bytes", () => {
    for (const rule of RISK_RULES) {
      expect(rule.reason.length, rule.id).toBeGreaterThan(0);
      expect(utf8ByteLength(rule.reason), rule.id).toBeLessThanOrEqual(LIMITS.riskReason);
    }
  });

  it("Bash: cmd is the command verbatim, desc is Claude's description, reason the rule", () => {
    const call = formatToolCall("Bash", { command: "git push --force origin main", description: "Push the fix" }, CWD);
    expect(call).toMatchObject({
      cmd: "git push --force origin main",
      desc: "Push the fix",
      risk: "high",
      reason: "Force push: rewrites the remote branch",
    });
    expect(call.cut).toBeUndefined();
    const low = formatToolCall("Bash", { command: "pnpm test\npnpm lint" }, CWD);
    expect(low).toMatchObject({ cmd: "pnpm test\npnpm lint", risk: "low" });
    expect(low.reason).toBeUndefined();
    expect(low.desc).toBeUndefined();
  });

  it("Edit: path, +A -B lines, then the change", () => {
    const call = formatToolCall("Edit", { file_path: `${CWD}/a.ts`, old_string: "x = 1", new_string: "x = 2\ny = 3" }, CWD);
    expect(call.cmd).toBe("a.ts\n+2 -1 lines\n- x = 1\n+ x = 2\n+ y = 3");
    expect(call.risk).toBe("low");
  });

  it("a path outside the project has a reason", () => {
    expect(formatToolCall("Write", { file_path: "/tmp/x", content: "a" }, CWD)).toMatchObject({
      risk: "med",
      reason: "Path outside the project",
    });
  });

  it("a long command is cut at 600 bytes; a non-ASCII one is cut too", () => {
    const long = formatToolCall("Bash", { command: `echo ${"a".repeat(700)}` }, CWD);
    expect(utf8ByteLength(long.cmd)).toBe(LIMITS.cmd);
    expect(long.cut).toBe(true);
    expect(formatToolCall("Bash", { command: "echo héllo" }, CWD).cut).toBe(true);
  });

  it("other tools: compact JSON of the input", () => {
    expect(formatToolCall("WebSearch", { query: "lvgl" }, CWD).cmd).toBe('{"query":"lvgl"}');
  });
});

describe("frames stay within 2048 bytes", () => {
  it("permission: never cut and grant together, never over 2048 bytes (random inputs)", () => {
    const next = rng(1);
    for (let i = 0; i < 300; i++) {
      const command = i % 3 === 0 ? `pnpm test ${randomText(next, 40)}` : randomText(next, 2500);
      const input = { command, description: randomText(next, 400) };
      const call = formatToolCall("Bash", input, CWD);
      const rule = i % 2 === 0 ? command.trim() : "pnpm test:*";
      const suggestions = [{ type: "addRules", behavior: "allow", destination: "localSettings", rules: [{ toolName: "Bash", ruleContent: rule }] }];
      const grant = deriveGrant("Bash", input, call.risk, call.cut === true, CWD, suggestions);
      const msg = buildPermission({
        id: ID,
        tool: call.tool,
        summary: call.summary,
        risk: call.risk,
        cmd: call.cmd,
        cut: call.cut,
        desc: call.desc,
        reason: call.reason,
        grant: grant ? { rule: grant.rule, match: grant.match } : undefined,
        project: randomText(next, 40),
        sid: "3fa9c1",
        timeout: 30_000,
        timestamp: new Date(0).toISOString(),
      });
      expect(msg.cut === true && msg.grant !== undefined).toBe(false);
      expect(utf8ByteLength(serialize(msg))).toBeLessThanOrEqual(MAX_FRAME_BYTES);
      if (msg.cmd !== call.cmd) expect(msg.cut).toBe(true);
    }
  });

  it("permission: a grant is dropped when escaping forces a shorter cmd", () => {
    const q = (n: number) => '"'.repeat(n);
    const cmd = q(LIMITS.cmd);
    const msg = buildPermission({
      id: ID,
      tool: q(LIMITS.tool),
      summary: q(LIMITS.summary),
      risk: "med",
      reason: q(LIMITS.riskReason),
      project: q(LIMITS.project),
      cmd,
      grant: { rule: q(LIMITS.grantRule), match: "exact" },
      timeout: 30_000,
    });
    expect(msg.cmd!.length).toBeLessThan(cmd.length);
    expect(utf8ByteLength(serialize(msg))).toBeLessThanOrEqual(MAX_FRAME_BYTES);
    expect(msg.cut).toBe(true);
    expect(msg.grant).toBeUndefined();
  });

  it("question: long descriptions are shortened, every frame fits (random inputs)", () => {
    const next = rng(2);
    const base = { id: ID, sid: "3fa9c1", project: "p".repeat(32), timeout: 300_000, timestamp: new Date(0).toISOString() };
    let framed = 0;
    for (let i = 0; i < 200; i++) {
      const questions = Array.from({ length: 1 + (i % 4) }, (_, q) => ({
        question: `Q${q} ${'"'.repeat(Math.floor(next() * 470))}`,
        header: randomText(next, 60),
        options: Array.from({ length: 2 + (i % 3) }, (_, o) => ({
          label: `L${o} ${"\\".repeat(Math.floor(next() * 70))}`,
          description: randomText(next, 600),
        })),
        multiSelect: next() < 0.5,
      }));
      const specs = questionSpecs({ questions });
      if (specs === null) continue;
      const frames = buildQuestionFrames(specs, base);
      if (frames === null) continue;
      framed++;
      for (const frame of frames) expect(utf8ByteLength(serialize(frame))).toBeLessThanOrEqual(MAX_FRAME_BYTES);
    }
    expect(framed).toBeGreaterThan(50);
  });

  it("plan: every frame fits, and every frame the overflow rule shortened has cut and more > 0", () => {
    const next = rng(3);
    const base = { id: ID, sid: "3fa9c1", project: "p".repeat(32), timeout: 300_000, timestamp: new Date(0).toISOString() };
    let overflowed = 0;
    for (let i = 0; i < 200; i++) {
      const char = i % 2 === 0 ? '"' : "\\";
      const steps = Array.from({ length: 1 + Math.floor(next() * 14) }, () =>
        i % 5 === 0 ? char.repeat(120) : randomText(next, 160),
      );
      const plan = `# ${randomText(next, 100)}\n\n${steps.map((s) => `- ${s.replace(/\n/g, " ")}`).join("\n")}`;
      const summary = summarisePlan(plan);
      const frame = buildPlanFrame(summary, base);
      expect(utf8ByteLength(serialize(frame))).toBeLessThanOrEqual(MAX_FRAME_BYTES);
      if (frame.steps.length < summary.steps.length) {
        overflowed++;
        expect(frame.cut).toBe(true);
        expect(frame.more).toBeGreaterThan(0);
      }
    }
    expect(overflowed).toBeGreaterThan(5);
  });
});

describe("time zones", () => {
  it("canonicalises and validates zone names", () => {
    expect(canonicalTimeZone("Europe/Berlin")).toBe("Europe/Berlin");
    expect(canonicalTimeZone("europe/berlin")).toBe("Europe/Berlin");
    expect(canonicalTimeZone("Asia/Kolkata")).toBe("Asia/Kolkata");
    expect(canonicalTimeZone("UTC")).toBe("UTC");
    expect(canonicalTimeZone("Mars/Olympus")).toBeNull();
    expect(canonicalTimeZone("")).toBeNull();
    expect(canonicalTimeZone("Europe/Berlin; DROP")).toBeNull();
  });

  it("a half-hour zone: Asia/Kolkata is 330", () => {
    expect(utcOffsetMinutes("Asia/Kolkata", Date.UTC(2026, 0, 15))).toBe(330);
  });

  it("follows daylight saving: Europe/Berlin around the change of 29 March 2026", () => {
    expect(utcOffsetMinutes("Europe/Berlin", Date.UTC(2026, 2, 29, 0, 59))).toBe(60);
    expect(utcOffsetMinutes("Europe/Berlin", Date.UTC(2026, 2, 29, 1, 0))).toBe(120);
    expect(utcOffsetMinutes("America/New_York", Date.UTC(2026, 6, 1))).toBe(-240);
  });

  it("the extremes of the range", () => {
    expect(utcOffsetMinutes("Pacific/Kiritimati", Date.UTC(2026, 0, 1))).toBe(840);
    expect(utcOffsetMinutes("Etc/GMT+12", Date.UTC(2026, 0, 1))).toBe(-720);
    expect(utcOffsetMinutes("Asia/Kathmandu", Date.UTC(2026, 0, 1))).toBe(345);
  });
});
