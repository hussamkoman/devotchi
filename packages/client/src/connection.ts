import { basename } from "node:path";
import WebSocket from "ws";
import {
  buildPong,
  buildRegister,
  serialize,
  type InboundMessage,
  type ServerToConsumerMessage,
} from "@devotchi/shared";
import type { ClientSettings } from "./config.js";

let ws: WebSocket | null = null;
let connected = false;
let reconnectDelay = 1000;
const MAX_RECONNECT_DELAY = 30_000;
let shutdownRequested = false;
let currentConfig: ClientSettings;

type MessageHandler = (msg: ServerToConsumerMessage) => void;
let onMessage: MessageHandler = () => {};

export function setMessageHandler(handler: MessageHandler): void {
  onMessage = handler;
}

function log(...args: unknown[]): void {
  console.error("[devotchi]", ...args);
}

function wsUrl(config: ClientSettings): string {
  const base = config.serverUrl.replace(/^http/, "ws").replace(/\/$/, "");
  return `${base}/ws?token=${encodeURIComponent(config.token)}`;
}

export function connect(config: ClientSettings): void {
  currentConfig = config;
  shutdownRequested = false;
  doConnect();
}

function doConnect(): void {
  if (ws) {
    ws.removeAllListeners();
    ws.close();
  }

  const url = wsUrl(currentConfig);
  log(`Connecting to ${currentConfig.serverUrl}`);
  ws = new WebSocket(url);

  ws.on("open", () => {
    connected = true;
    reconnectDelay = 1000;
    log("Connected, registering as producer");
    sendMessage(
      buildRegister({
        role: "producer",
        client: { kind: "mcp", version: "0.1.0", project: basename(process.cwd()) },
      })
    );
  });

  ws.on("message", (data: Buffer) => {
    try {
      const msg = JSON.parse(data.toString()) as ServerToConsumerMessage;
      handleMessage(msg);
    } catch (err) {
      log("Failed to parse message", err);
    }
  });

  ws.on("close", (code) => {
    connected = false;
    if (shutdownRequested) return;
    if (code === 4001) {
      log("Replaced by another producer, not reconnecting");
      return;
    }
    log(`Disconnected (code=${code}), reconnecting...`);
    scheduleReconnect();
  });

  ws.on("error", (err) => {
    log("Connection error:", err.message);
  });
}

function scheduleReconnect(): void {
  setTimeout(() => {
    reconnectDelay = Math.min(reconnectDelay * 2, MAX_RECONNECT_DELAY);
    doConnect();
  }, reconnectDelay);
}

function handleMessage(msg: ServerToConsumerMessage): void {
  switch (msg.type) {
    case "ping":
      sendMessage(buildPong());
      break;
    case "register_ack":
      log(`Registered as ${msg.role}, ${msg.peers.consumers} consumer(s)`);
      break;
    case "error":
      log(`Server error: ${msg.message} (${msg.code})`);
      break;
    default:
      onMessage(msg);
  }
}

export function sendMessage(msg: InboundMessage): boolean {
  if (!ws || ws.readyState !== WebSocket.OPEN) return false;
  ws.send(serialize(msg));
  return true;
}

export function disconnect(): void {
  shutdownRequested = true;
  if (ws) {
    ws.removeAllListeners();
    ws.close();
    ws = null;
  }
  connected = false;
}

export function isConnected(): boolean {
  return connected;
}
