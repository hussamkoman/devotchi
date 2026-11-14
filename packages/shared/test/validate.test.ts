import { describe, expect, it } from "vitest";
import {
  CLOSE_CODES,
  CONSUMER_KINDS,
  INBOUND_TYPES,
  LIMITS,
  MAX_FRAME_BYTES,
  MOODS,
  PRODUCER_KINDS,
  type InboundMessage,
  type Role,
  type TokenKind,
} from "../src/protocol.js";
import { utf8ByteLength } from "../src/text.js";
import {
  FrameTooLargeError,
  buildDismiss,
  buildError,
  buildHolding,
  buildIdentify,
  buildMood,
  buildNotify,
  buildPermission,
  buildPermissionResponse,
  buildRegister,
  buildRegisterAck,
  buildStatus,
  buildTelemetry,
  parseInbound,
  serialize,
  type InboundContext,
  type ParseResult,
} from "../src/validate.js";

const ID = "9f2c4e6a8b0d1f3a5c7e9b1d3f5a7c9e";
const producer: InboundContext = { role: "producer", tokenKind: "cli" };
const consumer: InboundContext = { role: "consumer", tokenKind: "device" };

function parse(msg: unknown, ctx: InboundContext): ParseResult {
  return parseInbound(JSON.stringify(msg), ctx);
}

function ok(msg: unknown, ctx: InboundContext): InboundMessage {
  const result = parse(msg, ctx);
  if (!result.ok) throw new Error(`expected ok, got ${result.code}: ${result.message}`);
  return result.msg;
}

function fails(msg: unknown, ctx: InboundContext) {
  const result = typeof msg === "string" ? parseInbound(msg, ctx) : parse(msg, ctx);
  if (result.ok) throw new Error(`expected failure, got ${JSON.stringify(result.msg)}`);
  return result;
}

const telemetry = {
  type: "telemetry",
  battery: { state: "discharging", pct: 82, mv: 3987 },
  power: "battery",
  rssi: -58,
  uptimeS: 5321,
  heapFree: 148392,
  heapMin: 101224,
  resetReason: "poweron",
  fw: "0.2.0",
  screen: "pet",
  stage: "active",
  alert: "battery_low",
};

describe("parseInbound: framing", () => {
  it("rejects non-JSON, non-objects and a missing or non-string type", () => {
    for (const raw of ["not json", "[]", "null", "42", '"status"', "{}", '{"type":3}']) {
      expect(fails(raw, producer).code).toBe("INVALID_MESSAGE");
    }
  });

  it("accepts a 2048-byte frame and rejects 2049 bytes as MESSAGE_TOO_LARGE", () => {
    const base = { type: "status", msg: "" };
    const pad = MAX_FRAME_BYTES - JSON.stringify(base).length;
    const exact = JSON.stringify({ ...base, msg: "a".repeat(pad) });
    expect(exact.length).toBe(MAX_FRAME_BYTES);
    expect(parseInbound(exact, producer).ok).toBe(true);
    const over = JSON.stringify({ ...base, msg: "a".repeat(pad + 1) });
    expect(fails(over, producer).code).toBe("MESSAGE_TOO_LARGE");
  });

  it("counts bytes, not characters", () => {
    const frame = JSON.stringify({ type: "status", msg: "日".repeat(700) });
    expect(frame.length).toBeLessThan(MAX_FRAME_BYTES);
    expect(fails(frame, producer).code).toBe("MESSAGE_TOO_LARGE");
  });

  it("accepts a Uint8Array frame", () => {
    const bytes = new TextEncoder().encode(JSON.stringify({ type: "ping" }));
    expect(parseInbound(bytes, producer)).toEqual({ ok: true, msg: { type: "ping" } });
  });

  it("drops unknown fields and producer timestamps", () => {
    const msg = ok({ type: "status", msg: "hi", extra: 1, timestamp: "2020-01-01T00:00:00Z" }, producer);
    expect(msg).toEqual({ type: "status", msg: "hi" });
  });
});

