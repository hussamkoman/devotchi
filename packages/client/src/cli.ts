#!/usr/bin/env node
import { Command, CommanderError, InvalidArgumentError } from "commander";
import { DEFAULT_SERVER_PORT, MAX_URGENCY } from "@devotchi/shared";
import { login, logout } from "./commands/account.js";
import { configGet, configPath, configSet, showConfig } from "./commands/config.js";
import { identifyDevice, listDevices, pair, removeDevice, renameDevice } from "./commands/devices.js";
import { runDoctor } from "./commands/doctor.js";
import { runHold, runMode, runNotify } from "./commands/room.js";
import { runCommand } from "./commands/run.js";
import { runStatus } from "./commands/status.js";
import { ConfigStore } from "./config.js";
import { connectServer, type Io } from "./context.js";
import { CliError, UsageError, errorMessage } from "./errors.js";
import type { LocalServer } from "./local.js";
import { startMcpServer } from "./mcp.js";
import { runSetup } from "./setup.js";
import { CLIENT_VERSION } from "./version.js";
import { startWebServer } from "./web-server.js";

const SERVER_OPTION = ["--server <url>", "Devotchi server URL (default: DEVOTCHI_SERVER_URL or the configured server)"] as const;

function portOption(value: string): number {
  if (!/^\d+$/.test(value)) throw new InvalidArgumentError("must be a port number (0..65535)");
  const port = Number(value);
  if (port > 65535) throw new InvalidArgumentError("must be a port number (0..65535)");
  return port;
}

function intOption(min: number, max: number, what: string) {
  return (value: string): number => {
    if (!/^\d+$/.test(value) || Number(value) < min || Number(value) > max) {
      throw new InvalidArgumentError(`must be ${what} (${min}..${max})`);
    }
    return Number(value);
  };
}

function say(line: string): void {
  process.stdout.write(line + "\n");
}

function note(line: string): void {
  process.stderr.write(`[devotchi] ${line}\n`);
}

const io: Io = { env: process.env, out: say, note };

/** Runs a command implementation and sets the exit code it returns. */
function exits<A extends unknown[]>(fn: (...args: A) => Promise<number>) {
  return async (...args: A) => {
    process.exitCode = await fn(...args);
  };
}

const program = new Command();
program.exitOverride();
program
  .name("devotchi")
  .description("Devotchi: a desk pet for AI coding agents")
  .version(CLIENT_VERSION)
  .enablePositionalOptions()
  .showHelpAfterError();

interface StartOptions {
  server?: string;
  local?: boolean;
  lan?: boolean;
  serverPort?: number;
  port?: number;
  open: boolean;
  dev?: boolean;
}

program
  .command("start")
  .description("Start the dashboard; with --local also run the server in this process")
  .option(...SERVER_OPTION)
  .option("--local", "Run the Devotchi server in this process (127.0.0.1)")
  .option("--lan", "With --local: listen on all interfaces so a device on the network can connect")
  .option("--server-port <n>", `With --local: server port (default ${DEFAULT_SERVER_PORT}, 0 picks a free one)`, portOption)
  .option("--port <n>", "Dashboard port (default: webPort from the configuration, 0 picks a free one)", portOption)
  .option("--no-open", "Do not open the browser")
  .option("--dev", "Serve the session secret on /dev-session for the web UI dev server (pnpm run dev)")
  .action(async (opts: StartOptions) => {
    if (opts.local !== true && (opts.lan === true || opts.serverPort !== undefined)) {
      throw new UsageError("--lan and --server-port only apply together with --local");
    }
    if (opts.local === true && opts.server !== undefined) throw new UsageError("use either --local or --server, not both");

    const store = new ConfigStore();
    let local: LocalServer | null = null;
    if (opts.local === true) {
      const { lanAddresses, startLocalServer } = await import("./local.js");
      local = await startLocalServer({ store, lan: opts.lan === true, port: opts.serverPort ?? DEFAULT_SERVER_PORT });
      say(`Local server: ${local.url} (database ${store.dir}/local.db)`);
      if (opts.lan === true) {
        const addresses = lanAddresses();
        for (const address of addresses) say(`  Devices on your network: ws://${address}:${local.server.port}`);
        if (addresses.length === 0) say("  No network interface with an IPv4 address was found.");
      }
    }
    const shutdownLocal = async () => {
      await local?.server.close();
    };

    let dashboard;
    try {
      const { url, creds } = await connectServer(local?.url ?? opts.server, io);
      const config = await store.load();
      dashboard = await startWebServer({
        serverUrl: url,
        token: creds.token,
        port: opts.port ?? config.webPort,
        local: local !== null,
        dev: opts.dev === true,
      });
    } catch (err) {
      await shutdownLocal();
      throw err;
    }
    const dashboardUrl = dashboard.url;
    say(`Dashboard: ${dashboardUrl}`);

    let stopping = false;
    const stop = () => {
      if (stopping) return;
      stopping = true;
      void dashboard
        .close()
        .then(shutdownLocal)
        .catch(() => undefined)
        .finally(() => process.exit(0));
    };
    process.once("SIGINT", stop);
    process.once("SIGTERM", stop);

    if (opts.open) {
      const open = (await import("open")).default;
      await open(dashboardUrl).catch((err: unknown) => note(`could not open a browser: ${errorMessage(err)}`));
    }
  });

