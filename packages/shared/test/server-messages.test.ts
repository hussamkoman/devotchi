import { describe, expect, it } from "vitest";
import { consumerMood, parseServerMessage } from "../src/server-messages.js";

const ID = "9f2c4e6a8b0d1f3a5c7e9b1d3f5a7c9e";
const permission = { type: "permission", id: ID, tool: "Bash", summary: "ls", risk: "low", timeout: 30000 };

describe("parseServerMessage", () => {
  it("accepts well-formed server frames", () => {
    for (const msg of [
      permission,
      { type: "dismiss", id: ID, reason: "answered", outcome: "allow", by: "dashboard" },
      { type: "dismiss", id: ID, reason: "timeout" },
      { type: "status", msg: "Edit a.c" },
      { type: "peers", producers: 1, consumers: 2, devices: 1 },
      { type: "mode", mode: "desk" },
      { type: "error", code: "UNKNOWN_REQUEST", message: "not pending", id: ID },
      { type: "ping" },
      { type: "register_ack", role: "consumer", roomId: "5e140e256d56", proto: 2, mode: "ask", peers: { producers: 0, consumers: 1, devices: 0 } },
    ]) {
      expect(parseServerMessage(JSON.stringify(msg)), msg.type).toEqual(msg);
    }
  });

  it("rejects malformed frames", () => {
    for (const raw of [
      "not json",
      "[]",
      JSON.stringify({ type: "holding", ids: [ID] }),
      JSON.stringify({ ...permission, timeout: "30000" }),
      JSON.stringify({ ...permission, risk: "extreme" }),
      JSON.stringify({ ...permission, summary: undefined }),
      JSON.stringify({ type: "dismiss", id: ID, reason: "answered" }),
      JSON.stringify({ type: "dismiss", id: ID, reason: "gone" }),
      JSON.stringify({ type: "status", msg: 3 }),
      JSON.stringify({ type: "mode", mode: "auto" }),
      JSON.stringify({ type: "error", code: "NO_PRODUCER", message: "x" }),
    ]) {
      expect(parseServerMessage(raw), raw).toBeNull();
    }
  });

  it("maps unknown moods to idle and keeps an absent status mood absent", () => {
    expect(parseServerMessage(JSON.stringify({ type: "mood", mood: "giddy" }))).toEqual({ type: "mood", mood: "idle" });
    expect(parseServerMessage(JSON.stringify({ type: "status", msg: "x" }))).not.toHaveProperty("mood");
    expect(consumerMood("happy")).toBe("happy");
  });
});
