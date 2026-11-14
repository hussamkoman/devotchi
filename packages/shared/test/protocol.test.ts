import { describe, expect, it } from "vitest";
import {
  DEVICE_ID_RE,
  HoldingAckScheduler,
  INBOUND_TYPES,
  MOODS,
  PROTO_VERSION,
  SIM_ID_RE,
  defaultMoodForEvent,
  eventClass,
  isMood,
  isValidDeviceId,
} from "../src/protocol.js";

const id = (n: number) => n.toString(16).padStart(32, "0");

describe("constants", () => {
  it("is protocol 2 with the 11 moods", () => {
    expect(PROTO_VERSION).toBe(2);
    expect([...MOODS].sort()).toEqual(
      ["happy", "sad", "excited", "anxious", "sleeping", "angry", "proud", "idle", "focused", "working", "thinking"].sort(),
    );
    expect(isMood("working")).toBe(true);
    expect(isMood("bored")).toBe(false);
    expect(isMood(1)).toBe(false);
  });

  it("matches the role table of section 3.7", () => {
    expect(INBOUND_TYPES.producer).not.toContain("permission_response");
    expect(INBOUND_TYPES.consumer).not.toContain("status");
    expect(INBOUND_TYPES.consumer).toContain("holding");
  });

  it.each([
    ["dv-e356e4", true],
    ["dv-000000", true],
    ["dv-E356E4", false],
    ["dv-e356e", false],
    ["dv-e356e45", false],
    ["sim-a", true],
    ["sim-desk-01", true],
    ["sim-abcdefghijkl", true],
    ["sim-abcdefghijklm", false],
    ["sim-", false],
    ["sim-Desk", false],
    ["sim_desk", false],
    ["device", false],
  ])("device id %s valid=%s", (value, valid) => {
    expect(isValidDeviceId(value)).toBe(valid);
    expect(DEVICE_ID_RE.test(value) || SIM_ID_RE.test(value)).toBe(valid);
  });
});

describe("eventClass (section 5.2)", () => {
  it.each([
    ["tests_passed", undefined, "positive"],
    ["build_succeeded", undefined, "positive"],
    ["deploy_complete", undefined, "positive"],
    ["lint_succeeded", "sad", "positive"],
    ["e2e_passed", undefined, "positive"],
    ["tests_failed", "happy", "negative"],
    ["build_failed", undefined, "negative"],
    ["deploy_failed", undefined, "negative"],
    ["error", undefined, "negative"],
    ["lint_failed", undefined, "negative"],
    ["task_complete", "happy", "complete"],
    ["task_paused", "sad", "none"],
    ["needs_input", "anxious", "attention"],
    ["coffee", "happy", "positive"],
    ["coffee", "excited", "positive"],
    ["coffee", "proud", "positive"],
    ["coffee", "sad", "negative"],
    ["coffee", "angry", "negative"],
    ["coffee", "thinking", "none"],
    ["coffee", undefined, "none"],
    ["coffee", "nonsense", "none"],
  ] as const)("%s with mood %s -> %s", (event, mood, cls) => {
    expect(eventClass(event, mood)).toBe(cls);
  });

  it("gives the REST default mood by class", () => {
    expect(defaultMoodForEvent("tests_passed")).toBe("happy");
    expect(defaultMoodForEvent("build_failed")).toBe("sad");
    expect(defaultMoodForEvent("task_complete")).toBe("idle");
    expect(defaultMoodForEvent("custom")).toBe("idle");
  });
});

describe("HoldingAckScheduler (section 4.7)", () => {
  it("is idle with nothing held", () => {
    const s = new HoldingAckScheduler();
    expect(s.nextDueAt()).toBeNull();
    expect(s.onTick(1_000_000)).toBeNull();
  });

  it("acks one permission 100 ms after it arrived", () => {
    const s = new HoldingAckScheduler();
    s.onPermission(id(1), 1000);
    expect(s.nextDueAt()).toBe(1100);
    expect(s.onTick(1099)).toBeNull();
    expect(s.onTick(1100)).toEqual([id(1)]);
    expect(s.onTick(1101)).toBeNull();
  });

  it("acks three permissions arriving within 50 ms with one frame naming all", () => {
    const s = new HoldingAckScheduler();
    s.onPermission(id(1), 0);
    expect(s.onTick(20)).toBeNull();
    s.onPermission(id(2), 20);
    expect(s.onTick(50)).toBeNull();
    s.onPermission(id(3), 50);
    expect(s.onTick(149)).toBeNull();
    expect(s.onTick(150)).toEqual([id(1), id(2), id(3)]);
  });

  it("acks a steady trickle every 90 ms no later than 500 ms after the first", () => {
    const s = new HoldingAckScheduler();
    let sentAt: number | null = null;
    let n = 0;
    for (let now = 0; now <= 1000 && sentAt === null; now += 10) {
      if (now % 90 === 0) s.onPermission(id(++n), now);
      if (s.onTick(now)) sentAt = now;
    }
    expect(sentAt).toBe(500);
  });

  it("sends a heartbeat every 4 s naming all held requests and stops when none is held", () => {
    const s = new HoldingAckScheduler();
    s.onPermission(id(1), 0);
    s.onPermission(id(2), 0);
    expect(s.onTick(100)).toEqual([id(1), id(2)]);
    expect(s.nextDueAt()).toBe(4100);
    expect(s.onTick(4099)).toBeNull();
    expect(s.onTick(4100)).toEqual([id(1), id(2)]);
    s.onRemove(id(1));
    expect(s.onTick(8100)).toEqual([id(2)]);
    s.onRemove(id(2));
    expect(s.nextDueAt()).toBeNull();
    expect(s.onTick(20_000)).toBeNull();
  });

  it("acks a new request quickly while others are already held, naming all of them", () => {
    const s = new HoldingAckScheduler();
    s.onPermission(id(1), 0);
    s.onTick(100);
    s.onPermission(id(2), 1000);
    expect(s.nextDueAt()).toBe(1100);
    expect(s.onTick(1100)).toEqual([id(2), id(1)]);
    expect(s.nextDueAt()).toBe(5100);
  });

  it("cancels a pending ack when the only unacknowledged request is removed", () => {
    const s = new HoldingAckScheduler();
    s.onPermission(id(1), 0);
    s.onRemove(id(1));
    expect(s.nextDueAt()).toBeNull();
    expect(s.onTick(100)).toBeNull();
  });

  it("acks a replayed request again", () => {
    const s = new HoldingAckScheduler();
    s.onPermission(id(1), 0);
    s.onTick(100);
    s.clear();
    expect(s.nextDueAt()).toBeNull();
    s.onPermission(id(1), 2000);
    expect(s.onTick(2100)).toEqual([id(1)]);
    s.onPermission(id(1), 3000);
    expect(s.onTick(3100)).toEqual([id(1)]);
    expect(s.ids).toEqual([id(1)]);
  });

  it("never names more than 8 ids", () => {
    const s = new HoldingAckScheduler();
    for (let i = 1; i <= 10; i++) s.onPermission(id(i), 0);
    expect(s.onTick(100)).toHaveLength(8);
  });

  it("accepts custom timings", () => {
    const s = new HoldingAckScheduler({ debounceMs: 10, maxDelayMs: 20, intervalMs: 50 });
    s.onPermission(id(1), 0);
    expect(s.onTick(10)).toEqual([id(1)]);
    expect(s.nextDueAt()).toBe(60);
  });
});
