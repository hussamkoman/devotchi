import { type IncomingMessage } from "node:http";
import type { Server } from "node:http";
import { WebSocketServer, WebSocket } from "ws";
import {
  CONTENT_RATE_PER_MIN,
  HARD_FRAME_BYTES,
  HEARTBEAT_INTERVAL_MS,
  buildMood,
  buildNotify,
  buildPing,
  buildPong,
  buildRegisterAck,
  buildStatus,
  parseInbound,
  type Role,
} from "@devotchi/shared";
import type { Database } from "../db/index.js";
import { lookupTokenHash } from "../middleware/auth.js";
import { RoomState, type ClientInfo } from "./rooms.js";

const SERVER_VERSION = "0.1.0";

function isRateLimited(client: ClientInfo): boolean {
  const now = Date.now();
  client.messageTimestamps = client.messageTimestamps.filter(
    (t) => now - t < 60_000
  );
  if (client.messageTimestamps.length >= CONTENT_RATE_PER_MIN) {
    return true;
  }
  client.messageTimestamps.push(now);
  return false;
}

function getQueryParam(url: string, param: string): string | null {
  try {
    return new URL(url, "http://localhost").searchParams.get(param);
  } catch {
    return null;
  }
}

function handleRegister(state: RoomState, ws: WebSocket, data: string, roomId: string): void {
  let msg: unknown;
  try {
    msg = JSON.parse(data);
  } catch {
    state.sendError(ws, "Invalid JSON", "INVALID_MESSAGE");
    return;
  }
  const fields = typeof msg === "object" && msg !== null ? (msg as Record<string, unknown>) : {};

  if (fields.type !== "register") {
    state.sendError(ws, "First message must be register", "INVALID_MESSAGE");
    ws.close();
    return;
  }

  const role: Role = fields.role === "producer" ? "producer" : "consumer";
  const room = state.getOrCreateRoom(roomId);
  const client: ClientInfo = {
    ws,
    role,
    roomId,
    alive: true,
    messageTimestamps: [],
  };

  if (role === "producer") {
    if (room.producer) {
      state.sendError(room.producer.ws, "Replaced by new producer", "INVALID_MESSAGE");
      room.producer.ws.close(4001, "replaced");
      state.clients.delete(room.producer.ws);
    }
    room.producer = client;
  } else {
    room.consumers.add(client);
  }

  state.clients.set(ws, client);

  state.send(ws, buildRegisterAck({
    role,
    roomId,
    serverVersion: SERVER_VERSION,
    serverTime: Date.now(),
    mode: "ask",
    peers: { producers: room.producer ? 1 : 0, consumers: room.consumers.size, devices: 0 },
  }));

  console.log(
    `[register] ${role} joined room ${roomId} (${state.peerCount(room)} in room)`
  );
}

function handleMessage(state: RoomState, ws: WebSocket, data: string, wsRoomId: string): void {
  const client = state.clients.get(ws);

  if (!client) {
    handleRegister(state, ws, data, wsRoomId);
    return;
  }

  const result = parseInbound(data, { role: client.role });
  if (!result.ok) {
    state.sendError(ws, result.message, result.code, result.id);
    return;
  }
  const msg = result.msg;

  if (msg.type === "pong") {
    client.alive = true;
    return;
  }

  if (msg.type === "ping") {
    state.send(ws, buildPong());
    return;
  }

  const room = state.rooms.get(client.roomId);
  if (!room) return;

  if (msg.type === "permission_response") {
    const pending = state.pendingHookRequests.get(msg.id);
    if (pending) {
      clearTimeout(pending.timer);
      state.pendingHookRequests.delete(msg.id);
      pending.resolve(msg);
    } else {
      state.sendError(ws, "not pending", "UNKNOWN_REQUEST", msg.id);
    }
    return;
  }

  if (msg.type !== "status" && msg.type !== "notify" && msg.type !== "mood") return;

  if (isRateLimited(client)) {
    state.sendError(ws, "Rate limited", "RATE_LIMITED");
    return;
  }

  const timestamp = new Date().toISOString();
  const out =
    msg.type === "status"
      ? buildStatus({ ...msg, timestamp })
      : msg.type === "notify"
        ? buildNotify({ ...msg, timestamp })
        : buildMood({ ...msg, timestamp });
  for (const consumer of room.consumers) {
    state.send(consumer.ws, out);
  }
}

function handleDisconnect(state: RoomState, ws: WebSocket): void {
  const client = state.clients.get(ws);
  if (!client) return;

  const room = state.rooms.get(client.roomId);
  if (room) {
    if (client.role === "producer" && room.producer?.ws === ws) {
      room.producer = null;
    } else {
      room.consumers.delete(client);
    }
    state.cleanupRoom(client.roomId);
  }

  state.clients.delete(ws);
  console.log(`[disconnect] ${client.role} left room ${client.roomId}`);
}

export function attachWebSocket(server: Server, state: RoomState, db: Database): {
  wss: WebSocketServer;
  heartbeatInterval: ReturnType<typeof setInterval>;
} {
  const wss = new WebSocketServer({ noServer: true, maxPayload: HARD_FRAME_BYTES });

  server.on("upgrade", (req: IncomingMessage, socket, head) => {
    const token = getQueryParam(req.url || "", "token");

    if (!token) {
      socket.write("HTTP/1.1 401 Unauthorized\r\n\r\n");
      socket.destroy();
      return;
    }

    const tokenInfo = lookupTokenHash(db, token);
    if (!tokenInfo) {
      socket.write("HTTP/1.1 401 Unauthorized\r\n\r\n");
      socket.destroy();
      return;
    }

    wss.handleUpgrade(req, socket, head, (ws) => {
      (ws as any).__roomId = tokenInfo.roomId;
      wss.emit("connection", ws, req);
    });
  });

  wss.on("connection", (ws: WebSocket, req: IncomingMessage) => {
    const roomId = (ws as any).__roomId as string;
    console.log(`[connect] new connection from ${req.socket.remoteAddress} (room: ${roomId})`);

    ws.on("message", (data: Buffer) => {
      handleMessage(state, ws, data.toString(), roomId);
    });

    ws.on("close", () => handleDisconnect(state, ws));
    ws.on("error", (err) => {
      console.error(`[error] ${err.message}`);
      handleDisconnect(state, ws);
    });
  });

  const heartbeatInterval = setInterval(() => {
    for (const [ws, client] of state.clients) {
      if (!client.alive) {
        console.log(
          `[heartbeat] ${client.role} room ${client.roomId} timed out`
        );
        ws.terminate();
        handleDisconnect(state, ws);
        continue;
      }
      client.alive = false;
      state.send(ws, buildPing());
    }
  }, HEARTBEAT_INTERVAL_MS);

  wss.on("close", () => clearInterval(heartbeatInterval));

  return { wss, heartbeatInterval };
}