describe("parseInbound: role whitelist (section 3.7)", () => {
  const samples: Record<string, unknown> = {
    ping: { type: "ping" },
    pong: { type: "pong" },
    status: { type: "status", msg: "x" },
    notify: { type: "notify", event: "tests_passed", mood: "happy" },
    mood: { type: "mood", mood: "happy" },
    holding: { type: "holding", ids: [ID] },
    permission_response: { type: "permission_response", id: ID, granted: true },
    permission_defer: { type: "permission_defer", id: ID },
    set_mode: { type: "set_mode", mode: "desk" },
    telemetry,
    permission: { type: "permission", id: ID, tool: "Bash", summary: "x", risk: "low", timeout: 5000 },
    register_ack: { type: "register_ack" },
    dismiss: { type: "dismiss", id: ID, reason: "timeout" },
    error: { type: "error", code: "INVALID_MESSAGE", message: "x" },
    peers: { type: "peers", producers: 1, consumers: 1, devices: 1 },
    mode: { type: "mode", mode: "ask" },
    identify: { type: "identify", durationMs: 3000 },
    approval: { type: "approval", id: ID, desc: "x", timeout: 1000 },
    approval_response: { type: "approval_response", id: ID, approved: true },
    some_future_type: { type: "some_future_type" },
  };

  for (const role of ["producer", "consumer"] as Role[]) {
    for (const [type, sample] of Object.entries(samples)) {
      const allowed = (INBOUND_TYPES[role] as readonly string[]).includes(type);
      it(`${role} ${type} -> ${allowed ? "accepted" : "NOT_ALLOWED"}`, () => {
        const result = parse(sample, { role });
        if (allowed) expect(result.ok).toBe(true);
        else expect(result).toMatchObject({ ok: false, code: "NOT_ALLOWED" });
      });
    }
  }

  it("rejects a second register on a registered socket", () => {
    const reg = { type: "register", role: "producer", proto: 2, client: { kind: "mcp" } };
    expect(fails(reg, producer)).toMatchObject({ code: "INVALID_MESSAGE" });
    expect(fails(reg, producer).close).toBeUndefined();
  });

  it("requires register as the first message and asks for close 1008", () => {
    const result = fails({ type: "status", msg: "x" }, { role: "unregistered", tokenKind: "cli" });
    expect(result).toMatchObject({ code: "INVALID_MESSAGE", close: CLOSE_CODES.POLICY_VIOLATION });
    expect(fails("garbage", { role: "unregistered", tokenKind: "cli" }).close).toBe(1008);
  });
});