program
  .command("mcp")
  .description("Run the MCP server on stdio (claude mcp add devotchi -- devotchi mcp)")
  .option(...SERVER_OPTION)
  .action(async (opts: { server?: string }) => {
    await startMcpServer({ server: opts.server });
  });

program
  .command("setup")
  .description("Install, check or remove the Claude Code hooks")
  .option(...SERVER_OPTION)
  .option("--dir <path>", "Project directory (writes <dir>/.claude/settings.local.json; default: current directory)")
  .option("--user", "Write ~/.claude/settings.json instead (all projects)")
  .option("--remove", "Remove the Devotchi hooks")
  .option("--check", "Report whether the installed hooks are current; exit 1 if not")
  .option("--share-text", "Let Stop notifications include the first line of Claude's reply")
  .option("--no-share-text", "Stop including Claude's reply in Stop notifications")
  .option("--env-token", "Reference $DEVOTCHI_TOKEN in the hooks instead of writing the token")
  .option("--force", "Write the hooks without verifying the server and token")
  .action(async (opts: Record<string, unknown>) => {
    const code = await runSetup(
      {
        dir: opts.dir as string | undefined,
        user: opts.user as boolean | undefined,
        server: opts.server as string | undefined,
        remove: opts.remove as boolean | undefined,
        check: opts.check as boolean | undefined,
        shareText: opts.shareText as boolean | undefined,
        envToken: opts.envToken as boolean | undefined,
        force: opts.force as boolean | undefined,
      },
      { env: process.env, out: say, cwd: process.cwd() },
    );
    process.exitCode = code;
  });

program
  .command("pair")
  .description("Confirm a code shown by a device, or create one with --new for the device console")
  .argument("[code]", "Pairing code shown on the device")
  .option(...SERVER_OPTION)
  .option("--new", "Create a code for `pair <CODE>` on the device console")
  .option("--name <name>", "Name for the device")
  .option("--json", "Print the result as JSON")
  .action(exits((code: string | undefined, opts: { server?: string; new?: boolean; name?: string; json?: boolean }) => pair(code, opts, io)));

const devices = program.command("devices").description("List, rename, identify or remove paired devices");
devices
  .command("list", { isDefault: true })
  .description("List paired devices with liveness (online, last seen, last ack), battery, RSSI and firmware")
  .option(...SERVER_OPTION)
  .option("--json", "Print the devices as JSON")
  .action(exits((opts: { server?: string; json?: boolean }) => listDevices(opts, io)));
devices
  .command("rename")
  .description("Rename a device")
  .argument("<id>", "Device id, e.g. dv-e356e4")
  .argument("<name>", "New name (1..24 characters)")
  .option(...SERVER_OPTION)
  .action(exits((id: string, name: string, opts: { server?: string }) => renameDevice(id, name, opts, io)));
