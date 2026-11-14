import { mkdir, readFile, readdir, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { labelFor } from "../src/commands/run.js";
import { notifyBody, parseHoldSeconds } from "../src/commands/room.js";
import { UsageError } from "../src/errors.js";
import { ago, minutesSeconds } from "../src/format.js";
import { parseBattery, parsePolicy } from "../src/sim.js";
import { makeSandbox, requireBuiltCli, runCli, unusedPort, type Sandbox } from "./helpers.js";

let box: Sandbox;

beforeAll(async () => {
  requireBuiltCli();
  box = await makeSandbox();
});

afterAll(() => box.cleanup());

const COMMANDS = [
  "start", "mcp", "setup", "pair", "devices", "devices list", "devices rename", "devices identify", "devices rm",
  "status", "doctor", "mode", "hold", "notify", "run", "sim", "login", "logout", "config", "config get", "config set", "config path",
];

describe("devotchi CLI", () => {
  it("every command has --help text", async () => {
    for (const command of COMMANDS) {
      const result = await runCli([...command.split(" "), "--help"], box.env);
      expect(result.code, command).toBe(0);
      expect(result.stdout, command).toMatch(new RegExp(`Usage: devotchi ${command}`));
      expect(result.stdout.split("\n")[2]!.length, command).toBeGreaterThan(10);
    }
  });

  it("an unknown command prints the help and exits 2 instead of starting the dashboard", async () => {
    const result = await runCli(["satus"], box.env);
    expect(result.code).toBe(2);
    expect(result.stderr).toMatch(/unknown command 'satus'/);
    expect(result.stderr).toMatch(/Commands:/);
  });

  it("rejects bad numeric options with exit code 2", async () => {
    expect((await runCli(["hold", "5"], box.env)).code).toBe(2);
    expect((await runCli(["sim", "--heartbeat-interval", "x"], box.env)).code).toBe(2);
    expect((await runCli(["notify", "x", "--urgency", "7"], box.env)).code).toBe(2);
  });

  it("config get/set/path", async () => {
    expect((await runCli(["config", "path"], box.env)).stdout.trim()).toBe(`${box.configDir}/config.json`);
    expect((await runCli(["config", "set", "serverUrl", "wss://example.com/"], box.env)).stdout).toMatch(/https:\/\/example.com/);
    expect((await runCli(["config", "get", "serverUrl"], box.env)).stdout.trim()).toBe("https://example.com");
    expect((await runCli(["config", "set", "token", "x"], box.env)).code).toBe(2);
  });
});

describe("devotchi doctor", () => {
  it("never writes: a v1 file stays as it is and the checks after an unreachable server are skipped", async () => {
    const own = await makeSandbox();
    try {
      await mkdir(own.configDir, { recursive: true });
      const v1 = JSON.stringify({ serverUrl: `http://127.0.0.1:${await unusedPort()}`, token: "abc", roomId: "r" });
      await writeFile(join(own.configDir, "config.json"), v1);
      const result = await runCli(["doctor", "--dir", own.dir, "--json"], own.env);
      expect(result.code).toBe(1);
      const { checks } = JSON.parse(result.stdout) as { checks: { id: string; level: string }[] };
      expect(checks.map((c) => `${c.id}:${c.level}`).slice(0, 4)).toEqual(["config:PASS", "health:FAIL", "token:SKIP", "websocket:SKIP"]);
      expect(await readFile(join(own.configDir, "config.json"), "utf8")).toBe(v1);
      expect(await readdir(own.configDir)).toEqual(["config.json"]);
    } finally {
      await own.cleanup();
    }
  });
});

describe("command helpers", () => {
  it("labelFor sanitises the command name for the event", () => {
    expect(labelFor("/usr/bin/pnpm")).toBe("pnpm");
    expect(labelFor("Unit Tests")).toBe("unit_tests");
    expect(labelFor("./build.sh")).toBe("build");
    expect(labelFor("---")).toBe("command");
    expect(`${labelFor("x".repeat(80))}_succeeded`.length).toBeLessThanOrEqual(40);
  });

  it("notifyBody validates event and mood and maps --urgent to urgency 2", () => {
    expect(notifyBody("tests_passed", "47 ok", { urgent: true, mood: "proud" })).toEqual({
      event: "tests_passed",
      detail: "47 ok",
      mood: "proud",
      urgency: 2,
    });
    expect(() => notifyBody("Bad", undefined, {})).toThrow(UsageError);
    expect(() => notifyBody("ok", undefined, { mood: "grumpy" })).toThrow(UsageError);
  });

  it("parseHoldSeconds accepts 10..300", () => {
    expect(parseHoldSeconds("10")).toBe(10);
    expect(parseHoldSeconds("300")).toBe(300);
    expect(() => parseHoldSeconds("301")).toThrow(UsageError);
    expect(() => parseHoldSeconds("9")).toThrow(UsageError);
  });

  it("parses sim policies and battery scripts", () => {
    expect(parsePolicy("after=250")).toEqual({ kind: "after", ms: 250 });
    expect(parsePolicy("none")).toEqual({ kind: "none" });
    expect(() => parsePolicy("sometimes")).toThrow(UsageError);
    expect(parseBattery("15,discharging")).toEqual({ pct: 15, state: "discharging" });
    expect(parseBattery("absent")).toEqual({ pct: null, state: "absent" });
    expect(() => parseBattery("101,full")).toThrow(UsageError);
  });

  it("formats durations and ages", () => {
    expect(minutesSeconds(252_000)).toBe("4m12s");
    expect(minutesSeconds(400)).toBe("0m00s");
    expect(ago(null)).toBe("never");
    expect(ago(5)).toBe("5 s ago");
    expect(ago(600)).toBe("10 min ago");
  });
});