describe("parseInbound: register (section 3.2)", () => {
  const first = (tokenKind: TokenKind): InboundContext => ({ role: "unregistered", tokenKind });
  const register = (role: string, kind: string, extra: Record<string, unknown> = {}) => ({
    type: "register",
    role,
    proto: 2,
    client: { kind, deviceId: kind === "device" ? "dv-e356e4" : kind === "sim" ? "sim-test" : undefined, ...extra },
  });

  // Every (token kind, role, client kind) combination against protocol 3.2.
  const expected: Record<TokenKind, Record<Role, Record<string, boolean>>> = {
    device: {
      producer: { mcp: false, cli: false },
      consumer: { device: true, sim: true, dashboard: false },
    },
    cli: {
      producer: { mcp: true, cli: true },
      consumer: { device: false, sim: true, dashboard: true },
    },
  };
  for (const tokenKind of ["device", "cli"] as TokenKind[]) {
    for (const role of ["producer", "consumer"] as Role[]) {
      for (const kind of [...PRODUCER_KINDS, ...CONSUMER_KINDS]) {
        const allowed = expected[tokenKind][role][kind] ?? false;
        it(`${tokenKind} token, ${role}/${kind} -> ${allowed ? "ok" : "NOT_ALLOWED + 1008"}`, () => {
          const result = parse(register(role, kind), first(tokenKind));
          if (allowed) {
            expect(result).toMatchObject({ ok: true, msg: { type: "register", role, client: { kind } } });
          } else {
            expect(result).toMatchObject({ ok: false, code: "NOT_ALLOWED", close: 1008 });
          }
        });
      }
    }
  }

  it("returns the normalised register without v1 fields", () => {
    const msg = ok(
      {
        type: "register",
        role: "consumer",
        proto: 2,
        roomId: "abc",
        token: "secret",
        client: { kind: "device", deviceId: "dv-e356e4", model: "devotchi-v1", version: "0.2.0", project: "x" },
      },
      first("device"),
    );
    expect(msg).toEqual({
      type: "register",
      role: "consumer",
      proto: 2,
      client: { kind: "device", deviceId: "dv-e356e4", model: "devotchi-v1", version: "0.2.0" },
    });
  });

  it("accepts client.fw instead of version but drops it", () => {
    const msg = ok(register("consumer", "device", { fw: "0.1.0" }), first("device"));
    expect(msg).toEqual({ type: "register", role: "consumer", proto: 2, client: { kind: "device", deviceId: "dv-e356e4" } });
  });

  it("keeps the producer project, cut to 32 bytes", () => {
    const msg = ok(register("producer", "mcp", { project: "p".repeat(50), version: "v".repeat(30) }), first("cli"));
    expect(msg).toMatchObject({ client: { kind: "mcp", project: "p".repeat(32), version: "v".repeat(24) } });
  });

  it.each([undefined, 1, 3, "2", null])("proto %j -> UNSUPPORTED_PROTO + close 4002", (proto) => {
    const reg = { type: "register", role: "consumer", proto, client: { kind: "dashboard" } };
    expect(fails(reg, first("cli"))).toMatchObject({ code: "UNSUPPORTED_PROTO", close: 4002 });
  });

  it("treats a v1 register (no proto) as unsupported", () => {
    const v1 = { type: "register", role: "consumer", roomId: "5e140e256d56", token: "t" };
    expect(fails(v1, first("device"))).toMatchObject({ code: "UNSUPPORTED_PROTO", close: 4002 });
  });

  it.each([
    ["missing deviceId", { kind: "device" }],
    ["upper-case hex", { kind: "device", deviceId: "dv-E356E4" }],
    ["short hex", { kind: "device", deviceId: "dv-e356" }],
    ["non-string", { kind: "device", deviceId: 123 }],
    ["sim without id", { kind: "sim" }],
    ["sim id too long", { kind: "sim", deviceId: "sim-abcdefghijklm" }],
    ["sim id bad chars", { kind: "sim", deviceId: "sim-Desk_1" }],
  ])("%s -> INVALID_MESSAGE + 1008", (_, client) => {
    const reg = { type: "register", role: "consumer", proto: 2, client };
    expect(fails(reg, first("device"))).toMatchObject({ code: "INVALID_MESSAGE", close: 1008 });
  });

  it("accepts both id formats for device and sim kinds", () => {
    for (const kind of ["device", "sim"]) {
      for (const deviceId of ["dv-e356e4", "sim-desk-1"]) {
        expect(parse({ type: "register", role: "consumer", proto: 2, client: { kind, deviceId } }, first("device")).ok).toBe(true);
      }
    }
  });

  it("does not require a deviceId for dashboards and producers", () => {
    expect(ok(register("consumer", "dashboard"), first("cli"))).toEqual({
      type: "register",
      role: "consumer",
      proto: 2,
      client: { kind: "dashboard" },
    });
  });

  it.each([
    ["bad role", { type: "register", role: "admin", proto: 2, client: { kind: "mcp" } }],
    ["no client", { type: "register", role: "producer", proto: 2 }],
    ["client not object", { type: "register", role: "producer", proto: 2, client: "mcp" }],
    ["kind not string", { type: "register", role: "producer", proto: 2, client: { kind: 1 } }],
    ["unknown kind", { type: "register", role: "producer", proto: 2, client: { kind: "robot" } }],
    ["model not string", { type: "register", role: "producer", proto: 2, client: { kind: "mcp", model: 1 } }],
  ])("%s -> INVALID_MESSAGE", (_, reg) => {
    expect(fails(reg, first("cli"))).toMatchObject({ code: "INVALID_MESSAGE", close: 1008 });
  });
});