devices
  .command("identify")
  .description("Make a device show its name and buzz")
  .argument("<id>", "Device id")
  .option(...SERVER_OPTION)
  .option("--duration <ms>", "How long (1000..10000 ms)", intOption(1000, 10_000, "milliseconds"))
  .action(exits((id: string, opts: { server?: string; duration?: number }) => identifyDevice(id, opts, io)));
devices
  .command("rm")
  .description("Unpair a device: revoke its token (it returns to pairing)")
  .argument("<id>", "Device id")
  .option(...SERVER_OPTION)
  .option("--yes", "Confirm the removal")
  .action(exits((id: string, opts: { server?: string; yes?: boolean }) => removeDevice(id, opts, io)));

program
  .command("status")
  .description("Server, token, peers, room mode and hold time, device liveness and the last request outcome")
  .option(...SERVER_OPTION)
  .option("--json", "Print the status as JSON")
  .action(exits((opts: { server?: string; json?: boolean }) => runStatus(opts, io)));

program
  .command("doctor")
  .description("Check configuration, server, token, WebSocket, hooks and devices; read-only, exits 1 on any failure")
  .option(...SERVER_OPTION)
  .option("--dir <path>", "Project directory whose .claude/settings.local.json is checked (default: current directory)")
  .option("--json", "Print the checks as JSON")
  .action(exits((opts: { server?: string; dir?: string; json?: boolean }) => runDoctor(opts, io, process.cwd())));

program
  .command("mode")
  .description("Show or set the room mode: ask (requests go to devices) or desk (straight to the terminal)")
  .argument("[mode]", "ask or desk")
  .option(...SERVER_OPTION)
  .option("--json", "Print the mode as JSON")
  .action(exits((mode: string | undefined, opts: { server?: string; json?: boolean }) => runMode(mode, opts, io)));

program
  .command("hold")
  .description("Show or set how long a request waits for an answer on a device (10..300 s)")
  .argument("[seconds]", "New hold time in seconds")
  .option(...SERVER_OPTION)
  .option("--json", "Print the hold time as JSON")
  .action(exits((seconds: string | undefined, opts: { server?: string; json?: boolean }) => runHold(seconds, opts, io)));

program
  .command("notify")
  .description("Send a notification to the pet from a script (POST /api/notify)")
  .argument("<event>", "Event name, [a-z0-9_]+, e.g. deploy_complete")
  .argument("[detail]", "Short detail text")
  .option(...SERVER_OPTION)
  .option("--mood <mood>", "Mood after the reaction (default: by event class)")
  .option("--urgent", "Toast and vibration (urgency 2)")
  .option("--urgency <n>", "0 mood only, 1 toast, 2 toast + vibration, 3 sticky banner", intOption(0, MAX_URGENCY, "an urgency"))
  .option("--source <label>", "Origin label, e.g. ci")
  .option("--json", "Print the result as JSON")
  .action(
    exits(
      (
        event: string,
        detail: string | undefined,
        opts: { server?: string; mood?: string; urgent?: boolean; urgency?: number; source?: string; json?: boolean },
      ) => runNotify(event, detail, opts, io),
    ),
  );

program
  .command("run")
  .description("Run a command, notify the pet when it ends (<label>_succeeded or <label>_failed) and exit with its code")
  .argument("<cmd...>", "The command and its arguments, after --")
  .option(...SERVER_OPTION)
  .option("--name <label>", "Event label (default: the command name)")
  .passThroughOptions()
  .action(exits((cmd: string[], opts: { server?: string; name?: string }) => runCommand(cmd, opts, io)));

