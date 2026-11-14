// `devotchi setup`: install, check or remove the Claude Code hooks.

import { mkdir, stat } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { ApiClient } from "./api.js";
import { ConfigStore, maskToken, type Env } from "./config.js";
import { ServerCredentialSource, type ResolvedCredentials } from "./credentials.js";
import { CliError, UsageError, errorMessage } from "./errors.js";
import {
  DEVOTCHI_HOOKS,
  TOKEN_ENV_VAR,
  checkDevotchiHooks,
  installDevotchiHooks,
  readSettings,
  removeDevotchiHooks,
  settingsPath,
  writeSettings,
  type HookReport,
} from "./hooks.js";

export interface SetupOptions {
  dir?: string;
  user?: boolean;
  server?: string;
  remove?: boolean;
  check?: boolean;
  /** true/false sets the room's shareText; undefined leaves it alone. */
  shareText?: boolean;
  envToken?: boolean;
  force?: boolean;
}

export interface SetupIo {
  env: Env;
  out: (line: string) => void;
  cwd: string;
}

async function targetPath(options: SetupOptions, cwd: string): Promise<string> {
  if (options.user === true) {
    if (options.dir !== undefined) throw new UsageError("use either --dir or --user, not both");
    return settingsPath({ user: true });
  }
  const dir = resolve(cwd, options.dir ?? ".");
  const info = await stat(dir).catch(() => null);
  if (info === null || !info.isDirectory()) throw new UsageError(`directory ${dir} does not exist`);
  return settingsPath({ dir });
}

/** Returns the process exit code. */
export async function runSetup(options: SetupOptions, io: SetupIo): Promise<number> {
  const modes = [options.remove, options.check].filter((flag) => flag === true).length;
  if (modes > 1) throw new UsageError("use only one of --remove and --check");
  const path = await targetPath(options, io.cwd);
  const file = await readSettings(path);

  if (options.remove === true) {
    const { settings, removed } = removeDevotchiHooks(file.data, path);
    if (removed === 0) {
      io.out(`No Devotchi hooks in ${path}; nothing changed.`);
      return 0;
    }
    const backup = await writeSettings(file, settings);
    io.out(`Removed ${removed} Devotchi hook${removed === 1 ? "" : "s"} from ${path}.`);
    if (backup !== null) io.out(`The previous file is saved as ${backup}; it may contain a token, so do not commit it.`);
    return 0;
  }

  const store = new ConfigStore(io.env);
  const { url } = await store.resolveServer(options.server);

  if (options.check === true) {
    const token = store.envToken ?? (await store.getCredentials(url))?.token ?? null;
    const report = checkDevotchiHooks(file.data, url, token, io.env);
    printReport(report, path, url, io.out);
    return report.status === "current" ? 0 : 1;
  }

  const credentials = new ServerCredentialSource(store, url, (line) => io.out(line));
  let creds: ResolvedCredentials;
  try {
    creds = await credentials.get();
  } catch (err) {
    throw new CliError(`cannot get a token for ${url}: ${errorMessage(err)}; nothing was changed`);
  }
  if (options.force !== true) {
    try {
      creds = await credentials.verify(creds);
    } catch (err) {
      throw new CliError(
        `cannot verify ${url} and its token: ${errorMessage(err)}; nothing was changed (--force writes the hooks anyway)`,
      );
    }
  }
  const api = new ApiClient(url);
  if (options.shareText !== undefined) {
    await api.withToken(creds.token).updateRoomSettings({ shareText: options.shareText });
  }

  const envToken = options.envToken === true;
  const { settings, removed } = installDevotchiHooks(
    file.data,
    path,
    url,
    envToken ? { envVar: TOKEN_ENV_VAR } : { token: creds.token },
  );
  await mkdir(dirname(path), { recursive: true });
  const backup = await writeSettings(file, settings);

  io.out(`Installed ${DEVOTCHI_HOOKS.length} Devotchi hooks in ${path}.`);
  io.out(`  Server: ${url}${creds.roomId !== "" ? `, room ${creds.roomId}` : ""}`);
  io.out(
    envToken
      ? `  Token: read from $${TOKEN_ENV_VAR} when Claude Code runs a hook`
      : `  Token: ${maskToken(creds.token)} (sent in the Authorization header, never in the URL)`,
  );
  if (removed > 0) io.out(`  Replaced ${removed} older Devotchi hook${removed === 1 ? "" : "s"}; other hooks were kept.`);
  if (backup !== null) io.out(`  The previous file is saved as ${backup}; it may contain a token, so do not commit it.`);
  if (options.shareText !== undefined) {
    io.out(`  Stop notifications ${options.shareText ? "now include" : "no longer include"} the first line of Claude's reply.`);
  }
  if (options.force === true) io.out("  The server and token were not verified (--force).");
  if (envToken) {
    io.out(`Add this to the environment Claude Code starts from (for example your shell profile):`);
    io.out(`  export ${TOKEN_ENV_VAR}=${creds.token}`);
  }
  io.out("Claude Code picks up the change by itself; check the hooks any time with `devotchi setup --check`.");
  return 0;
}

function printReport(report: HookReport, path: string, url: string, out: (line: string) => void): void {
  const label = { current: "up to date", missing: "not installed", stale: "out of date" }[report.status];
  out(`Devotchi hooks in ${path}: ${label} (server ${url})`);
  for (const event of report.events) {
    const mark = event.state === "ok" ? "ok     " : event.state === "missing" ? "missing" : "stale  ";
    out(`  ${mark} ${event.event}${event.reason !== undefined ? `: ${event.reason}` : ""}`);
  }
  for (const event of report.extra) out(`  extra   ${event}: not one of the nine Devotchi hooks`);
  if (report.status !== "current") out("Run `devotchi setup` to install the current hooks.");
}
