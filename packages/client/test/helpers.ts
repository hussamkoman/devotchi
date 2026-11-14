// Client test helpers: temporary config/home directories, a free port that
// nothing listens on, and running the built CLI as a child process.

import { spawn, type ChildProcess } from "node:child_process";
import { existsSync } from "node:fs";
import { mkdir, mkdtemp, rm } from "node:fs/promises";
import net from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

export const CLI_PATH = fileURLToPath(new URL("../dist/cli.js", import.meta.url));

export function requireBuiltCli(): void {
  if (!existsSync(CLI_PATH)) throw new Error("run `pnpm run build` before the client tests");
}

export interface Sandbox {
  dir: string;
  home: string;
  configDir: string;
  /** Environment for the code under test: temp HOME and config dir, no inherited Devotchi variables. */
  env: Record<string, string | undefined>;
  cleanup(): Promise<void>;
}

export async function makeSandbox(extraEnv: Record<string, string> = {}): Promise<Sandbox> {
  const dir = await mkdtemp(join(tmpdir(), "devotchi-client-"));
  const home = join(dir, "home");
  const configDir = join(dir, "config");
  await mkdir(home);
  const env: Record<string, string | undefined> = {};
  for (const [key, value] of Object.entries(process.env)) {
    if (!key.startsWith("DEVOTCHI_")) env[key] = value;
  }
  Object.assign(env, { HOME: home, DEVOTCHI_CONFIG_DIR: configDir, ...extraEnv });
  return { dir, home, configDir, env, cleanup: () => rm(dir, { recursive: true, force: true }) };
}

/** A loopback port that was free a moment ago and has nothing listening on it. */
export async function unusedPort(): Promise<number> {
  const server = net.createServer();
  await new Promise<void>((resolve) => server.listen(0, "127.0.0.1", resolve));
  const { port } = server.address() as net.AddressInfo;
  await new Promise<void>((resolve) => server.close(() => resolve()));
  return port;
}

export interface CliResult {
  code: number | null;
  stdout: string;
  stderr: string;
}

export function runCli(args: string[], env: Record<string, string | undefined>, cwd?: string, timeoutMs = 20_000): Promise<CliResult> {
  return new Promise((resolve, reject) => {
    const child = spawn(process.execPath, [CLI_PATH, ...args], { env: env as NodeJS.ProcessEnv, cwd, stdio: ["ignore", "pipe", "pipe"] });
    let stdout = "";
    let stderr = "";
    child.stdout.on("data", (chunk: Buffer) => (stdout += chunk.toString()));
    child.stderr.on("data", (chunk: Buffer) => (stderr += chunk.toString()));
    const timer = setTimeout(() => {
      child.kill("SIGKILL");
      reject(new Error(`devotchi ${args.join(" ")} did not finish within ${timeoutMs} ms\n${stdout}\n${stderr}`));
    }, timeoutMs);
    child.on("error", reject);
    child.on("close", (code) => {
      clearTimeout(timer);
      resolve({ code, stdout, stderr });
    });
  });
}

/** Starts the CLI and resolves once stdout contains a line matching `ready`. */
export function startCli(
  args: string[],
  env: Record<string, string | undefined>,
  ready: RegExp,
  timeoutMs = 20_000,
): Promise<{ child: ChildProcess; stdout: () => string; stderr: () => string; match: RegExpMatchArray }> {
  return new Promise((resolve, reject) => {
    const child = spawn(process.execPath, [CLI_PATH, ...args], { env: env as NodeJS.ProcessEnv, stdio: ["ignore", "pipe", "pipe"] });
    let stdout = "";
    let stderr = "";
    const timer = setTimeout(() => {
      child.kill("SIGKILL");
      reject(new Error(`no output matching ${ready} within ${timeoutMs} ms\n${stdout}\n${stderr}`));
    }, timeoutMs);
    child.stdout!.on("data", (chunk: Buffer) => {
      stdout += chunk.toString();
      const match = stdout.match(ready);
      if (match) {
        clearTimeout(timer);
        resolve({ child, stdout: () => stdout, stderr: () => stderr, match });
      }
    });
    child.stderr!.on("data", (chunk: Buffer) => (stderr += chunk.toString()));
    child.on("exit", (code) => {
      clearTimeout(timer);
      reject(new Error(`exited with ${code} before ready\n${stdout}\n${stderr}`));
    });
  });
}

export function waitForExit(child: ChildProcess, timeoutMs = 10_000): Promise<number | null> {
  if (child.exitCode !== null) return Promise.resolve(child.exitCode);
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error("child did not exit")), timeoutMs);
    child.once("exit", (code) => {
      clearTimeout(timer);
      resolve(code);
    });
  });
}

export async function until(check: () => boolean | Promise<boolean>, timeoutMs = 5000, what = "condition"): Promise<void> {
  const started = Date.now();
  while (!(await check())) {
    if (Date.now() - started > timeoutMs) throw new Error(`timed out waiting for ${what}`);
    await new Promise((resolve) => setTimeout(resolve, 20));
  }
}
