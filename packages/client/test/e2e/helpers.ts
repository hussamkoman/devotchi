// End-to-end fixtures: a real server on an ephemeral port, a sandboxed CLI
// configuration that points at it, and simulated devices (in-process or as
// `devotchi sim` child processes).

import { spawn, type ChildProcess } from "node:child_process";
import { createInterface } from "node:readline";
import type { ServerTiming } from "@devotchi/shared";
import { startTestServer, type Json, type TestServer } from "../../../server/test/helpers.js";
import { hook, permissionInput, postHook } from "../../../server/test/hook-helpers.js";
import { ConfigStore } from "../../src/config.js";
import { Sim, type SimEvent, type SimOptions } from "../../src/sim.js";
import { CLIENT_VERSION } from "../../src/version.js";
import { CLI_PATH, makeSandbox, runCli, until, type CliResult, type Sandbox } from "../helpers.js";

/** Short timers so that every liveness path finishes within a few seconds. */
export const E2E_TIMING: Partial<ServerTiming> = {
  ackMs: 700,
  aliveMs: 1500,
  holdWriteMs: 0,
  deviceWriteMs: 0,
  hookStatusIntervalMs: 0,
};
/** Sims heartbeat well inside ALIVE. */
export const SIM_HEARTBEAT_MS = 300;

export interface World {
  t: TestServer;
  box: Sandbox;
  token: string;
  roomId: string;
  sims: Sim[];
  children: ChildProcess[];
  cli(args: string[], timeoutMs?: number): Promise<CliResult>;
  /** POST /hook/event; resolves with the JSON reply. */
  hook(body: Json): Promise<unknown>;
  permission(fields?: Json): Promise<unknown>;
  post(body: Json, init?: RequestInit): Promise<Response>;
  close(): Promise<void>;
}

export interface WorldOptions {
  timing?: Partial<ServerTiming>;
  /** Store the room token in the CLI config (default true); false lets the CLI provision its own. */
  storeToken?: boolean;
  server?: Parameters<typeof startTestServer>[0];
}

export async function makeWorld(options: WorldOptions = {}): Promise<World> {
  const t = await startTestServer({ ...options.server, timing: { ...E2E_TIMING, ...options.timing } });
  const box = await makeSandbox({ DEVOTCHI_SERVER_URL: t.url });
  const minted = t.cliToken("e2e");
  if (options.storeToken !== false) {
    await new ConfigStore(box.env).setCredentials(t.url, { token: minted.token, roomId: minted.roomId });
  }
  const world: World = {
    t,
    box,
    token: minted.token,
    roomId: minted.roomId,
    sims: [],
    children: [],
    cli: (args, timeoutMs) => runCli(args, box.env, box.dir, timeoutMs),
    hook: (body) => hook(t, world.token, body),
    permission: (fields = {}) => hook(t, world.token, permissionInput(fields)),
    post: (body, init) => postHook(t, world.token, body, init),
    async close() {
      await Promise.all(world.sims.map((sim) => sim.stop()));
      for (const child of world.children) if (child.exitCode === null && child.signalCode === null) child.kill("SIGKILL");
      await t.close();
      await box.cleanup();
    },
  };
  return world;
}

export interface SimHandle {
  sim: Sim;
  events: SimEvent[];
  next(event: string, match?: (e: SimEvent) => boolean, timeoutMs?: number): Promise<SimEvent>;
  sent(type: string): Json[];
}

function eventWaiter(events: SimEvent[]) {
  return async (event: string, match: (e: SimEvent) => boolean = () => true, timeoutMs = 5000): Promise<SimEvent> => {
    let found: SimEvent | undefined;
    await until(
      () => {
        found = events.find((e) => e.event === event && match(e));
        return found !== undefined;
      },
      timeoutMs,
      `sim event ${event}`,
    );
    return found!;
  };
}

/** An in-process sim with the room's cli token unless `token`/`code` is given. */
export async function startSim(
  world: World,
  options: Partial<Omit<SimOptions, "emit" | "serverUrl" | "version">> = {},
  waitRegistered = true,
): Promise<SimHandle> {
  const events: SimEvent[] = [];
  const sim = new Sim({
    serverUrl: world.t.url,
    name: "a",
    version: CLIENT_VERSION,
    policy: { kind: "all" },
    heartbeatIntervalMs: SIM_HEARTBEAT_MS,
    ...(options.code === undefined && options.statePath === undefined ? { token: world.token } : {}),
    ...options,
    emit: (event) => events.push(event),
  });
  world.sims.push(sim);
  await sim.start();
  const handle: SimHandle = {
    sim,
    events,
    next: eventWaiter(events),
    sent: (type) =>
      events.filter((e) => e.event === "tx" && (e.msg as Json).type === type).map((e) => e.msg as Json),
  };
  if (waitRegistered) await handle.next("registered");
  return handle;
}

export interface SimProcess {
  child: ChildProcess;
  events: SimEvent[];
  next(event: string, match?: (e: SimEvent) => boolean, timeoutMs?: number): Promise<SimEvent>;
  exited: Promise<number | null>;
}

/** `devotchi sim` as a child process; its JSON lines are parsed into events. */
export function spawnSim(world: World, args: string[]): SimProcess {
  const child = spawn(process.execPath, [CLI_PATH, "sim", "--heartbeat-interval", String(SIM_HEARTBEAT_MS), ...args], {
    env: world.box.env as NodeJS.ProcessEnv,
    stdio: ["pipe", "pipe", "pipe"],
  });
  world.children.push(child);
  const events: SimEvent[] = [];
  createInterface({ input: child.stdout! }).on("line", (line) => {
    try {
      events.push(JSON.parse(line) as SimEvent);
    } catch {
      events.push({ event: "garbage", at: "", line });
    }
  });
  const exited = new Promise<number | null>((resolve) => child.once("exit", (code) => resolve(code)));
  return { child, events, next: eventWaiter(events), exited };
}

/** Runs the CLI with `input` on stdin (for prompts read from a pipe). */
export function runCliWithInput(world: World, args: string[], input: string, timeoutMs = 20_000): Promise<CliResult> {
  return new Promise((resolve, reject) => {
    const child = spawn(process.execPath, [CLI_PATH, ...args], { env: world.box.env as NodeJS.ProcessEnv, stdio: "pipe" });
    let stdout = "";
    let stderr = "";
    child.stdout.on("data", (chunk: Buffer) => (stdout += chunk.toString()));
    child.stderr.on("data", (chunk: Buffer) => (stderr += chunk.toString()));
    const timer = setTimeout(() => {
      child.kill("SIGKILL");
      reject(new Error(`devotchi ${args.join(" ")} did not finish\n${stdout}\n${stderr}`));
    }, timeoutMs);
    child.on("close", (code) => {
      clearTimeout(timer);
      resolve({ code, stdout, stderr });
    });
    child.stdin.end(input);
  });
}
