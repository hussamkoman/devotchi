import { execFile } from "node:child_process";
import { existsSync } from "node:fs";
import net from "node:net";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { ApiClient } from "../src/api.js";
import { ConfigStore } from "../src/config.js";
import { ServerCredentialSource } from "../src/credentials.js";
import { CliError } from "../src/errors.js";
import { lanAddresses, startLocalServer, type LocalServer } from "../src/local.js";
import { makeSandbox, requireBuiltCli, startCli, waitForExit, type Sandbox } from "./helpers.js";

const run = promisify(execFile);
const clientDir = fileURLToPath(new URL("..", import.meta.url));

let box: Sandbox;
let store: ConfigStore;
let local: LocalServer | null = null;

beforeEach(async () => {
  box = await makeSandbox();
  store = new ConfigStore(box.env);
});

afterEach(async () => {
  await local?.server.close();
  local = null;
  await box.cleanup();
});

function connectRefused(host: string, port: number): Promise<boolean> {
  return new Promise((resolve) => {
    const socket = net.connect({ host, port });
    socket.once("connect", () => {
      socket.destroy();
      resolve(false);
    });
    socket.once("error", () => resolve(true));
  });
}

describe("--local", () => {
  it("importing the server package from the client starts nothing", async () => {
    expect(existsSync(join(clientDir, "../server/dist/app.js")), "run `pnpm run build` first").toBe(true);
    const script = `
      import net from "node:net";
      let listens = 0;
      const listen = net.Server.prototype.listen;
      net.Server.prototype.listen = function (...args) { listens++; return listen.apply(this, args); };
      const before = process.getActiveResourcesInfo().length;
      const mod = await import("@devotchi/server");
      const resources = process.getActiveResourcesInfo();
      process.stdout.write(JSON.stringify({ createServer: typeof mod.createServer, listens, resources, before }));
    `;
    const { stdout } = await run(process.execPath, ["--no-warnings", "--input-type=module", "-e", script], {
      cwd: clientDir,
      timeout: 10_000,
    });
    const result = JSON.parse(stdout) as { createServer: string; listens: number; resources: string[] };
    expect(result.createServer).toBe("function");
    expect(result.listens).toBe(0);
    expect(result.resources.filter((r) => r === "TCPServerWrap" || r === "Timeout")).toEqual([]);
  });

  it("runs the real server on the given port, stores it as the default and pairs a device against it", async () => {
    local = await startLocalServer({ store, lan: false, port: 0, logSink: () => undefined });
    expect(local.url).toBe(`http://127.0.0.1:${local.server.port}`);
    expect((await store.load()).serverUrl).toBe(local.url);
    expect(existsSync(join(box.configDir, "local.db"))).toBe(true);

    const source = new ServerCredentialSource(store, (await store.resolveServer()).url);
    const creds = await source.verify(await source.get());
    expect(creds.source).toBe("provisioned");
    expect(creds.token).not.toBe("local-token");

    const api = new ApiClient(local.url, { token: creds.token });
    const { code } = await api.createPairCode();
    const claim = await fetch(`${local.url}/api/pair/claim`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ code, deviceId: "dv-0a1b2c", fw: "0.2.0" }),
    });
    expect(claim.status).toBe(201);
    const paired = (await claim.json()) as { token: string; roomId: string; deviceId: string };
    expect(paired).toMatchObject({ roomId: creds.roomId, deviceId: "dv-0a1b2c" });
    const room = await api.room();
    expect(room.devices.map((d) => d.id)).toEqual(["dv-0a1b2c"]);
  });

  it("listens on loopback only without --lan", async () => {
    local = await startLocalServer({ store, lan: false, port: 0, logSink: () => undefined });
    const address = local.server.internals.http.address() as net.AddressInfo;
    expect(address.address).toBe("127.0.0.1");
    for (const ip of lanAddresses()) {
      expect(await connectRefused(ip, local.server.port), ip).toBe(true);
    }
  });

  it("listens on all interfaces with --lan", async () => {
    local = await startLocalServer({ store, lan: true, port: 0, logSink: () => undefined });
    expect((local.server.internals.http.address() as net.AddressInfo).address).toBe("0.0.0.0");
  });

  it("reports a port in use in one line that suggests --server-port", async () => {
    const blocker = net.createServer();
    await new Promise<void>((resolve) => blocker.listen(0, "127.0.0.1", resolve));
    const port = (blocker.address() as net.AddressInfo).port;
    try {
      const err = await startLocalServer({ store, lan: false, port, logSink: () => undefined }).catch((e: unknown) => e);
      expect(err).toBeInstanceOf(CliError);
      expect((err as Error).message).toBe(
        `port ${port} is already in use; start the local server on another port with --server-port <n>`,
      );
      expect((await store.load()).serverUrl).toBe("http://localhost:8080");
    } finally {
      await new Promise((resolve) => blocker.close(resolve));
    }
  });

  it("devotchi start --local serves the server and dashboard without a producer, and stops on SIGTERM", async () => {
    requireBuiltCli();
    const started = await startCli(
      ["start", "--local", "--server-port", "0", "--port", "0", "--no-open"],
      box.env,
      /Dashboard: http:\/\/localhost:(\d+)/,
    );
    try {
      const serverUrl = started.stdout().match(/Local server: (http:\/\/127\.0\.0\.1:\d+)/)![1]!;
      const health = (await (await fetch(`${serverUrl}/health`)).json()) as { proto: number; mode: string };
      expect(health).toMatchObject({ proto: 2, mode: "single-user" });
      expect((await store.load()).serverUrl).toBe(serverUrl);
      const creds = (await store.getCredentials(serverUrl))!;
      const room = await new ApiClient(serverUrl, { token: creds.token }).room();
      expect(room.peers.producers).toBe(0);
      const page = await fetch(`http://127.0.0.1:${started.match[1]}/`);
      expect(page.status).toBe(200);
      expect(started.stderr()).not.toContain("ExperimentalWarning");
    } finally {
      started.child.kill("SIGTERM");
      expect(await waitForExit(started.child)).toBe(0);
    }
  });
});
