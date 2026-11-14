import { describe, expect, it } from "vitest";
import { DEFAULT_SERVER_TIMING } from "@devotchi/shared";
import { ConfigError, loadConfig, resolveConfig } from "../src/config.js";

const base = { HOME: "/tmp/devotchi-config-test" };

function error(env: Record<string, string>): string {
  try {
    loadConfig({ ...base, ...env });
  } catch (err) {
    expect(err).toBeInstanceOf(ConfigError);
    expect((err as Error).message).not.toContain("\n");
    return (err as Error).message;
  }
  throw new Error("expected a ConfigError");
}

describe("loadConfig", () => {
  it("uses the defaults of architecture 2.6", () => {
    const { config, warnings } = loadConfig(base);
    expect(config).toMatchObject({
      host: "0.0.0.0",
      port: 8080,
      mode: "single-user",
      corsOrigins: [],
      trustProxy: false,
      registration: "open",
      logLevel: "info",
      maxSockets: 200,
    });
    expect(config.dbPath.endsWith("/.devotchi/devotchi.db")).toBe(true);
    expect(config.timing).toEqual(DEFAULT_SERVER_TIMING);
    expect(warnings).toEqual([]);
  });

  it("reads every variable", () => {
    const { config } = loadConfig({
      ...base,
      DEVOTCHI_HOST: "127.0.0.1",
      DEVOTCHI_PORT: "18123",
      DEVOTCHI_MODE: "multi-user",
      DEVOTCHI_DB_PATH: "/tmp/x.db",
      DEVOTCHI_CORS_ORIGINS: "https://a.example, http://localhost:5173",
      DEVOTCHI_TRUST_PROXY: "1",
      DEVOTCHI_ADMIN_SECRET: "admin",
      DEVOTCHI_REGISTRATION: "invite",
      DEVOTCHI_INVITE_CODE: "join",
      DEVOTCHI_PERMISSION_HOLD_MS: "60000",
      DEVOTCHI_PERMISSION_ACK_MS: "2000",
      DEVOTCHI_PERMISSION_ALIVE_MS: "10000",
      DEVOTCHI_LOG_LEVEL: "debug",
    });
    expect(config).toMatchObject({
      host: "127.0.0.1",
      port: 18123,
      mode: "multi-user",
      dbPath: "/tmp/x.db",
      corsOrigins: ["https://a.example", "http://localhost:5173"],
      trustProxy: true,
      adminSecret: "admin",
      registration: "invite",
      inviteCode: "join",
      logLevel: "debug",
    });
    expect(config.timing).toMatchObject({ holdMs: 60000, ackMs: 2000, aliveMs: 10000 });
  });

  it("validates with one-line errors", () => {
    expect(error({ DEVOTCHI_PORT: "abc" })).toContain("DEVOTCHI_PORT");
    expect(error({ DEVOTCHI_PORT: "70000" })).toContain("0..65535");
    expect(error({ DEVOTCHI_PORT: "-1" })).toContain("DEVOTCHI_PORT");
    expect(error({ DEVOTCHI_MODE: "multi" })).toContain("single-user, multi-user");
    expect(error({ DEVOTCHI_PERMISSION_HOLD_MS: "9999" })).toContain("10000..300000");
    expect(error({ DEVOTCHI_PERMISSION_HOLD_MS: "300001" })).toContain("10000..300000");
    expect(error({ DEVOTCHI_HOST: "not a host!" })).toContain("DEVOTCHI_HOST");
    expect(error({ DEVOTCHI_TRUST_PROXY: "yes" })).toContain("0 or 1");
    expect(error({ DEVOTCHI_CORS_ORIGINS: "https://a.example/path" })).toContain("origin");
    expect(error({ DEVOTCHI_LOG_LEVEL: "verbose" })).toContain("DEVOTCHI_LOG_LEVEL");
    expect(error({ DEVOTCHI_REGISTRATION: "maybe" })).toContain("DEVOTCHI_REGISTRATION");
    expect(error({ DEVOTCHI_MODE: "multi-user", DEVOTCHI_REGISTRATION: "invite" })).toContain("DEVOTCHI_INVITE_CODE");
  });

  it("accepts the hold range bounds and port 0", () => {
    expect(loadConfig({ ...base, DEVOTCHI_PERMISSION_HOLD_MS: "10000" }).config.timing.holdMs).toBe(10000);
    expect(loadConfig({ ...base, DEVOTCHI_PERMISSION_HOLD_MS: "300000" }).config.timing.holdMs).toBe(300000);
    expect(loadConfig({ ...base, DEVOTCHI_PORT: "0" }).config.port).toBe(0);
  });

  it("ignores DEVOTCHI_JWT_SECRET with a warning", () => {
    const { warnings } = loadConfig({ ...base, DEVOTCHI_JWT_SECRET: "old" });
    expect(warnings).toEqual(["DEVOTCHI_JWT_SECRET is no longer used and is ignored"]);
  });
});

describe("resolveConfig", () => {
  it("fills embedding defaults: loopback and the protocol timing", () => {
    const config = resolveConfig({ dbPath: ":memory:" });
    expect(config.host).toBe("127.0.0.1");
    expect(config.timing).toEqual(DEFAULT_SERVER_TIMING);
  });

  it("rejects invalid programmatic values", () => {
    expect(() => resolveConfig({ dbPath: "" })).toThrow(ConfigError);
    expect(() => resolveConfig({ dbPath: "x", port: 1.5 })).toThrow(ConfigError);
    expect(() => resolveConfig({ dbPath: "x", timing: { heartbeatDeadMs: 0 } })).toThrow(ConfigError);
    expect(() => resolveConfig({ dbPath: "x", mode: "solo" as never })).toThrow(ConfigError);
  });
});