describe("parseInbound: producer content", () => {
  it("keeps a status without mood without mood", () => {
    const msg = ok({ type: "status", msg: "Edit src/app.c" }, producer);
    expect(msg).toEqual({ type: "status", msg: "Edit src/app.c" });
    expect("mood" in msg).toBe(false);
  });

  it("accepts every mood in status, notify and mood", () => {
    for (const mood of MOODS) {
      expect(ok({ type: "status", msg: "x", mood }, producer)).toMatchObject({ mood });
      expect(ok({ type: "notify", event: "e", mood }, producer)).toMatchObject({ mood });
      expect(ok({ type: "mood", mood }, producer)).toMatchObject({ mood });
    }
  });

  it.each([
    ["status unknown mood", { type: "status", msg: "x", mood: "bored" }],
    ["status msg missing", { type: "status" }],
    ["status msg number", { type: "status", msg: 5 }],
    ["status msg null", { type: "status", msg: null }],
    ["status bad sid", { type: "status", msg: "x", sid: "XYZ" }],
    ["status project number", { type: "status", msg: "x", project: 3 }],
    ["notify no mood", { type: "notify", event: "x" }],
    ["notify unknown mood", { type: "notify", event: "x", mood: "bored" }],
    ["notify event pattern", { type: "notify", event: "Tests Passed", mood: "happy" }],
    ["notify empty event", { type: "notify", event: "", mood: "happy" }],
    ["notify urgency 4", { type: "notify", event: "x", mood: "happy", urgency: 4 }],
    ["notify urgency float", { type: "notify", event: "x", mood: "happy", urgency: 1.5 }],
    ["notify urgency string", { type: "notify", event: "x", mood: "happy", urgency: "2" }],
    ["mood missing", { type: "mood" }],
    ["mood reason number", { type: "mood", mood: "happy", reason: 1 }],
  ])("%s -> INVALID_MESSAGE", (_, msg) => {
    const result = fails(msg, producer);
    expect(result.code).toBe("INVALID_MESSAGE");
    expect(result.close).toBeUndefined();
  });

  it("truncates strings to their byte limits on code point boundaries", () => {
    const status = ok({ type: "status", msg: "é".repeat(150), project: "日".repeat(20) }, producer) as {
      msg: string;
      project: string;
    };
    expect(status.msg).toBe("é".repeat(100));
    expect(status.project).toBe("日".repeat(10));
    const notify = ok(
      { type: "notify", event: "e".repeat(60), mood: "sad", detail: "d".repeat(400), source: "s".repeat(40), urgency: 3 },
      producer,
    );
    expect(notify).toEqual({
      type: "notify",
      event: "e".repeat(LIMITS.event),
      mood: "sad",
      detail: "d".repeat(LIMITS.detail),
      source: "s".repeat(LIMITS.source),
      urgency: 3,
    });
    const mood = ok({ type: "mood", mood: "anxious", reason: "😀".repeat(60) }, producer) as { reason: string };
    expect(utf8ByteLength(mood.reason)).toBe(200);
  });

  it("replaces lone surrogates in forwarded text", () => {
    const msg = ok({ type: "status", msg: "a\ud800b" }, producer) as { msg: string };
    expect(msg.msg).toBe("a�b");
  });

  it("keeps notify optional fields", () => {
    expect(ok({ type: "notify", event: "tests_failed", mood: "sad", sid: "3fa9c1", project: "devotchi", urgency: 0 }, producer)).toEqual({
      type: "notify",
      event: "tests_failed",
      mood: "sad",
      sid: "3fa9c1",
      project: "devotchi",
      urgency: 0,
    });
  });
});

