// `devotchi start --local`: the real server, embedded in this process.

import { networkInterfaces } from "node:os";
import { join } from "node:path";
import type { DevotchiServer } from "@devotchi/server";
import type { ConfigStore } from "./config.js";
import { CliError, UsageError, errorMessage } from "./errors.js";

export const LOCAL_DB_FILE = "local.db";

export interface LocalServerOptions {
  store: ConfigStore;
  /** Listen on all interfaces so a device on the LAN can connect. */
  lan: boolean;
  /** 0 picks a free port. */
  port: number;
  /** Server log lines (JSON); warnings and errors only. */
  logSink?: (line: string) => void;
}

export interface LocalServer {
  server: DevotchiServer;
  /** Loopback URL stored as the default server, so mcp, setup and pair find it. */
  url: string;
}

/**
 * The server package loads node:sqlite, which Node 24 announces with a
 * two-line ExperimentalWarning on stderr. Only that warning is dropped, and
 * the package is imported only by the command that runs the server, so `mcp`
 * and the other commands never load it.
 */
let warningFilterInstalled = false;

async function loadServerPackage(): Promise<typeof import("@devotchi/server")> {
  if (warningFilterInstalled) return import("@devotchi/server");
  warningFilterInstalled = true;
  const emitWarning = process.emitWarning.bind(process) as (...args: unknown[]) => void;
  process.emitWarning = ((warning: string | Error, ...rest: unknown[]) => {
    const text = typeof warning === "string" ? warning : warning.message;
    const type = typeof rest[0] === "string" ? rest[0] : (rest[0] as { type?: unknown } | undefined)?.type;
    if (type === "ExperimentalWarning" && text.startsWith("SQLite ")) return;
    emitWarning(warning, ...rest);
  }) as typeof process.emitWarning;
  return import("@devotchi/server");
}

export async function startLocalServer(options: LocalServerOptions): Promise<LocalServer> {
  const { store, lan, port } = options;
  await store.ensureDir();
  const { ConfigError: ServerConfigError, createServer } = await loadServerPackage();
  let server: DevotchiServer;
  try {
    server = await createServer({
      host: lan ? "0.0.0.0" : "127.0.0.1",
      port,
      dbPath: join(store.dir, LOCAL_DB_FILE),
      mode: "single-user",
      logLevel: "warn",
      logSink: options.logSink ?? ((line) => process.stderr.write(line + "\n")),
    });
  } catch (err) {
    if (err instanceof ServerConfigError) throw new UsageError(err.message);
    const message = errorMessage(err);
    if (/already in use/.test(message)) {
      throw new CliError(`port ${port} is already in use; start the local server on another port with --server-port <n>`);
    }
    throw new CliError(`cannot start the local server: ${message}`);
  }
  const url = `http://127.0.0.1:${server.port}`;
  try {
    await store.update((config) => {
      config.serverUrl = url;
    });
  } catch (err) {
    await server.close();
    throw err;
  }
  return { server, url };
}

/** IPv4 addresses of this machine that a device on the same network can use. */
export function lanAddresses(): string[] {
  const out: string[] = [];
  for (const list of Object.values(networkInterfaces())) {
    for (const addr of list ?? []) {
      if (addr.family === "IPv4" && !addr.internal) out.push(addr.address);
    }
  }
  return out;
}
