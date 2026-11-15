import { describe, expect, it } from "vitest";
import type { Room, ServerToConsumerMessage } from "@devotchi/shared";
import { LOG_MAX, SEND_TIMEOUT_MS, initialState, reduce, type Action, type State } from "../../web/src/store.js";

const id = (n: number) => n.toString(16).padStart(32, "0");
const T0 = 1_800_000_000_000;

function permission(n: number, extra: Partial<Extract<ServerToConsumerMessage, { type: "permission" }>> = {}): ServerToConsumerMessage {
  return { type: "permission", id: id(n), tool: "Bash", summary: `cmd ${n}`, risk: "low", timeout: 30_000, ...extra };
}

function run(state: State, ...actions: Action[]): State {
  return actions.reduce(reduce, state);
}

const frame = (msg: ServerToConsumerMessage, now = T0): Action => ({ type: "frame", msg, now });
const ack: ServerToConsumerMessage = {
  type: "register_ack",
  role: "consumer",
  roomId: "5e140e256d56",
  proto: 2,
  serverVersion: "0.2.0",
  serverTime: T0,
  mode: "ask",
  peers: { producers: 1, consumers: 1, devices: 0 },
};
const open = () => run(initialState(), frame(ack));

describe("store reducer", () => {
  it("keeps several requests in arrival order with local deadlines", () => {
    const s = run(open(), frame(permission(1), T0), frame(permission(2, { timeout: 5000 }), T0 + 10), frame(permission(3), T0 + 20));
    expect(s.requests.map((r) => r.id)).toEqual([id(1), id(2), id(3)]);
    expect(s.requests[1].deadline).toBe(T0 + 10 + 5000);
    expect(s.connection.phase).toBe("open");
  });

  it("a replayed permission updates the deadline but keeps the position", () => {
    const s = run(open(), frame(permission(1)), frame(permission(2)), frame(permission(1, { timeout: 4000 }), T0 + 100));
    expect(s.requests.map((r) => r.id)).toEqual([id(1), id(2)]);
    expect(s.requests[0].deadline).toBe(T0 + 4100);
  });

  it("removes a request only on dismiss, with a note per reason", () => {
    let s = run(
      open(),
      { type: "devices", devices: [{ id: "dv-e356e4", name: "desk" } as State["devices"][number]], now: T0 },
      frame(permission(1)),
      frame(permission(2)),
      frame(permission(3)),
      frame(permission(4)),
    );
    s = run(
      s,
      frame({ type: "dismiss", id: id(1), reason: "answered", outcome: "allow", by: "dv-e356e4" }),
      frame({ type: "dismiss", id: id(2), reason: "cancelled" }),
      frame({ type: "dismiss", id: id(3), reason: "timeout" }),
    );
    expect(s.requests.map((r) => r.id)).toEqual([id(4)]);
    const notes = s.toasts.map((t) => t.text);
    expect(notes).toEqual(["Bash: allowed, answered on desk", "Bash: handled in terminal", "Bash: time ran out, sent to terminal"]);
    s = run(s, frame({ type: "dismiss", id: id(4), reason: "released" }), frame({ type: "dismiss", id: id(9), reason: "released" }));
    expect(s.requests).toEqual([]);
    expect(s.toasts.at(-1)?.text).toBe("Bash: handed to the terminal");
  });

  it("an error with id UNKNOWN_REQUEST removes that request; INVALID_MESSAGE re-enables it", () => {
    let s = run(open(), frame(permission(1)), frame(permission(2)), { type: "send", id: id(1), reply: { kind: "allow" }, now: T0 }, { type: "send", id: id(2), reply: { kind: "deny" }, now: T0 });
    expect(s.requests.every((r) => r.sending !== null)).toBe(true);
    s = run(
      s,
      frame({ type: "error", code: "UNKNOWN_REQUEST", message: "not pending", id: id(1) }),
      frame({ type: "error", code: "INVALID_MESSAGE", message: "granted must be a boolean", id: id(2) }),
    );
    expect(s.requests.map((r) => r.id)).toEqual([id(2)]);
    expect(s.requests[0]).toMatchObject({ sending: null, error: "granted must be a boolean" });
    expect(s.toasts.map((t) => t.text)).toContain("Bash: no longer pending");
  });

  it("drops a request at its local deadline and flags a send without a dismiss after 3 s", () => {
    let s = run(open(), frame(permission(1, { timeout: 2000 })), frame(permission(2)), { type: "send", id: id(2), reply: { kind: "defer" }, now: T0 + 100 });
    s = run(s, { type: "tick", now: T0 + 1999 });
    expect(s.requests).toHaveLength(2);
    s = run(s, { type: "tick", now: T0 + 2000 });
    expect(s.requests.map((r) => r.id)).toEqual([id(2)]);
    expect(s.requests[0].sending).not.toBeNull();
    s = run(s, { type: "tick", now: T0 + 100 + SEND_TIMEOUT_MS });
    expect(s.requests[0]).toMatchObject({ sending: null, error: expect.stringContaining("no confirmation") });
  });

  it("status never hides requests and a status without a mood keeps the mood", () => {
    let s = run(open(), frame(permission(1)), frame({ type: "mood", mood: "anxious" }));
    s = run(s, frame({ type: "status", msg: "Edit a.c", project: "devotchi" }));
    expect(s.requests).toHaveLength(1);
    expect(s.pet).toEqual({ mood: "anxious", status: "Edit a.c", project: "devotchi" });
    s = run(s, frame({ type: "status", msg: "Run tests", mood: "working" }));
    expect(s.pet.mood).toBe("working");
    s = run(s, frame({ type: "notify", event: "tests_passed", mood: "happy" }));
    expect(s.pet.mood).toBe("happy");
    s = run(s, frame({ type: "status", msg: "next" }));
    expect(s.pet.mood).toBe("happy");
    expect(s.requests).toHaveLength(1);
  });

  it("resets requests and peers when the socket closes and on a new register_ack", () => {
    let s = run(open(), frame(permission(1)));
    s = run(s, { type: "connection", phase: "reconnecting", retryAt: T0 + 2000, reason: "connection lost", now: T0 });
    expect(s.requests).toEqual([]);
    expect(s.peers).toBeNull();
    expect(s.connection).toEqual({ phase: "reconnecting", retryAt: T0 + 2000, reason: "connection lost" });
    s = run(s, { type: "connection", phase: "connecting", now: T0 + 2000 }, frame(ack, T0 + 2100), frame(permission(1), T0 + 2200));
    expect(s.connection.phase).toBe("open");
    expect(s.requests.map((r) => r.id)).toEqual([id(1)]);
  });

  it("caps the log, uses server timestamps and applies mode, room and settings", () => {
    let s = open();
    for (let i = 0; i < LOG_MAX + 20; i++) s = reduce(s, frame({ type: "status", msg: `s${i}`, timestamp: "2026-11-14T14:30:00.000Z" }));
    expect(s.log).toHaveLength(LOG_MAX);
    expect(s.log[0]).toEqual({ at: "2026-11-14T14:30:00.000Z", kind: "status", text: `s${LOG_MAX + 19}` });
    const room = {
      settings: { mode: "ask", holdMs: 45_000, shareText: false },
      diagnostics: { dialogWhilePending: { count: 2, lastSeenAt: null } },
      devices: [],
    } as unknown as Room;
    s = run(s, { type: "room", room, now: T0 }, frame({ type: "mode", mode: "desk" }));
    expect(s.settings).toEqual({ mode: "desk", holdMs: 45_000, shareText: false });
    expect(s.mode).toBe("desk");
    expect(s.diagnostics?.dialogWhilePending.count).toBe(2);
  });

  it("toasts expire on tick", () => {
    let s = run(open(), { type: "toast", text: "hello", now: T0 });
    expect(s.toasts).toHaveLength(1);
    s = run(s, { type: "tick", now: T0 + 60_000 });
    expect(s.toasts).toEqual([]);
  });
});