describe("parseInbound: consumer messages", () => {
  it("accepts a permission_response with a strict boolean", () => {
    expect(ok({ type: "permission_response", id: ID, granted: false, timestamp: "x" }, consumer)).toEqual({
      type: "permission_response",
      id: ID,
      granted: false,
    });
  });

  it.each(["false", 0, 1, null, "true"])("granted %j -> INVALID_MESSAGE with the id", (granted) => {
    expect(fails({ type: "permission_response", id: ID, granted }, consumer)).toEqual({
      ok: false,
      code: "INVALID_MESSAGE",
      message: "permission_response.granted must be a boolean",
      id: ID,
    });
  });

  it("does not echo a malformed id", () => {
    for (const id of ["abc", ID.toUpperCase(), 7, undefined]) {
      const result = fails({ type: "permission_response", id, granted: "no" }, consumer);
      expect(result.code).toBe("INVALID_MESSAGE");
      expect(result.id).toBeUndefined();
    }
    expect(fails({ type: "permission_defer", id: "short" }, consumer).id).toBeUndefined();
  });

  it("accepts permission_defer", () => {
    expect(ok({ type: "permission_defer", id: ID }, consumer)).toEqual({ type: "permission_defer", id: ID });
  });

  it("accepts holding and drops malformed or duplicate ids", () => {
    const other = "a".repeat(32);
    expect(ok({ type: "holding", ids: [ID, "nope", ID, other] }, consumer)).toEqual({ type: "holding", ids: [ID, other] });
  });

  it.each([
    ["no ids", { type: "holding" }],
    ["empty", { type: "holding", ids: [] }],
    ["nine ids", { type: "holding", ids: Array.from({ length: 9 }, () => ID) }],
    ["not array", { type: "holding", ids: ID }],
    ["non-string id", { type: "holding", ids: [1] }],
  ])("holding %s -> INVALID_MESSAGE", (_, msg) => {
    expect(fails(msg, consumer).code).toBe("INVALID_MESSAGE");
  });

  it("accepts set_mode ask and desk only", () => {
    expect(ok({ type: "set_mode", mode: "desk" }, consumer)).toEqual({ type: "set_mode", mode: "desk" });
    expect(fails({ type: "set_mode", mode: "auto" }, consumer).code).toBe("INVALID_MESSAGE");
  });

  it("accepts full telemetry and an absent battery with pct null", () => {
    expect(ok(telemetry, consumer)).toEqual(telemetry);
    const usb = { ...telemetry, battery: { state: "absent", pct: null, mv: 4116 }, power: "usb" };
    delete (usb as Record<string, unknown>).alert;
    expect(ok(usb, consumer)).toEqual(usb);
  });

  it.each([
    ["no battery", { ...telemetry, battery: undefined }],
    ["bad state", { ...telemetry, battery: { state: "dead", pct: 1, mv: 1 } }],
    ["pct 101", { ...telemetry, battery: { state: "full", pct: 101, mv: 1 } }],
    ["pct missing", { ...telemetry, battery: { state: "full", mv: 1 } }],
    ["mv float", { ...telemetry, battery: { state: "full", pct: 1, mv: 1.5 } }],
    ["bad power", { ...telemetry, power: "solar" }],
    ["rssi string", { ...telemetry, rssi: "-58" }],
    ["uptime negative", { ...telemetry, uptimeS: -1 }],
    ["heap NaN-like", { ...telemetry, heapFree: "1" }],
    ["fw missing", { ...telemetry, fw: undefined }],
    ["bad stage", { ...telemetry, stage: "sleep" }],
    ["bad alert", { ...telemetry, alert: "fire" }],
  ])("telemetry %s -> INVALID_MESSAGE", (_, msg) => {
    expect(fails(msg, consumer).code).toBe("INVALID_MESSAGE");
  });

  it("answers pong and ping with bare messages", () => {
    expect(ok({ type: "pong", extra: 1 }, consumer)).toEqual({ type: "pong" });
    expect(ok({ type: "ping" }, consumer)).toEqual({ type: "ping" });
  });
});