program
  .command("sim")
  .description("Headless virtual device speaking the device protocol; prints one JSON line per event")
  .option(...SERVER_OPTION)
  .option("--name <name>", "Device id becomes sim-<name> (a-z, 0-9, -; max 12)", "local")
  .option("--approve <policy>", "all | none (reject) | defer | after=<ms> | manual (stdin: approve|reject|defer [id])", "all")
  .option("--code <CODE>", "Pair with a code from `devotchi pair --new` (flow B) instead of showing one")
  .option("--token <token>", "Connect with this token, no pairing (a cli token makes a memory-only consumer)")
  .option("--state <file>", "Keep the paired credentials in this file (default: memory only)")
  .option("--repair", "After the token is revoked (4003), pair again instead of exiting with code 3")
  .option("--no-ack", "Never send holding (tests the server's ack timeout)")
  .option("--naive-ack", "One holding per request without the debounce (tests the server's holding bucket)")
  .option("--stop-heartbeat-after <ms>", "Stop sending holding this long after the first request", intOption(0, 86_400_000, "milliseconds"))
  .option("--heartbeat-interval <ms>", "Holding heartbeat interval (default 4000; shorter for tests)", intOption(50, 60_000, "milliseconds"))
  .option("--telemetry-interval <ms>", "Telemetry interval (default 60000)", intOption(100, 3_600_000, "milliseconds"))
  .option(
    "--battery <pct,state>",
    "Scripted battery values, one per telemetry frame (repeatable; e.g. 82,discharging or absent)",
    (value: string, previous: string[] = []) => [...previous, value],
  )
  .action(
    exits(async (opts: Record<string, unknown>) => {
      const sim = await import("./sim.js");
      if (opts.ack === false && opts.naiveAck === true) throw new UsageError("use either --no-ack or --naive-ack");
      const { url } = await new ConfigStore(io.env).resolveServer(opts.server as string | undefined);
      return sim.runSim(
        {
          server: url,
          name: opts.name as string,
          approve: sim.parsePolicy(opts.approve as string),
          code: opts.code as string | undefined,
          token: opts.token as string | undefined,
          state: opts.state as string | undefined,
          ack: opts.ack === false ? "none" : opts.naiveAck === true ? "naive" : "debounced",
          stopHeartbeatAfter: opts.stopHeartbeatAfter as number | undefined,
          heartbeatInterval: opts.heartbeatInterval as number | undefined,
          telemetryInterval: opts.telemetryInterval as number | undefined,
          battery: (opts.battery as string[] | undefined)?.map(sim.parseBattery),
          repair: opts.repair as boolean | undefined,
          version: CLIENT_VERSION,
        },
        say,
      );
    }),
  );

program
  .command("login")
  .description("Log in to a multi-user server and store a token for this computer (the login session is discarded)")
  .option(...SERVER_OPTION)
  .option("--email <email>", "Account email (prompted when omitted)")
  .action(
    exits((opts: { server?: string; email?: string }) => login(opts, io, { input: process.stdin, output: process.stderr })),
  );

program
  .command("logout")
  .description("Remove the stored credentials for a server")
  .option(...SERVER_OPTION)
  .option("--revoke", "Also delete the token on the server")
  .action(exits((opts: { server?: string; revoke?: boolean }) => logout(opts, io)));

const config = program
  .command("config")
  .description("Show the configuration, or get/set a key")
  .action(exits(() => showConfig(io)));
config
  .command("get")
  .description("Print one key (serverUrl, webPort)")
  .argument("<key>")
  .action(exits((key: string) => configGet(key, io)));
config
  .command("set")
  .description("Change one key (serverUrl, webPort)")
  .argument("<key>")
  .argument("<value>")
  .action(exits((key: string, value: string) => configSet(key, value, io)));
config
  .command("path")
  .description("Print the path of the configuration file")
  .action(exits(() => configPath(io)));

async function main(): Promise<void> {
  try {
    await program.parseAsync(process.argv);
  } catch (err) {
    if (err instanceof CommanderError) {
      // Commander has already printed the message (or the help text).
      const ok = err.code === "commander.helpDisplayed" || err.code === "commander.version" || err.code === "commander.help";
      process.exit(ok ? 0 : 2);
    }
    process.stderr.write(`devotchi: ${errorMessage(err)}\n`);
    process.exit(err instanceof CliError ? err.exitCode : 1);
  }
}

void main();
