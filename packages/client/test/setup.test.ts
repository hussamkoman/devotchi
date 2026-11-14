import { mkdir, readFile, readdir, stat, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { afterAll, afterEach, beforeAll, beforeEach, describe, expect, it } from "vitest";
import { startTestServer, type Json, type TestServer } from "../../server/test/helpers.js";
import { ConfigStore } from "../src/config.js";
import { CliError, UsageError } from "../src/errors.js";
import { DEVOTCHI_HOOKS, isDevotchiHook } from "../src/hooks.js";
import { runSetup, type SetupOptions } from "../src/setup.js";
import { makeSandbox, requireBuiltCli, runCli, unusedPort, type Sandbox } from "./helpers.js";

let t: TestServer;
let box: Sandbox;
let project: string;
let lines: string[];
let token: string;
let roomId: string;

const NINE = ["PermissionRequest", "PostToolUse", "PostToolUseFailure", "PostToolBatch", "Notification", "UserPromptSubmit", "Stop", "StopFailure", "SessionEnd"];

beforeAll(async () => {
  t = await startTestServer();
});

afterAll(async () => {
  await t.close();
});

beforeEach(async () => {
  box = await makeSandbox();
  project = join(box.dir, "project");
  await mkdir(project);
  lines = [];
  const minted = t.cliToken("setup-test");
  token = minted.token;
  roomId = minted.roomId;
  await new ConfigStore(box.env).setCredentials(t.url, { token, roomId });
});

afterEach(async () => {
  await box.cleanup();
});

const settingsFile = () => join(project, ".claude", "settings.local.json");

function setup(options: SetupOptions = {}, env: Record<string, string | undefined> = box.env): Promise<number> {
  return runSetup({ dir: project, server: t.url, ...options }, { env, out: (line) => lines.push(line), cwd: box.dir });
}

async function readSettingsJson(): Promise<Json> {
  return JSON.parse(await readFile(settingsFile(), "utf8")) as Json;
}

async function writeSettingsJson(data: unknown): Promise<string> {
  await mkdir(join(project, ".claude"), { recursive: true });
  const text = typeof data === "string" ? data : JSON.stringify(data, null, 2);
  await writeFile(settingsFile(), text);
  return text;
}

function owned(settings: Json): { event: string; group: Json; hook: Json }[] {
  const out: { event: string; group: Json; hook: Json }[] = [];
  for (const [event, groups] of Object.entries((settings.hooks ?? {}) as Record<string, Json[]>)) {
    for (const group of groups) {
      for (const hook of group.hooks as Json[]) if (isDevotchiHook(hook)) out.push({ event, group, hook });
    }
  }
  return out;
}

const THIRD_PARTY = {
  permissions: { allow: ["Bash(npm test:*)"], deny: [] },
  env: { FOO: "bar" },
  model: "opus",
  hooks: {
    PostToolUse: [
      { matcher: "Edit", hooks: [{ type: "command", command: "prettier --write" }] },
      { hooks: [{ type: "http", url: "https://ci.example.com/hook/status", timeout: 3 }] },
    ],
    Stop: [{ hooks: [{ type: "http", url: "https://other.example.com/api/hook/stop?token=theirs" }] }],
    PreToolUse: [{ matcher: "Bash", hooks: [{ type: "command", command: "audit.sh" }] }],
  },
};

describe("devotchi setup", () => {
  it("writes exactly the nine hooks with Bearer + X-Devotchi-Hook headers and no token in any URL", async () => {
    expect(await setup()).toBe(0);
    const settings = await readSettingsJson();
    const hooks = owned(settings);
    expect(hooks.map((h) => h.event)).toEqual(NINE);
    for (const spec of DEVOTCHI_HOOKS) {
      const entry = hooks.find((h) => h.event === spec.event)!;
      expect(entry.hook).toEqual({
        type: "http",
        url: `${t.url}/hook/event`,
        timeout: spec.timeout,
        headers: { Authorization: `Bearer ${token}`, "X-Devotchi-Hook": "1" },
      });
      expect(entry.group.matcher).toBe(spec.matcher);
    }
    expect(hooks.find((h) => h.event === "Notification")!.group.matcher).toBe("idle_prompt|agent_needs_input|permission_prompt");
    expect(hooks.find((h) => h.event === "PermissionRequest")!.hook.timeout).toBe(310);
    expect(hooks.find((h) => h.event === "SessionEnd")!.hook.timeout).toBe(2);
    const text = await readFile(settingsFile(), "utf8");
    expect(text).not.toContain("token=");
    expect((await stat(settingsFile())).mode & 0o777).toBe(0o600);
    expect(lines[0]).toContain("Installed 9 Devotchi hooks");
    expect(lines.join("\n")).not.toContain(token);
  });

  it("is idempotent", async () => {
    await setup();
    const first = await readFile(settingsFile(), "utf8");
    await setup();
    expect(await readFile(settingsFile(), "utf8")).toBe(first);
    expect(owned(await readSettingsJson())).toHaveLength(9);
  });

  it("keeps unrelated settings and third-party hooks, including an http hook whose URL contains /hook/", async () => {
    await writeSettingsJson(THIRD_PARTY);
    await setup();
    const settings = await readSettingsJson();
    expect(settings.permissions).toEqual(THIRD_PARTY.permissions);
    expect(settings.env).toEqual(THIRD_PARTY.env);
    expect(settings.model).toBe("opus");
    const hooks = settings.hooks as Record<string, Json[]>;
    expect(hooks.PreToolUse).toEqual(THIRD_PARTY.hooks.PreToolUse);
    expect(hooks.PostToolUse!.slice(0, 2)).toEqual(THIRD_PARTY.hooks.PostToolUse);
    expect(hooks.Stop![0]).toEqual(THIRD_PARTY.hooks.Stop[0]);
    expect(owned(settings)).toHaveLength(9);
  });

  it("keeps the previous file as .bak and its mode", async () => {
    const original = await writeSettingsJson(THIRD_PARTY);
    const { chmod } = await import("node:fs/promises");
    await chmod(settingsFile(), 0o640);
    await setup();
    expect(await readFile(settingsFile() + ".bak", "utf8")).toBe(original);
    expect((await stat(settingsFile())).mode & 0o777).toBe(0o640);
    expect((await readdir(join(project, ".claude"))).sort()).toEqual(["settings.local.json", "settings.local.json.bak"]);
  });

  it("migrates legacy v1 Devotchi hooks (token in the URL)", async () => {
    await writeSettingsJson({
      hooks: {
        PermissionRequest: [{ hooks: [{ type: "http", url: `${t.url}/hook/permission?token=old`, timeout: 310 }] }],
        PostToolUse: [
          { matcher: "Bash|Write|Edit", hooks: [{ type: "http", url: `${t.url}/hook/status?token=old`, timeout: 5 }] },
          ...THIRD_PARTY.hooks.PostToolUse,
        ],
        Stop: [{ hooks: [{ type: "http", url: `http://elsewhere:8080/hook/stop?token=old`, timeout: 5 }] }],
      },
    });
    await setup();
    const settings = await readSettingsJson();
    expect(owned(settings)).toHaveLength(9);
    expect(JSON.stringify(settings)).not.toContain("token=old");
    expect((settings.hooks as Record<string, Json[]>).PostToolUse!.slice(0, 2)).toEqual(THIRD_PARTY.hooks.PostToolUse);
    expect(lines.join("\n")).toContain("Replaced 3 older Devotchi hooks");
  });

  it("aborts on an unparsable settings file and changes nothing", async () => {
    const broken = '{ "hooks": { "Stop": [ }';
    await writeSettingsJson(broken);
    const err = await setup().catch((e: unknown) => e);
    expect(err).toBeInstanceOf(CliError);
    expect((err as Error).message).toMatch(/not valid JSON/);
    expect(await readFile(settingsFile(), "utf8")).toBe(broken);
    expect(await readdir(join(project, ".claude"))).toEqual(["settings.local.json"]);
    await expect(setup({ remove: true })).rejects.toBeInstanceOf(CliError);
    expect(await readFile(settingsFile(), "utf8")).toBe(broken);
  });

  it("--remove deletes only Devotchi hooks", async () => {
    await writeSettingsJson(THIRD_PARTY);
    await setup();
    lines = [];
    expect(await setup({ remove: true })).toBe(0);
    expect(await readSettingsJson()).toEqual(THIRD_PARTY);
    expect(lines[0]).toContain("Removed 9 Devotchi hooks");
    lines = [];
    expect(await setup({ remove: true })).toBe(0);
    expect(lines[0]).toContain("nothing changed");
  });

  it("--remove drops the hooks key when only Devotchi hooks were in it", async () => {
    await writeSettingsJson({ model: "opus" });
    await setup();
    await setup({ remove: true });
    expect(await readSettingsJson()).toEqual({ model: "opus" });
  });

  it("--check reports current, missing and stale states", async () => {
    expect(await setup({ check: true })).toBe(1);
    expect(lines[0]).toContain("not installed");

    await setup();
    lines = [];
    expect(await setup({ check: true })).toBe(0);
    expect(lines[0]).toContain("up to date");

    const settings = await readSettingsJson();
    delete (settings.hooks as Record<string, unknown>).PostToolBatch;
    await writeSettingsJson(settings);
    lines = [];
    expect(await setup({ check: true })).toBe(1);
    expect(lines[0]).toContain("out of date");
    expect(lines.join("\n")).toMatch(/missing PostToolBatch/);

    await setup();
    await new ConfigStore(box.env).setCredentials(t.url, { token: "another-token", roomId });
    lines = [];
    expect(await setup({ check: true })).toBe(1);
    expect(lines.join("\n")).toContain("uses a different token");

    const otherServer = `http://127.0.0.1:${await unusedPort()}`;
    await new ConfigStore(box.env).setCredentials(otherServer, { token, roomId });
    lines = [];
    expect(await setup({ check: true, server: otherServer })).toBe(1);
    expect(lines.join("\n")).toContain(`points at ${t.url}/hook/event`);
  });

  it("refuses to write when /api/me fails, and --force skips the check", async () => {
    const env = { ...box.env, DEVOTCHI_TOKEN: "00000000-0000-4000-8000-000000000000" };
    const err = await setup({}, env).catch((e: unknown) => e);
    expect(err).toBeInstanceOf(CliError);
    expect((err as Error).message).toMatch(/cannot verify .*nothing was changed/);
    await expect(stat(settingsFile())).rejects.toThrow();

    const down = `http://127.0.0.1:${await unusedPort()}`;
    await new ConfigStore(box.env).setCredentials(down, { token, roomId });
    await expect(setup({ server: down })).rejects.toThrow(/cannot reach/);
    await expect(stat(settingsFile())).rejects.toThrow();

    expect(await setup({ server: down, force: true })).toBe(0);
    expect(owned(await readSettingsJson())).toHaveLength(9);
  });

  it("refuses a --dir that does not exist", async () => {
    await expect(setup({ dir: join(box.dir, "missing") })).rejects.toBeInstanceOf(UsageError);
    await expect(stat(join(box.dir, "missing"))).rejects.toThrow();
  });

  it("--env-token writes $DEVOTCHI_TOKEN with allowedEnvVars and prints the export line", async () => {
    await setup({ envToken: true });
    for (const { hook } of owned(await readSettingsJson())) {
      expect(hook.headers).toEqual({ Authorization: "Bearer $DEVOTCHI_TOKEN", "X-Devotchi-Hook": "1" });
      expect(hook.allowedEnvVars).toEqual(["DEVOTCHI_TOKEN"]);
    }
    expect(await readFile(settingsFile(), "utf8")).not.toContain(token);
    expect(lines).toContain(`  export DEVOTCHI_TOKEN=${token}`);
    lines = [];
    expect(await setup({ check: true }, { ...box.env, DEVOTCHI_TOKEN: token })).toBe(0);
    lines = [];
    expect(await setup({ check: true })).toBe(1);
    expect(lines.join("\n")).toContain("not set in this shell");
  });

  it("--share-text and --no-share-text set the room setting", async () => {
    await setup({ shareText: true });
    let room = (await (await t.fetch("/api/room", { token })).json()) as { settings: { shareText: boolean } };
    expect(room.settings.shareText).toBe(true);
    await setup({ shareText: false });
    room = (await (await t.fetch("/api/room", { token })).json()) as { settings: { shareText: boolean } };
    expect(room.settings.shareText).toBe(false);
  });

  it("provisions a token on first use when none is stored", async () => {
    const fresh = await makeSandbox();
    try {
      const freshProject = join(fresh.dir, "p");
      await mkdir(freshProject);
      const code = await runSetup({ dir: freshProject, server: t.url }, { env: fresh.env, out: () => undefined, cwd: fresh.dir });
      expect(code).toBe(0);
      const stored = await new ConfigStore(fresh.env).getCredentials(t.url);
      const text = await readFile(join(freshProject, ".claude", "settings.local.json"), "utf8");
      expect(text).toContain(`Bearer ${stored!.token}`);
    } finally {
      await fresh.cleanup();
    }
  });
});

describe("devotchi setup (CLI)", () => {
  it("--dir writes the hooks; a bad settings file exits 1 with one line", async () => {
    requireBuiltCli();
    const run = await runCli(["setup", "--dir", project, "--server", t.url], box.env);
    expect(run.code, run.stderr).toBe(0);
    expect(owned(await readSettingsJson())).toHaveLength(9);

    const check = await runCli(["setup", "--check", "--dir", project, "--server", t.url], box.env);
    expect(check.code).toBe(0);

    await writeSettingsJson("nope");
    const bad = await runCli(["setup", "--dir", project, "--server", t.url], box.env);
    expect(bad.code).toBe(1);
    expect(bad.stderr.trim().split("\n")).toHaveLength(1);
    expect(bad.stderr).toMatch(/^devotchi: .*not valid JSON/);
  });

  it("--user writes ~/.claude/settings.json under HOME", async () => {
    requireBuiltCli();
    const run = await runCli(["setup", "--user", "--server", t.url], box.env);
    expect(run.code, run.stderr).toBe(0);
    const settings = JSON.parse(await readFile(join(box.home, ".claude", "settings.json"), "utf8")) as Json;
    expect(owned(settings)).toHaveLength(9);
  });

  it("exits 2 on a usage error", async () => {
    requireBuiltCli();
    const run = await runCli(["setup", "--dir", join(box.dir, "nope"), "--server", t.url], box.env);
    expect(run.code).toBe(2);
    expect(run.stderr).toMatch(/does not exist/);
    const bad = await runCli(["start", "--port", "http"], box.env);
    expect(bad.code).toBe(2);
  });
});
