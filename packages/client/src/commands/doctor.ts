// `devotchi doctor`: ordered, read-only checks, each with a fix line.

import { readFile } from "node:fs/promises";
import { homedir } from "node:os";
import { join, resolve } from "node:path";
import WebSocket from "ws";
import {
  PROTO_VERSION,
  buildPing,
  buildRegister,
  serialize,
  type ClientConfig,
  type HealthResponse,
  type Room,
} from "@devotchi/shared";
import { ApiClient, ApiError } from "../api.js";
import { ConfigStore, serverWsUrl } from "../config.js";
import { printJson, type Io } from "../context.js";
import { errorMessage } from "../errors.js";
import { ago, deviceLine, secondsSince } from "../format.js";
import { DEVOTCHI_HOOKS, checkDevotchiHooks, readSettings, settingsPath } from "../hooks.js";
import { CLIENT_VERSION } from "../version.js";

export type Level = "PASS" | "WARN" | "FAIL" | "INFO" | "SKIP";

export interface Check {
  id: string;
  level: Level;
  message: string;
  fix?: string;
}

export interface DoctorOptions {
  server?: string;
  dir?: string;
  json?: boolean;
}

const LOW_BATTERY_PCT = 20;
const WS_TIMEOUT_MS = 5000;

/** Connects as a `cli` producer, registers and times one ping/pong round trip. */
function wsRoundTrip(serverUrl: string, token: string): Promise<number> {
  return new Promise((resolvePromise, reject) => {
    const ws = new WebSocket(serverWsUrl(serverUrl), { headers: { Authorization: `Bearer ${token}` }, handshakeTimeout: WS_TIMEOUT_MS });
    let pingAt = 0;
    const fail = (reason: string) => {
      clearTimeout(timer);
      ws.terminate();
      reject(new Error(reason));
    };
    const timer = setTimeout(() => fail(`no answer within ${WS_TIMEOUT_MS / 1000} s`), WS_TIMEOUT_MS);
    ws.on("unexpected-response", (_req, res) => fail(`upgrade answered with HTTP ${res.statusCode ?? 0}`));
    ws.on("error", (err) => fail(err.message));
    ws.on("close", (code) => fail(`closed with code ${code}`));
    ws.on("open", () => {
      ws.send(serialize(buildRegister({ role: "producer", client: { kind: "cli", version: CLIENT_VERSION, project: "doctor" } })));
    });
    ws.on("message", (data) => {
      let msg: { type?: unknown; code?: unknown; message?: unknown };
      try {
        msg = JSON.parse(data.toString()) as typeof msg;
      } catch {
        return;
      }
      if (msg.type === "register_ack") {
        pingAt = performance.now();
        ws.send(serialize(buildPing()));
      } else if (msg.type === "pong" && pingAt > 0) {
        const latency = performance.now() - pingAt;
        clearTimeout(timer);
        ws.removeAllListeners("close");
        ws.close(1000);
        resolvePromise(latency);
      } else if (msg.type === "error") {
        fail(`server error ${String(msg.code)}: ${String(msg.message)}`);
      }
    });
  });
}

/** Whether a `devotchi` MCP server appears in the Claude Code configuration files we know of. */
async function mcpRegistered(dir: string, home: string): Promise<boolean> {
  const read = async (path: string): Promise<Record<string, unknown> | null> => {
    try {
      return JSON.parse(await readFile(path, "utf8")) as Record<string, unknown>;
    } catch {
      return null;
    }
  };
  const has = (servers: unknown) => typeof servers === "object" && servers !== null && "devotchi" in servers;
  const project = await read(join(dir, ".mcp.json"));
  if (has(project?.mcpServers)) return true;
  const user = await read(join(home, ".claude.json"));
  if (user === null) return false;
  if (has(user.mcpServers)) return true;
  const projects = user.projects as Record<string, { mcpServers?: unknown }> | undefined;
  return has(projects?.[dir]?.mcpServers);
}

