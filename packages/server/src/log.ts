// One JSON object per line. Callers pass only identifiers and counters:
// tokens, pairing codes and message text never go into a log line.

import type { LogLevel } from "@devotchi/shared";

export type LogFields = Record<string, string | number | boolean | null | undefined>;
export type LogSink = (line: string) => void;

const RANK: Record<LogLevel, number> = { debug: 10, info: 20, warn: 30, error: 40 };

export class Logger {
  private readonly min: number;

  constructor(
    level: LogLevel,
    private readonly sink: LogSink = (line) => process.stdout.write(line + "\n"),
  ) {
    this.min = RANK[level];
  }

  enabled(level: LogLevel): boolean {
    return RANK[level] >= this.min;
  }

  debug(msg: string, fields?: LogFields): void {
    this.write("debug", msg, fields);
  }

  info(msg: string, fields?: LogFields): void {
    this.write("info", msg, fields);
  }

  warn(msg: string, fields?: LogFields): void {
    this.write("warn", msg, fields);
  }

  error(msg: string, fields?: LogFields): void {
    this.write("error", msg, fields);
  }

  private write(level: LogLevel, msg: string, fields?: LogFields): void {
    if (!this.enabled(level)) return;
    this.sink(JSON.stringify({ time: new Date().toISOString(), level, msg, ...fields }));
  }
}

/** Message and stack of a thrown value, for the `err`/`stack` log fields. */
export function errorFields(err: unknown): LogFields {
  if (err instanceof Error) return { err: err.message, stack: err.stack };
  return { err: String(err) };
}
