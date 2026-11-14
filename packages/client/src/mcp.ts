// `devotchi mcp`: MCP server on stdio. The stdio transport starts first and
// the server link is set up in the background, so an unreachable or
// misconfigured server never prevents `initialize`. stdout carries only
// JSON-RPC; every log line goes to stderr.

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import {
  MAX_URGENCY,
  MOODS,
  NOTIFY_EVENT_RE,
  buildMood,
  buildNotify,
  buildStatus,
  type Room,
} from "@devotchi/shared";
import { ApiClient, ApiError } from "./api.js";
import { ConfigStore, type Env } from "./config.js";
import { ProducerConnection, type ContentMessage, type LinkState } from "./connection.js";
import { ServerCredentialSource } from "./credentials.js";
import { errorMessage } from "./errors.js";
import { deviceLiveness } from "./format.js";
import { Outbox } from "./outbox.js";
import { CLIENT_VERSION } from "./version.js";

export interface McpOptions {
  server?: string;
  env?: Env;
}

export interface ToolResult {
  ok: boolean;
  delivered: boolean;
  consumers: number;
  devices: number;
  throttled?: true;
  link?: LinkState;
  note?: string;
  error?: string;
}

function log(line: string): void {
  process.stderr.write(`[devotchi] ${line}\n`);
}

/** The server link, created once the configuration has been read. */
class Link {
  connection: ProducerConnection | null = null;
  credentials: ServerCredentialSource | null = null;
  outbox: Outbox | null = null;
  /** Set when the link cannot be created at all (unreadable configuration). */
  fatal: string | null = null;
  /** Settles once the configuration was read; it never waits for the network. */
  private ready: Promise<void> = Promise.resolve();

  begin(options: McpOptions): void {
    this.ready = this.start(options);
  }

  private async start(options: McpOptions): Promise<void> {
    try {
      const store = new ConfigStore(options.env ?? process.env);
      // Read even when --server is given, so an unreadable file is reported by the first tool call.
      await store.load();
      const { url } = await store.resolveServer(options.server);
      this.credentials = new ServerCredentialSource(store, url, log);
      const connection = new ProducerConnection({
        serverUrl: url,
        credentials: this.credentials,
        version: CLIENT_VERSION,
        kind: "mcp",
        log,
      });
      this.connection = connection;
      this.outbox = new Outbox(
        (msg) => connection.send(msg),
        () => ({ consumers: connection.peers?.consumers ?? 0, devices: connection.peers?.devices ?? 0 }),
      );
      connection.start();
    } catch (err) {
      this.fatal = errorMessage(err);
      log(`cannot connect: ${this.fatal}`);
    }
  }

  async submit(msg: ContentMessage): Promise<ToolResult> {
    await this.ready;
    const connection = this.connection;
    if (connection === null || this.outbox === null) {
      return {
        ok: false,
        delivered: false,
        consumers: 0,
        devices: 0,
        error: this.fatal ?? "the Devotchi link is not set up",
      };
    }
    if (connection.state === "stopped") {
      return {
        ok: false,
        delivered: false,
        consumers: 0,
        devices: 0,
        link: "stopped",
        error: connection.lastError ?? "the link to the Devotchi server stopped",
      };
    }
    const result = this.outbox.submit(msg);
    const delivered = result.sent && result.consumers >= 1;
    const out: ToolResult = {
      ok: true,
      delivered,
      consumers: result.consumers,
      devices: result.devices,
      link: connection.state,
    };
    if (result.throttled) {
      out.throttled = true;
      out.note = "queued: updates are paced to at most 4 per second; a newer status or mood replaces a queued one";
    } else if (!result.sent) {
      out.note = `not connected to ${connection.serverUrl}${connection.lastError !== null ? ` (${connection.lastError})` : ""}; the latest status and mood are sent when the link returns`;
    } else if (!delivered) {
      out.note = "no device or dashboard is connected to this room";
    }
    return out;
  }

  async roomSummary(): Promise<Record<string, unknown>> {
    await this.ready;
    if (this.credentials === null) return { ok: false, error: this.fatal ?? "the Devotchi link is not set up" };
    const api = new ApiClient(this.credentials.serverUrl);
    try {
      let creds = await this.credentials.get();
      let room: Room;
      try {
        room = await api.withToken(creds.token).room();
      } catch (err) {
        if (!(err instanceof ApiError) || err.status !== 401) throw err;
        creds = await this.credentials.renew(creds.token);
        room = await api.withToken(creds.token).room();
      }
      return summariseRoom(room, Date.now());
    } catch (err) {
      return { ok: false, error: errorMessage(err) };
    }
  }

