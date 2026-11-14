// Claude Code hook entries owned by Devotchi (protocol 8.7): building them,
// recognising them in a settings file, and checking whether the installed
// ones are current. Hooks that are not Devotchi's are never touched.

import { readFile, stat, writeFile } from "node:fs/promises";
import { homedir } from "node:os";
import { join } from "node:path";
import { HOOK_MARKER_HEADER } from "@devotchi/shared";
import { CliError, errorMessage } from "./errors.js";
import { isErrno, writeFileAtomic } from "./fsutil.js";

export interface HookSpec {
  event: string;
  matcher?: string;
  timeout: number;
}

/** The nine hooks of protocol 8.7, in installation order. */
export const DEVOTCHI_HOOKS: readonly HookSpec[] = [
  { event: "PermissionRequest", timeout: 310 },
  { event: "PostToolUse", timeout: 5 },
  { event: "PostToolUseFailure", timeout: 5 },
  { event: "PostToolBatch", timeout: 5 },
  { event: "Notification", matcher: "idle_prompt|agent_needs_input|permission_prompt", timeout: 5 },
  { event: "UserPromptSubmit", timeout: 5 },
  { event: "Stop", timeout: 5 },
  { event: "StopFailure", timeout: 5 },
  { event: "SessionEnd", timeout: 2 },
];

export const TOKEN_ENV_VAR = "DEVOTCHI_TOKEN";
const LEGACY_PATHS = new Set(["/hook/permission", "/hook/status", "/hook/stop"]);
const ENV_TOKEN_VALUES = new Set([`Bearer $${TOKEN_ENV_VAR}`, `Bearer \${${TOKEN_ENV_VAR}}`]);

export type HookAuth = { token: string } | { envVar: typeof TOKEN_ENV_VAR };

type Json = Record<string, unknown>;

