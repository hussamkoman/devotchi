#!/usr/bin/env node
// Executable: configuration from the environment, signals, exit codes
// (0 clean stop, 1 runtime or startup failure, 2 invalid configuration).

import { ConfigError, StartupError, createServer, loadConfig, type DevotchiServer } from "./app.js";
import { Logger, errorFields } from "./log.js";

function fatal(message: string, code: number): never {
  process.stderr.write(`devotchi-server: ${message}\n`);
  process.exit(code);
}

async function main(): Promise<void> {
  let env;
  try {
    env = loadConfig(process.env);
  } catch (err) {
    if (err instanceof ConfigError) fatal(err.message, 2);
    throw err;
  }
  const log = new Logger(env.config.logLevel);
  for (const warning of env.warnings) log.warn(warning);

  process.on("uncaughtException", (err) => {
    log.error("uncaught exception", errorFields(err));
    process.exit(1);
  });
  process.on("unhandledRejection", (reason) => {
    log.error("unhandled rejection", errorFields(reason));
    process.exit(1);
  });

  let server: DevotchiServer;
  try {
    server = await createServer(env.config);
  } catch (err) {
    if (err instanceof ConfigError) fatal(err.message, 2);
    if (err instanceof StartupError) fatal(err.message, 1);
    throw err;
  }

  let stopping = false;
  const stop = (signal: string) => {
    if (stopping) {
      log.warn("second signal, exiting immediately", { signal });
      process.exit(1);
    }
    stopping = true;
    log.info("shutting down", { signal });
    server.close().then(
      () => process.exit(0),
      (err: unknown) => {
        log.error("shutdown failed", errorFields(err));
        process.exit(1);
      },
    );
  };
  process.on("SIGINT", () => stop("SIGINT"));
  process.on("SIGTERM", () => stop("SIGTERM"));
}

main().catch((err: unknown) => {
  process.stderr.write(`devotchi-server: ${err instanceof Error ? err.message : String(err)}\n`);
  process.exit(1);
});
