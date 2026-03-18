// Devotchi MCP server — exposes voluntary tools for AI coding agents
// Approvals/permissions are handled automatically via Claude Code hooks
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import WebSocket from "ws";
import { readFile, writeFile, mkdir } from "node:fs/promises";
import { homedir } from "node:os";
import { join } from "node:path";
import {
  type DevotchiMessage,
  type PetMood,
  type StatusMood,
  generatePairingCode,
  DEFAULT_APPROVAL_TIMEOUT,
} from "@devotchi/shared";

// --- Config ---

interface DevotchiConfig {
  pairingCode: string;
  brokerUrl: string;
  approvalTimeout: number;
  logLevel: "debug" | "info" | "error";
}

const CONFIG_DIR = join(homedir(), ".devotchi");
const CONFIG_PATH = join(CONFIG_DIR, "config.json");

async function loadConfig(): Promise<DevotchiConfig> {
  const defaults: DevotchiConfig = {
    pairingCode: generatePairingCode(),
    brokerUrl: "ws://localhost:8080",
    approvalTimeout: DEFAULT_APPROVAL_TIMEOUT,
    logLevel: "info",
  };

  try {
    const raw = await readFile(CONFIG_PATH, "utf-8");
    return { ...defaults, ...JSON.parse(raw) };
  } catch {
    await mkdir(CONFIG_DIR, { recursive: true });
    await writeFile(CONFIG_PATH, JSON.stringify(defaults, null, 2));
    return defaults;
  }
}

// --- Logging ---

let logLevel: "debug" | "info" | "error" = "info";
const LOG_LEVELS = { debug: 0, info: 1, error: 2 };

function log(level: "debug" | "info" | "error", ...args: unknown[]): void {
  if (LOG_LEVELS[level] >= LOG_LEVELS[logLevel]) {
    console.error(`[devotchi:${level}]`, ...args);
  }
}

// --- Broker Connection ---

let ws: WebSocket | null = null;
let connected = false;
let config: DevotchiConfig;
let reconnectDelay = 1000;
const MAX_RECONNECT_DELAY = 30_000;

let shutdownRequested = false;

function connectToBroker(): void {
  if (ws) {
    ws.removeAllListeners();
    ws.close();
  }

  log("info", `Connecting to broker at ${config.brokerUrl}`);
  ws = new WebSocket(config.brokerUrl);

  ws.on("open", () => {
    connected = true;
    reconnectDelay = 1000;
    log("info", `Connected to broker, registering as producer with code ${config.pairingCode}`);
    sendMessage({
      type: "register",
      role: "producer",
      pairingCode: config.pairingCode,
    });
  });

  ws.on("message", (data: Buffer) => {
    try {
      const msg: DevotchiMessage = JSON.parse(data.toString());
      handleBrokerMessage(msg);
    } catch (err) {
      log("error", "Failed to parse broker message", err);
    }
  });

  ws.on("close", (code, reason) => {
    connected = false;
    if (shutdownRequested) return;
    if (code === 4001) {
      log("info", "Replaced by another producer, not reconnecting");
      return;
    }
    log("info", `Disconnected from broker (code=${code}), reconnecting...`);
    scheduleReconnect();
  });

  ws.on("error", (err) => {
    log("error", "Broker connection error", err.message);
  });
}

function scheduleReconnect(): void {
  setTimeout(() => {
    reconnectDelay = Math.min(reconnectDelay * 2, MAX_RECONNECT_DELAY);
    connectToBroker();
  }, reconnectDelay);
}

function sendMessage(msg: DevotchiMessage): boolean {
  if (!ws || ws.readyState !== WebSocket.OPEN) return false;
  ws.send(JSON.stringify(msg));
  return true;
}

function handleBrokerMessage(msg: DevotchiMessage): void {
  switch (msg.type) {
    case "ping":
      sendMessage({ type: "pong" });
      break;
    case "register_ack":
      log("info", `Registered as ${msg.role}, ${msg.connectedPeers} peer(s) connected`);
      break;
    case "error":
      log("error", `Broker error: ${msg.message} (${msg.code})`);
      break;
    default:
      log("debug", "Unhandled message type", msg.type);
  }
}

// --- MCP Server ---

const server = new McpServer({
  name: "devotchi",
  version: "0.1.0",
});

server.tool(
  "show_status",
  "Update the Devotchi pet display with what the agent is currently doing",
  {
    msg: z.string().max(200).describe("What the agent is doing"),
    mood: z
      .enum(["focused", "working", "thinking", "idle"])
      .optional()
      .default("working")
      .describe("Agent activity mood"),
  },
  async ({ msg, mood }) => {
    const sent = sendMessage({
      type: "status",
      msg,
      mood: mood as StatusMood,
      timestamp: new Date().toISOString(),
    });

    return {
      content: [
        {
          type: "text" as const,
          text: JSON.stringify({ ok: true, brokerConnected: sent }),
        },
      ],
    };
  }
);

server.tool(
  "notify",
  "Send a notification to the Devotchi pet (tests passed, build failed, etc.)",
  {
    event: z.string().describe("Event identifier (e.g. tests_passed, build_failed)"),
    mood: z
      .enum(["happy", "sad", "excited", "anxious", "sleeping", "angry", "proud", "idle"])
      .describe("How the pet should feel"),
    detail: z.string().max(1000).optional().describe("Additional info to display"),
  },
  async ({ event, mood, detail }) => {
    const sent = sendMessage({
      type: "notify",
      event,
      mood: mood as PetMood,
      detail,
      timestamp: new Date().toISOString(),
    });

    return {
      content: [
        {
          type: "text" as const,
          text: JSON.stringify({ ok: true, brokerConnected: sent }),
        },
      ],
    };
  }
);

server.tool(
  "set_mood",
  "Set the Devotchi pet's mood to reflect overall project state",
  {
    mood: z
      .enum(["happy", "sad", "excited", "anxious", "sleeping", "angry", "proud", "idle"])
      .describe("The mood to set"),
    reason: z.string().max(1000).optional().describe("Why this mood was set"),
  },
  async ({ mood, reason }) => {
    const sent = sendMessage({
      type: "mood",
      mood: mood as PetMood,
      reason,
      timestamp: new Date().toISOString(),
    });

    return {
      content: [
        {
          type: "text" as const,
          text: JSON.stringify({ ok: true, brokerConnected: sent }),
        },
      ],
    };
  }
);

// --- Start ---

async function main(): Promise<void> {
  config = await loadConfig();
  logLevel = config.logLevel;

  log("info", `Pairing code: ${config.pairingCode}`);
  log("info", `Broker URL: ${config.brokerUrl}`);

  connectToBroker();

  const transport = new StdioServerTransport();
  await server.connect(transport);
  log("info", "MCP server started on stdio");

  process.stdin.on("end", () => {
    shutdownRequested = true;
    if (ws) ws.close();
    process.exit(0);
  });
}

main().catch((err) => {
  console.error("Fatal error:", err);
  process.exit(1);
});