  async close(): Promise<void> {
    this.outbox?.close();
    await this.connection?.close();
  }
}

export function summariseRoom(room: Room, now: number): Record<string, unknown> {
  return {
    ok: true,
    mode: room.settings.mode,
    holdMs: room.settings.holdMs,
    devices: room.devices.map((device) => deviceLiveness(device, now)),
    pending: room.pending.length,
    consumers: room.peers.consumers,
  };
}

function textResult(value: unknown) {
  return { content: [{ type: "text" as const, text: JSON.stringify(value) }] };
}

const moodSchema = z.enum(MOODS);
const MOOD_LIST = MOODS.join(", ");

function createMcpServer(link: Link): McpServer {
  const server = new McpServer(
    { name: "devotchi", version: CLIENT_VERSION },
    {
      instructions:
        "Devotchi is a desk pet that mirrors what you are doing. Call show_status when you start a task or subtask, " +
        "notify when tests or builds pass or fail or a task completes, and set_mood to reflect the overall state of the project. " +
        "Permission prompts are handled by Claude Code hooks, not by these tools.",
    },
  );

  server.registerTool(
    "show_status",
    {
      title: "Show status",
      description:
        "Show a one-line description of what you are doing right now on the Devotchi pet (max 200 UTF-8 bytes; longer text is cut). " +
        "If you omit mood, the status is sent without a mood and the pet keeps its current mood; pass a mood only when it should change. " +
        `Moods: ${MOOD_LIST}. Result: {ok, delivered, consumers, devices}; delivered is true only when a device or dashboard is connected.`,
      inputSchema: {
        msg: z.string().min(1).describe("What you are doing, e.g. 'Running the test suite'"),
        mood: moodSchema.optional().describe("Optional new mood; omit to keep the current one"),
      },
    },
    async ({ msg, mood }) => textResult(await link.submit(buildStatus({ msg, mood }))),
  );

  server.registerTool(
    "notify",
    {
      title: "Notify",
      description:
        "Make the pet react to an event such as tests_passed, tests_failed, build_succeeded, build_failed or task_complete, " +
        "then settle into the given mood. urgency: 0 mood only, 1 toast (default), 2 toast + vibration, 3 sticky banner + vibration until a button is pressed. " +
        `Moods: ${MOOD_LIST}.`,
      inputSchema: {
        event: z
          .string()
          .regex(NOTIFY_EVENT_RE, "lower-case letters, digits and underscores only")
          .describe("Event name, [a-z0-9_]+, max 40 bytes"),
        mood: moodSchema.describe("Mood the pet settles into after the reaction"),
        detail: z.string().optional().describe("Short detail, e.g. '3 of 47 failing' (max 300 bytes)"),
        urgency: z.number().int().min(0).max(MAX_URGENCY).optional().describe("0..3, default 1"),
      },
    },
    async ({ event, mood, detail, urgency }) => textResult(await link.submit(buildNotify({ event, mood, detail, urgency }))),
  );

  server.registerTool(
    "set_mood",
    {
      title: "Set mood",
      description:
        "Set the pet's mood to reflect the overall project state (happy when green, sad when failing, focused in deep work, " +
        `thinking while planning, anxious before risky changes, sleeping when idle). Moods: ${MOOD_LIST}.`,
      inputSchema: {
        mood: moodSchema.describe("The new mood"),
        reason: z.string().optional().describe("Why, e.g. 'Long build running' (max 200 bytes)"),
      },
    },
    async ({ mood, reason }) => textResult(await link.submit(buildMood({ mood, reason }))),
  );

  server.registerTool(
    "get_device_state",
    {
      title: "Get device state",
      description:
        "Read the Devotchi room: mode (ask or desk), hold time, paired devices with online state, seconds since last seen and " +
        "since the last request acknowledgement, battery, the number of pending permission requests and connected consumers.",
      inputSchema: {},
      annotations: { readOnlyHint: true },
    },
    async () => textResult(await link.roomSummary()),
  );

  return server;
}

export async function startMcpServer(options: McpOptions = {}): Promise<void> {
  const link = new Link();
  const server = createMcpServer(link);
  await server.connect(new StdioServerTransport());
  log(`MCP server ${CLIENT_VERSION} ready on stdio`);
  link.begin(options);

  let exiting = false;
  const shutdown = () => {
    if (exiting) return;
    exiting = true;
    void link
      .close()
      .catch(() => undefined)
      .finally(() => process.exit(0));
  };
  process.stdin.once("end", shutdown);
  process.stdin.once("close", shutdown);
  process.once("SIGINT", shutdown);
  process.once("SIGTERM", shutdown);
}