function isObject(value: unknown): value is Json {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

export function hookUrl(serverUrl: string): string {
  return `${serverUrl}/hook/event`;
}

export function settingsPath(options: { dir: string } | { user: true }): string {
  return "user" in options
    ? join(homedir(), ".claude", "settings.json")
    : join(options.dir, ".claude", "settings.local.json");
}

function headerValue(headers: unknown, name: string): string | undefined {
  if (!isObject(headers)) return undefined;
  const wanted = name.toLowerCase();
  for (const [key, value] of Object.entries(headers)) {
    if (key.toLowerCase() === wanted && typeof value === "string") return value;
  }
  return undefined;
}

function hasHeader(headers: unknown, name: string): boolean {
  if (!isObject(headers)) return false;
  const wanted = name.toLowerCase();
  return Object.keys(headers).some((key) => key.toLowerCase() === wanted);
}

function isLegacyDevotchiUrl(url: unknown): boolean {
  if (typeof url !== "string") return false;
  try {
    const parsed = new URL(url);
    return LEGACY_PATHS.has(parsed.pathname) && parsed.searchParams.has("token");
  } catch {
    return false;
  }
}

/** A hook installed by any version of `devotchi setup`. */
export function isDevotchiHook(hook: unknown): boolean {
  if (!isObject(hook) || hook.type !== "http") return false;
  return hasHeader(hook.headers, HOOK_MARKER_HEADER) || isLegacyDevotchiUrl(hook.url);
}

export function buildHookEntry(serverUrl: string, spec: HookSpec, auth: HookAuth): Json {
  const hook: Json = {
    type: "http",
    url: hookUrl(serverUrl),
    timeout: spec.timeout,
    headers: {
      Authorization: "token" in auth ? `Bearer ${auth.token}` : `Bearer $${auth.envVar}`,
      [HOOK_MARKER_HEADER]: "1",
    },
  };
  if ("envVar" in auth) hook.allowedEnvVars = [auth.envVar];
  const group: Json = {};
  if (spec.matcher !== undefined) group.matcher = spec.matcher;
  group.hooks = [hook];
  return group;
}

function hooksObject(settings: Json, path: string): Json | null {
  if (settings.hooks === undefined) return null;
  if (!isObject(settings.hooks)) throw new CliError(`"hooks" in ${path} is not an object; nothing was changed`);
  return settings.hooks;
}

/**
 * Removes every Devotchi hook. A group left without hooks and an event left
 * without groups are removed too, but only when Devotchi hooks emptied them.
 */
export function removeDevotchiHooks(settings: Json, path: string): { settings: Json; removed: number } {
  const hooks = hooksObject(settings, path);
  if (hooks === null) return { settings, removed: 0 };
  let removed = 0;
  const nextHooks: Json = {};
  for (const [event, groups] of Object.entries(hooks)) {
    if (!Array.isArray(groups)) {
      nextHooks[event] = groups;
      continue;
    }
    let changed = false;
    const nextGroups: unknown[] = [];
    for (const group of groups) {
      if (!isObject(group) || !Array.isArray(group.hooks)) {
        nextGroups.push(group);
        continue;
      }
      const kept = group.hooks.filter((hook) => !isDevotchiHook(hook));
      const dropped = group.hooks.length - kept.length;
      if (dropped === 0) {
        nextGroups.push(group);
        continue;
      }
      removed += dropped;
      changed = true;
      if (kept.length > 0) nextGroups.push({ ...group, hooks: kept });
    }
    if (!changed || nextGroups.length > 0) nextHooks[event] = changed ? nextGroups : groups;
  }
  const next: Json = { ...settings };
  if (removed > 0 && Object.keys(nextHooks).length === 0) delete next.hooks;
  else next.hooks = removed > 0 ? nextHooks : hooks;
  return { settings: next, removed };
}

export function installDevotchiHooks(
  settings: Json,
  path: string,
  serverUrl: string,
  auth: HookAuth,
): { settings: Json; removed: number } {
  const stripped = removeDevotchiHooks(settings, path);
  const hooks: Json = isObject(stripped.settings.hooks) ? { ...stripped.settings.hooks } : {};
  for (const spec of DEVOTCHI_HOOKS) {
    const existing = hooks[spec.event];
    if (existing !== undefined && !Array.isArray(existing)) {
      throw new CliError(`"hooks.${spec.event}" in ${path} is not an array; nothing was changed`);
    }
    hooks[spec.event] = [...(existing ?? []), buildHookEntry(serverUrl, spec, auth)];
  }
  return { settings: { ...stripped.settings, hooks }, removed: stripped.removed };
}

export type HookState = "ok" | "missing" | "stale";

export interface HookCheck {
  event: string;
  state: HookState;
  reason?: string;
}

export interface HookReport {
  status: "current" | "missing" | "stale";
  events: HookCheck[];
  /** Devotchi hooks under events that are not part of the nine. */
  extra: string[];
}

function ownedHooks(groups: unknown): { group: Json; hook: Json }[] {
  if (!Array.isArray(groups)) return [];
  const out: { group: Json; hook: Json }[] = [];
  for (const group of groups) {
    if (!isObject(group) || !Array.isArray(group.hooks)) continue;
    for (const hook of group.hooks) if (isDevotchiHook(hook)) out.push({ group, hook: hook as Json });
  }
  return out;
}

function checkOne(spec: HookSpec, found: { group: Json; hook: Json }[], serverUrl: string, token: string | null, env: Record<string, string | undefined>): HookCheck {
  if (found.length === 0) return { event: spec.event, state: "missing" };
  if (found.length > 1) return { event: spec.event, state: "stale", reason: `${found.length} Devotchi hooks instead of one` };
  const { group, hook } = found[0]!;
  const stale = (reason: string): HookCheck => ({ event: spec.event, state: "stale", reason });
  if (!hasHeader(hook.headers, HOOK_MARKER_HEADER)) return stale("old v1 hook with the token in its URL");
  if (hook.url !== hookUrl(serverUrl)) return stale(`points at ${String(hook.url)}`);
  if (hook.timeout !== spec.timeout) return stale(`timeout ${String(hook.timeout)} s instead of ${spec.timeout} s`);
  if ((group.matcher ?? undefined) !== spec.matcher) return stale(`matcher ${JSON.stringify(group.matcher ?? null)}`);
  const auth = headerValue(hook.headers, "Authorization");
  if (auth === undefined) return stale("no Authorization header");
  if (token === null) return stale("no token is stored for this server to compare with");
  if (ENV_TOKEN_VALUES.has(auth)) {
    const allowed = Array.isArray(hook.allowedEnvVars) && hook.allowedEnvVars.includes(TOKEN_ENV_VAR);
    if (!allowed) return stale(`${TOKEN_ENV_VAR} is not listed in allowedEnvVars`);
    const value = env[TOKEN_ENV_VAR];
    if (value === undefined || value === "") return stale(`uses $${TOKEN_ENV_VAR}, which is not set in this shell`);
    if (value !== token) return stale(`$${TOKEN_ENV_VAR} is not the token of this server`);
    return { event: spec.event, state: "ok" };
  }
  if (auth !== `Bearer ${token}`) return stale("uses a different token");
  return { event: spec.event, state: "ok" };
}

/**
 * Compares the Devotchi hooks in `settings` with what `setup` would write
 * for `serverUrl` and `token` (null when no token is known).
 */
export function checkDevotchiHooks(
  settings: Json,
  serverUrl: string,
  token: string | null,
  env: Record<string, string | undefined> = process.env,
): HookReport {
  const hooks = isObject(settings.hooks) ? settings.hooks : {};
  const events = DEVOTCHI_HOOKS.map((spec) => checkOne(spec, ownedHooks(hooks[spec.event]), serverUrl, token, env));
  const expected = new Set(DEVOTCHI_HOOKS.map((spec) => spec.event));
  const extra = Object.keys(hooks).filter((event) => !expected.has(event) && ownedHooks(hooks[event]).length > 0);
  let status: HookReport["status"] = "stale";
  if (extra.length === 0 && events.every((e) => e.state === "ok")) status = "current";
  else if (extra.length === 0 && events.every((e) => e.state === "missing")) status = "missing";
  return { status, events, extra };
}

export interface SettingsFile {
  path: string;
  exists: boolean;
  data: Json;
  text: string | null;
  mode: number | null;
}

/** A missing file reads as {}; anything that is not a JSON object aborts. */
export async function readSettings(path: string): Promise<SettingsFile> {
  let text: string;
  try {
    text = await readFile(path, "utf8");
  } catch (err) {
    if (isErrno(err, "ENOENT")) return { path, exists: false, data: {}, text: null, mode: null };
    throw new CliError(`cannot read ${path}: ${errorMessage(err)}`);
  }
  let data: unknown;
  try {
    data = text.trim() === "" ? {} : JSON.parse(text);
  } catch (err) {
    throw new CliError(`${path} is not valid JSON (${errorMessage(err)}); fix it first, nothing was changed`);
  }
  if (!isObject(data)) throw new CliError(`${path} does not contain a JSON object; nothing was changed`);
  const info = await stat(path);
  return { path, exists: true, data, text, mode: info.mode & 0o777 };
}

/**
 * Atomic replace. A new file gets mode 0600 because it holds the token; an
 * existing one keeps its mode and its previous content is copied to .bak.
 * Returns the backup path, if one was written.
 */
export async function writeSettings(file: SettingsFile, data: Json): Promise<string | null> {
  const text = JSON.stringify(data, null, 2) + "\n";
  let backup: string | null = null;
  if (file.exists && file.text !== null) {
    backup = `${file.path}.bak`;
    await writeFile(backup, file.text, { mode: 0o600 });
  }
  await writeFileAtomic(file.path, text, file.mode ?? 0o600);
  return backup;
}