describe("builders", () => {
  it("builds a status with mood only when given", () => {
    const without = buildStatus({ msg: "hi" });
    expect(without).toEqual({ type: "status", msg: "hi" });
    expect("mood" in without).toBe(false);
    expect(buildStatus({ msg: "hi", mood: "working", sid: "3fa9c1", project: "p", timestamp: "t" })).toEqual({
      type: "status",
      msg: "hi",
      mood: "working",
      sid: "3fa9c1",
      project: "p",
      timestamp: "t",
    });
  });

  it("drops a malformed sid and rejects an unknown mood", () => {
    expect(buildStatus({ msg: "x", sid: "nothex" })).toEqual({ type: "status", msg: "x" });
    expect(() => buildStatus({ msg: "x", mood: "bored" as never })).toThrow(TypeError);
  });

  it("builds notify with clamped urgency and checks the event", () => {
    expect(buildNotify({ event: "tests_failed", mood: "sad", urgency: 9 })).toMatchObject({ urgency: 3 });
    expect(() => buildNotify({ event: "Bad Event", mood: "sad" })).toThrow(TypeError);
  });

  it("builds a permission with ASCII text and timeout >= 1000", () => {
    const msg = buildPermission({
      id: ID,
      tool: "Bash\t",
      summary: "rm -rf dist ✓",
      details: "Remove\n$ rm -rf dist",
      risk: "high",
      sid: "3fa9c1",
      project: "dévotchi",
      timeout: 200,
      timestamp: "t",
    });
    expect(msg).toEqual({
      type: "permission",
      id: ID,
      tool: "Bash?",
      summary: "rm -rf dist ?",
      details: "Remove\n$ rm -rf dist",
      risk: "high",
      sid: "3fa9c1",
      project: "d?votchi",
      timeout: 1000,
      timestamp: "t",
    });
    expect(() => buildPermission({ id: "x", tool: "t", summary: "s", risk: "low", timeout: 5000 })).toThrow(TypeError);
    expect(() => buildPermission({ id: "a".repeat(32), tool: "t", summary: "s", risk: "low", timeout: NaN })).toThrow(TypeError);
  });

  it("builds dismiss variants", () => {
    expect(buildDismiss({ id: ID, reason: "answered", outcome: "allow", by: "dv-e356e4" })).toEqual({
      type: "dismiss",
      id: ID,
      reason: "answered",
      outcome: "allow",
      by: "dv-e356e4",
    });
    expect(buildDismiss({ id: ID, reason: "released" })).toEqual({ type: "dismiss", id: ID, reason: "released" });
  });

  it("builds an error with an optional well-formed id", () => {
    expect(buildError("UNKNOWN_REQUEST", "not pending", ID)).toEqual({
      type: "error",
      code: "UNKNOWN_REQUEST",
      message: "not pending",
      id: ID,
    });
    expect(buildError("INVALID_MESSAGE", "x")).toEqual({ type: "error", code: "INVALID_MESSAGE", message: "x" });
    expect(buildError("INVALID_MESSAGE", "x", "bad")).not.toHaveProperty("id");
  });

  it("builds register and register_ack", () => {
    expect(buildRegister({ role: "producer", client: { kind: "mcp", version: "0.2.0", project: "p".repeat(40) } })).toEqual({
      type: "register",
      role: "producer",
      proto: 2,
      client: { kind: "mcp", version: "0.2.0", project: "p".repeat(32) },
    });
    const ack = buildRegisterAck({
      role: "consumer",
      roomId: "5e140e256d56",
      serverVersion: "0.2.0",
      serverTime: 1794668400000,
      mode: "ask",
      peers: { producers: 1, consumers: 2, devices: 1 },
      device: { id: "dv-e356e4", name: "desk" },
    });
    expect(ack).toEqual({
      type: "register_ack",
      role: "consumer",
      roomId: "5e140e256d56",
      proto: 2,
      serverVersion: "0.2.0",
      serverTime: 1794668400000,
      mode: "ask",
      peers: { producers: 1, consumers: 2, devices: 1 },
      device: { id: "dv-e356e4", name: "desk" },
    });
  });

  it("builds client messages", () => {
    expect(buildHolding([ID, ID, "bad"])).toEqual({ type: "holding", ids: [ID] });
    expect(buildPermissionResponse(ID, true)).toEqual({ type: "permission_response", id: ID, granted: true });
    expect(buildIdentify(50_000)).toEqual({ type: "identify", durationMs: 10_000 });
    expect(buildIdentify()).toEqual({ type: "identify", durationMs: 3000 });
    const { type: _type, ...fields } = telemetry;
    expect(buildTelemetry(fields as never)).toEqual(telemetry);
  });

  it("round-trips builder output through parseInbound", () => {
    for (const msg of [
      buildStatus({ msg: "x", mood: "focused" }),
      buildNotify({ event: "e", mood: "happy", detail: "d", urgency: 2 }),
      buildMood({ mood: "proud", reason: "r" }),
    ]) {
      expect(parseInbound(serialize(msg), producer)).toEqual({ ok: true, msg });
    }
  });

  it("serialize refuses a frame above 2048 bytes", () => {
    const huge = { type: "status", msg: "x".repeat(3000) } as const;
    expect(() => serialize(huge)).toThrow(FrameTooLargeError);
  });
});

