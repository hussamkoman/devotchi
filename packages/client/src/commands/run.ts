// `devotchi run -- <cmd...>`: run a command, tell the pet how it ended and
// exit with the command's exit code.

import { spawn } from "node:child_process";
import { constants } from "node:os";
import { basename } from "node:path";
import { connectServer, type Io } from "../context.js";
import { UsageError, errorMessage } from "../errors.js";
import { minutesSeconds } from "../format.js";

/** "_succeeded" is the longest suffix; events are at most 40 bytes. */
const LABEL_MAX = 40 - "_succeeded".length;
const FORWARDED_SIGNALS = ["SIGINT", "SIGTERM", "SIGHUP"] as const;

export function labelFor(command: string): string {
  const label = basename(command)
    .toLowerCase()
    .replace(/\.(exe|cmd|bat|sh|js|mjs|py)$/, "")
    .replace(/[^a-z0-9_]+/g, "_")
    .replace(/^_+|_+$/g, "")
    .slice(0, LABEL_MAX);
  return label === "" ? "command" : label;
}

export interface RunOptions {
  server?: string;
  name?: string;
}

export async function runCommand(argv: string[], options: RunOptions, io: Io): Promise<number> {
  const [command, ...args] = argv;
  if (command === undefined) throw new UsageError("give the command to run after --, e.g. devotchi run -- pnpm test");
  const label = options.name !== undefined ? labelFor(options.name) : labelFor(command);

  const started = Date.now();
  const child = spawn(command, args, { stdio: "inherit" });
  const forward = (signal: NodeJS.Signals) => () => {
    child.kill(signal);
  };
  const handlers = FORWARDED_SIGNALS.map((signal) => [signal, forward(signal)] as const);
  for (const [signal, handler] of handlers) process.on(signal, handler);

  const exit = await new Promise<{ code: number; text: string }>((resolve) => {
    child.once("error", (err) => resolve({ code: 127, text: `could not start: ${errorMessage(err)}` }));
    child.once("exit", (code, signal) => {
      if (signal !== null) resolve({ code: 128 + (constants.signals[signal] ?? 0), text: `killed by ${signal}` });
      else resolve({ code: code ?? 1, text: `exit ${code ?? 1}` });
    });
  });
  for (const [signal, handler] of handlers) process.off(signal, handler);
  if (exit.code === 127 && exit.text.startsWith("could not start")) io.note(`${command}: ${exit.text}`);

  const event = `${label}_${exit.code === 0 ? "succeeded" : "failed"}`;
  const detail = `${exit.text} after ${minutesSeconds(Date.now() - started)}`;
  try {
    const { api } = await connectServer(options.server, io);
    const result = await api.notify({ event, detail, source: "run" });
    io.note(`${event} (${detail}) sent to ${result.delivered} consumer${result.delivered === 1 ? "" : "s"}`);
  } catch (err) {
    io.note(`could not notify the pet: ${errorMessage(err)}`);
  }
  return exit.code;
}
