import { spawn } from "node:child_process";
import { mkdir, readFile, readdir, stat, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { startTestServer, type TestServer } from "../../server/test/helpers.js";
import { ConfigStore, normaliseServerUrl } from "../src/config.js";
import { ServerCredentialSource } from "../src/credentials.js";
import { ConfigError, LoginRequiredError, UsageError } from "../src/errors.js";
import { makeSandbox, requireBuiltCli, runCli, unusedPort, type Sandbox } from "./helpers.js";

let box: Sandbox;
let store: ConfigStore;

beforeEach(async () => {
  box = await makeSandbox();
  store = new ConfigStore(box.env);
});

afterEach(async () => {
  await box.cleanup();
});

const configFile = () => join(box.configDir, "config.json");

async function writeConfig(content: string): Promise<void> {
  await mkdir(box.configDir, { recursive: true });
  await writeFile(configFile(), content);
}

function tokenCount(t: TestServer): number {
  return (t.server.internals.db.get("SELECT COUNT(*) AS n FROM tokens") as { n: number }).n;
}

describe("normaliseServerUrl", () => {
  it.each([
    ["http://localhost:8080", "http://localhost:8080"],
    ["http://localhost:8080/", "http://localhost:8080"],
    ["ws://Example.COM:80/", "http://example.com"],
    ["wss://example.com", "https://example.com"],
    ["https://example.com:443//", "https://example.com"],
    ["10.0.0.5:18100", "http://10.0.0.5:18100"],
    ["https://example.com/devotchi/", "https://example.com/devotchi"],
  ])("%s -> %s", (input, expected) => {
    expect(normaliseServerUrl(input)).toBe(expected);
  });

  it.each(["ftp://example.com", "http://example.com/?token=x", "http://user:pw@example.com", "", "http://"])(
    "rejects %j",
    (input) => {
      expect(() => normaliseServerUrl(input)).toThrow(UsageError);
    },
  );
});

describe("config file", () => {
  it("reads defaults when there is no file and creates nothing", async () => {
    const config = await store.load();
    expect(config).toEqual({ version: 2, serverUrl: "http://localhost:8080", webPort: 3456, servers: {} });
    await expect(stat(box.configDir)).rejects.toThrow();
  });

  it("migrates a v1 file, keeps the original as .v1.bak and drops pairingCode and localMode", async () => {
    const v1 = JSON.stringify({
      serverUrl: "http://LOCALHOST:8080/",
      token: "11111111-1111-4111-8111-111111111111",
      roomId: "5e140e256d56",
      pairingCode: "ABCDEF",
      webPort: 4000,
      localMode: false,
    });
    await writeConfig(v1);
    const config = await store.load();
    expect(config).toEqual({
      version: 2,
      serverUrl: "http://localhost:8080",
      webPort: 4000,
      servers: { "http://localhost:8080": { token: "11111111-1111-4111-8111-111111111111", roomId: "5e140e256d56" } },
    });
    const written = JSON.parse(await readFile(configFile(), "utf8"));
    expect(written).toEqual(config);
    expect(written).not.toHaveProperty("pairingCode");
    expect(written).not.toHaveProperty("localMode");
    expect(await readFile(configFile() + ".v1.bak", "utf8")).toBe(v1);
  });

  it("never persists the fake v1 local-broker token", async () => {
    await writeConfig(JSON.stringify({ serverUrl: "http://localhost:3457", token: "local-token", roomId: "local", localMode: true }));
    const config = await store.load();
    expect(config.servers).toEqual({});
    expect(await readFile(configFile(), "utf8")).not.toContain("local-token");
  });

  it.each([
    ["not JSON", "{ this is not json"],
    ["not an object", "[1, 2]"],
    ["a bad field", JSON.stringify({ version: 2, serverUrl: "http://x", webPort: "3456", servers: {} })],
    ["a bad server URL", JSON.stringify({ version: 2, serverUrl: "gopher://x", webPort: 3456, servers: {} })],
    ["an unknown version", JSON.stringify({ version: 3 })],
  ])("throws ConfigError (exit 2) for %s and never rewrites the file", async (_name, content) => {
    await writeConfig(content);
    const err = await store.load().catch((e: unknown) => e);
    expect(err).toBeInstanceOf(ConfigError);
    expect((err as ConfigError).exitCode).toBe(2);
    expect((err as Error).message).toContain(configFile());
    expect((err as Error).message).not.toContain("\n");
    await expect(store.setCredentials("http://a", { token: "t", roomId: "r" })).rejects.toBeInstanceOf(ConfigError);
    expect(await readFile(configFile(), "utf8")).toBe(content);
    expect((await readdir(box.configDir)).sort()).toEqual(["config.json"]);
  });

  it("a corrupt file makes the CLI exit 2 with one line", async () => {
    requireBuiltCli();
    await writeConfig("{ broken");
    const run = await runCli(["config"], box.env);
    expect(run.code).toBe(2);
    expect(run.stderr.trim().split("\n")).toHaveLength(1);
    expect(run.stderr).toMatch(/^devotchi: .*config\.json is not valid JSON/);
    expect(await readFile(configFile(), "utf8")).toBe("{ broken");
  });

  it("writes atomically with mode 0600 in a 0700 directory and leaves no temp files", async () => {
    await store.setCredentials("http://127.0.0.1:18101", { token: "a", roomId: "r1" });
    expect((await stat(configFile())).mode & 0o777).toBe(0o600);
    expect((await stat(box.configDir)).mode & 0o777).toBe(0o700);
    expect((await readdir(box.configDir)).sort()).toEqual(["config.json"]);
  });

  it("serialises concurrent updates through the lock so none is lost", async () => {
    const stores = Array.from({ length: 4 }, () => new ConfigStore(box.env));
    await Promise.all(
      Array.from({ length: 24 }, (_, i) =>
        stores[i % 4]!.setCredentials(`http://host-${i}.test`, { token: `t${i}`, roomId: `r${i}` }),
      ),
    );
    const config = await store.load();
    expect(Object.keys(config.servers)).toHaveLength(24);
    expect(config.servers["http://host-7.test"]).toEqual({ token: "t7", roomId: "r7" });
    expect((await readdir(box.configDir)).sort()).toEqual(["config.json"]);
  });

  it("takes over a lock left behind by a dead process", async () => {
    await mkdir(box.configDir, { recursive: true });
    const dead = spawn(process.execPath, ["-e", ""]);
    const pid = dead.pid!;
    await new Promise((resolve) => dead.on("exit", resolve));
    await writeFile(join(box.configDir, "config.lock"), `${pid} ${Date.now()} abcd`);
    await store.setCredentials("http://a.test", { token: "t", roomId: "r" });
    expect((await store.getCredentials("http://a.test"))?.token).toBe("t");
  });

  it("stores credentials per server URL and looks them up in normalised form", async () => {
    await store.setCredentials("http://one.test:18102/", { token: "one", roomId: "r1" });
    await store.setCredentials("wss://two.test", { token: "two", roomId: "r2" });
    expect(await store.getCredentials("http://one.test:18102")).toEqual({ token: "one", roomId: "r1" });
    expect(await store.getCredentials("https://two.test/")).toEqual({ token: "two", roomId: "r2" });
    expect(await store.getCredentials("http://three.test")).toBeNull();
    expect(await store.removeCredentials("http://one.test:18102")).toBe(true);
    expect(await store.getCredentials("http://one.test:18102")).toBeNull();
  });

  it("keeps fields written by a newer client", async () => {
    await writeConfig(JSON.stringify({ version: 2, serverUrl: "http://x.test", webPort: 3456, servers: {}, future: { a: 1 } }));
    await store.setCredentials("http://x.test", { token: "t", roomId: "r" });
    expect(JSON.parse(await readFile(configFile(), "utf8")).future).toEqual({ a: 1 });
  });
});

describe("environment overrides", () => {
  it("DEVOTCHI_SERVER_URL beats the file and --server beats both", async () => {
    await store.update((c) => {
      c.serverUrl = "http://file.test";
    });
    expect(await store.resolveServer()).toEqual({ url: "http://file.test", source: "config" });
    const envStore = new ConfigStore({ ...box.env, DEVOTCHI_SERVER_URL: "ws://env.test:18103/" });
    expect(await envStore.resolveServer()).toEqual({ url: "http://env.test:18103", source: "env" });
    expect(await envStore.resolveServer("https://flag.test")).toEqual({ url: "https://flag.test", source: "flag" });
  });

  it("DEVOTCHI_TOKEN is used without contacting the server and is never written", async () => {
    const envStore = new ConfigStore({ ...box.env, DEVOTCHI_TOKEN: "env-token-value" });
    const port = await unusedPort();
    const creds = await new ServerCredentialSource(envStore, `http://127.0.0.1:${port}`).get();
    expect(creds).toEqual({ token: "env-token-value", roomId: "", source: "env" });
    await expect(stat(configFile())).rejects.toThrow();
  });
});

describe("ensureToken", () => {
  let t: TestServer;

  afterEach(async () => {
    await t?.close();
  });

  it("provisions once, stores the token and reuses it", async () => {
    t = await startTestServer();
    const source = new ServerCredentialSource(store, t.url);
    const first = await source.get();
    expect(first.source).toBe("provisioned");
    expect(first.token).toMatch(/^[0-9a-f-]{36}$/);
    const again = await new ServerCredentialSource(new ConfigStore(box.env), t.url).get();
    expect(again).toEqual({ token: first.token, roomId: first.roomId, source: "config" });
    expect(tokenCount(t)).toBe(1);
  });

  it("two concurrent first runs in one process create one token", async () => {
    t = await startTestServer();
    const results = await Promise.all(
      Array.from({ length: 5 }, () => new ServerCredentialSource(new ConfigStore(box.env), t.url).get()),
    );
    expect(new Set(results.map((r) => r.token)).size).toBe(1);
    expect(tokenCount(t)).toBe(1);
  });

  it("concurrent first runs in separate processes create one room", async () => {
    requireBuiltCli();
    t = await startTestServer();
    const runs = await Promise.all(
      Array.from({ length: 3 }, () => runCli(["pair", "--new", "--server", t.url], box.env)),
    );
    for (const run of runs) expect(run.code, run.stderr).toBe(0);
    expect(tokenCount(t)).toBe(1);
    const config = await store.load();
    expect(Object.keys(config.servers)).toEqual([t.url]);
  });

  it("replaces a token the server rejects (single-user) exactly once", async () => {
    t = await startTestServer();
    await store.setCredentials(t.url, { token: "00000000-0000-4000-8000-000000000000", roomId: "000000000000" });
    const source = new ServerCredentialSource(store, t.url);
    const stale = await source.get();
    const renewed = await source.verify(stale);
    expect(renewed.source).toBe("provisioned");
    expect(renewed.token).not.toBe(stale.token);
    expect((await store.getCredentials(t.url))?.token).toBe(renewed.token);
  });

  it("asks for devotchi login on a multi-user server", async () => {
    t = await startTestServer({ mode: "multi-user", bcryptCost: 4 });
    const err = await new ServerCredentialSource(store, t.url).get().catch((e: unknown) => e);
    expect(err).toBeInstanceOf(LoginRequiredError);
    expect((err as Error).message).toContain("devotchi login");
    expect(await store.getCredentials(t.url)).toBeNull();
  });
});