export async function runDoctor(options: DoctorOptions, io: Io, cwd: string): Promise<number> {
  const checks: Check[] = [];
  const add = (check: Check) => checks.push(check);
  const dir = resolve(cwd, options.dir ?? ".");
  const home = io.env.HOME !== undefined && io.env.HOME !== "" ? io.env.HOME : homedir();

  // 1. Configuration.
  const store = new ConfigStore(io.env);
  let url: string | null = null;
  let stored: ClientConfig | null = null;
  try {
    stored = (await store.read()).config;
    url = (await store.resolveServer(options.server, stored)).url;
    add({ id: "config", level: "PASS", message: `configuration ${store.path} is valid; server ${url}` });
  } catch (err) {
    add({ id: "config", level: "FAIL", message: errorMessage(err), fix: `fix or remove ${store.path}, or pass --server <url>` });
  }

  // 2. Server health.
  let health: HealthResponse | null = null;
  if (url !== null) {
    try {
      health = await new ApiClient(url).health();
      add({ id: "health", level: "PASS", message: `server ${health.version} speaks protocol ${PROTO_VERSION} (${health.mode})` });
    } catch (err) {
      add({ id: "health", level: "FAIL", message: errorMessage(err), fix: "start the server (devotchi start --local) or check --server" });
    }
  } else add({ id: "health", level: "SKIP", message: "no server URL" });

  // 3. Token.
  let token: string | null = null;
  if (url !== null && health !== null) {
    const candidate = store.envToken ?? stored?.servers[url]?.token ?? null;
    const fixToken = health.mode === "multi-user" ? `devotchi login --server ${url}` : `devotchi setup --server ${url}`;
    if (candidate === null) {
      add({ id: "token", level: "FAIL", message: `no token stored for ${url}`, fix: fixToken });
    } else {
      try {
        const me = await new ApiClient(url, { token: candidate }).me();
        token = candidate;
        add({ id: "token", level: "PASS", message: `token is valid for room ${me.roomId}${store.envToken !== undefined ? " (DEVOTCHI_TOKEN)" : ""}` });
      } catch (err) {
        const rejected = err instanceof ApiError && err.status === 401;
        add({
          id: "token",
          level: "FAIL",
          message: rejected ? "the server rejects the token" : errorMessage(err),
          fix: rejected ? fixToken : "check that the server is reachable",
        });
      }
    }
  } else add({ id: "token", level: "SKIP", message: "server not reachable" });

  // 4. WebSocket.
  if (url !== null && token !== null) {
    try {
      const latency = await wsRoundTrip(url, token);
      add({ id: "websocket", level: "PASS", message: `WebSocket register and ping/pong in ${latency.toFixed(1)} ms` });
    } catch (err) {
      add({
        id: "websocket",
        level: "FAIL",
        message: `WebSocket ${serverWsUrl(url)}: ${errorMessage(err)}`,
        fix: "a reverse proxy must forward WebSocket upgrades on /ws",
      });
    }
  } else add({ id: "websocket", level: "SKIP", message: "no valid token" });

  let room: Room | null = null;
  if (url !== null && token !== null) {
    room = await new ApiClient(url, { token }).room().catch(() => null);
  }

  // 5. Hooks.
  if (url !== null) {
    const projectPath = settingsPath({ dir });
    const userPath = join(home, ".claude", "settings.json");
    try {
      const project = await readSettings(projectPath);
      const report = checkDevotchiHooks(project.data, url, token, io.env);
      const user = report.status === "missing" ? checkDevotchiHooks((await readSettings(userPath)).data, url, token, io.env) : null;
      const effective = user !== null && user.status !== "missing" ? user : report;
      const where = effective === user ? userPath : projectPath;
      if (effective.status === "current") {
        // checkDevotchiHooks requires the 310 s PermissionRequest timeout, which covers the largest hold time + 10 s.
        add({ id: "hooks", level: "PASS", message: `all ${DEVOTCHI_HOOKS.length} hooks in ${where} are current` });
      } else if (effective.status === "missing") {
        add({ id: "hooks", level: "FAIL", message: `no Devotchi hooks in ${projectPath} or ${userPath}`, fix: `devotchi setup --dir ${dir}` });
      } else {
        const bad = effective.events
          .filter((e) => e.state !== "ok")
          .map((e) => `${e.event} ${e.state}${e.reason !== undefined ? ` (${e.reason})` : ""}`)
          .concat(effective.extra.map((e) => `${e} is not a Devotchi hook event`));
        add({ id: "hooks", level: "FAIL", message: `hooks in ${where} are out of date: ${bad.join("; ")}`, fix: `devotchi setup --dir ${dir}` });
      }
    } catch (err) {
      add({ id: "hooks", level: "FAIL", message: errorMessage(err), fix: "fix the settings file, then run devotchi setup" });
    }
  } else add({ id: "hooks", level: "SKIP", message: "no server URL" });

  // 6. Devices.
  const now = Date.now();
  if (room !== null) {
    const online = room.devices.filter((d) => d.online);
    if (room.devices.length === 0) {
      add({ id: "devices", level: "WARN", message: "no device is paired", fix: "devotchi pair <CODE> with the code on the device screen, or devotchi sim" });
    } else {
      add({
        id: "devices",
        level: online.length > 0 ? "PASS" : "WARN",
        message: `${room.devices.length} paired, ${online.length} online; ` + room.devices.map((d) => deviceLine(d, now)).join("; "),
        ...(online.length > 0 ? {} : { fix: "switch the device on and check its WiFi" }),
      });
    }
    for (const device of room.devices) {
      const battery = device.telemetry?.battery;
      if (battery !== undefined && battery.state === "discharging" && battery.pct !== null && battery.pct < LOW_BATTERY_PCT) {
        add({ id: "battery", level: "WARN", message: `${device.id} battery at ${battery.pct}%`, fix: "charge the device" });
      }
    }
    const mcp = await mcpRegistered(dir, home);
    add({
      id: "mcp",
      level: mcp ? "PASS" : "INFO",
      message: mcp ? "the devotchi MCP server is registered with Claude Code" : "no devotchi MCP server found in .mcp.json or ~/.claude.json",
      ...(mcp ? {} : { fix: "claude mcp add devotchi -- devotchi mcp" }),
    });

    // 7. Hold time advice (protocol 8.4).
    const holdS = room.settings.holdMs / 1000;
    const seen = room.diagnostics.dialogWhilePending;
    if (seen.count === 0) {
      add({
        id: "hold",
        level: "INFO",
        message:
          `No sign that Claude Code shows its own prompt while Devotchi holds a request; with hold time ${holdS} s you wait at most ` +
          `${holdS} s at the keyboard (desk mode or ACTION-hold on the device skips it)`,
      });
    } else {
      const last = seen.lastSeenAt !== null ? `${seen.lastSeenAt}, ${ago(secondsSince(seen.lastSeenAt, now))}` : "unknown";
      add({
        id: "hold",
        level: "INFO",
        message:
          `Claude Code was seen showing its prompt while a request was pending (${seen.count} time${seen.count === 1 ? "" : "s"}, last ${last}): ` +
          "the terminal stays usable, you can raise the hold time",
        fix: `devotchi hold <seconds> (currently ${holdS} s, up to 300 s)`,
      });
    }
  } else {
    add({ id: "devices", level: "SKIP", message: "room not readable" });
    add({ id: "hold", level: "SKIP", message: "room not readable" });
  }

  const failed = checks.some((c) => c.level === "FAIL");
  if (options.json === true) printJson(io, { ok: !failed, checks });
  else {
    for (const check of checks) {
      io.out(`${check.level.padEnd(4)}  ${check.id.padEnd(9)} ${check.message}`);
      if (check.fix !== undefined && check.level !== "PASS") io.out(`      ${"".padEnd(9)} fix: ${check.fix}`);
    }
    io.out(failed ? "Some checks failed." : "No failures.");
  }
  return failed ? 1 : 0;
}