describe("2048-byte guarantee", () => {
  // Characters with the largest JSON expansion: control characters become
  // \u00XX (6 bytes for 1), quotes and backslashes double.
  const worst = ["\u0001", '"', "\\", "\n", "\u001f", "é", "日", "😀", "\ud800", "a"];

  function randomText(seed: number, length: number): string {
    let state = seed;
    let out = "";
    for (let i = 0; i < length; i++) {
      state = (state * 1103515245 + 12345) & 0x7fffffff;
      out += worst[state % worst.length];
    }
    return out;
  }

  it("a built permission frame never exceeds 2048 bytes", () => {
    for (let seed = 0; seed < 300; seed++) {
      const msg = buildPermission({
        id: ID,
        tool: randomText(seed, 100),
        summary: randomText(seed + 1, (seed * 7) % 900),
        details: randomText(seed + 2, (seed * 13) % 2000),
        risk: "med",
        sid: "3fa9c1",
        project: randomText(seed + 3, 80),
        timeout: 300000,
        timestamp: "2026-11-14T14:30:00.000Z",
      });
      expect(utf8ByteLength(serialize(msg))).toBeLessThanOrEqual(MAX_FRAME_BYTES);
    }
  });

  it("a permission made only of characters that JSON doubles still fits", () => {
    const msg = buildPermission({
      id: ID,
      tool: '"'.repeat(40),
      summary: '"'.repeat(200),
      details: '"'.repeat(600),
      risk: "high",
      sid: "3fa9c1",
      project: '"'.repeat(32),
      timeout: 30000,
      timestamp: "2026-11-14T14:30:00.000Z",
    });
    const frame = serialize(msg);
    expect(utf8ByteLength(frame)).toBeLessThanOrEqual(MAX_FRAME_BYTES);
    expect(msg.summary).toHaveLength(200);
  });

  it("status, notify and mood frames fit with worst-case escaping", () => {
    for (let seed = 0; seed < 200; seed++) {
      const text = (n: number) => randomText(seed + n, 1000);
      const frames = [
        buildStatus({ msg: text(0), mood: "working", sid: "3fa9c1", project: text(1), timestamp: "2026-11-14T14:30:00.000Z" }),
        buildNotify({
          event: "tests_failed",
          mood: "sad",
          detail: text(2),
          source: text(3),
          project: text(4),
          sid: "3fa9c1",
          urgency: 2,
          timestamp: "2026-11-14T14:30:00.000Z",
        }),
        buildMood({ mood: "anxious", reason: text(5), timestamp: "2026-11-14T14:30:00.000Z" }),
        buildError("INVALID_MESSAGE", text(6), ID),
      ];
      for (const frame of frames) expect(utf8ByteLength(serialize(frame))).toBeLessThanOrEqual(MAX_FRAME_BYTES);
    }
  });

  it("a notify of control characters fits by shortening detail", () => {
    const msg = buildNotify({
      event: "e",
      mood: "sad",
      detail: "\u0001".repeat(300),
      source: "\u0001".repeat(32),
      project: "\u0001".repeat(32),
    });
    expect(utf8ByteLength(serialize(msg))).toBeLessThanOrEqual(MAX_FRAME_BYTES);
    expect(msg.source).toHaveLength(32);
  });
});
