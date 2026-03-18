// Devotchi broker — WebSocket message router
import { createServer, type IncomingMessage } from "node:http";
import { WebSocketServer, WebSocket } from "ws";
import {
  type DevotchiMessage,
  type ClientRole,
  isValidPairingCode,
  HEARTBEAT_INTERVAL,
  HEARTBEAT_TIMEOUT,
  RATE_LIMIT_PER_MINUTE,
  MAX_MESSAGE_SIZE,
} from "@devotchi/shared";

// --- Types ---

interface ClientInfo {
  ws: WebSocket;
  role: ClientRole;
  pairingCode: string;
  alive: boolean;
  messageTimestamps: number[];
}

interface PairingRoom {
  producer: ClientInfo | null;
  consumers: Set<ClientInfo>;
}

// --- State ---

const rooms = new Map<string, PairingRoom>();
const clients = new Map<WebSocket, ClientInfo>();

function getOrCreateRoom(code: string): PairingRoom {
  let room = rooms.get(code);
  if (!room) {
    room = { producer: null, consumers: new Set() };
    rooms.set(code, room);
  }
  return room;
}

function cleanupRoom(code: string): void {
  const room = rooms.get(code);
  if (room && !room.producer && room.consumers.size === 0) {
    rooms.delete(code);
  }
}

function peerCount(room: PairingRoom): number {
  return (room.producer ? 1 : 0) + room.consumers.size;
}

// --- Rate Limiting ---

function isRateLimited(client: ClientInfo): boolean {
  const now = Date.now();
  client.messageTimestamps = client.messageTimestamps.filter(
    (t) => now - t < 60_000
  );
  if (client.messageTimestamps.length >= RATE_LIMIT_PER_MINUTE) {
    return true;
  }
  client.messageTimestamps.push(now);
  return false;
}

// --- Message Sending ---

function send(ws: WebSocket, msg: DevotchiMessage): void {
  if (ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(msg));
  }
}

function sendError(
  ws: WebSocket,
  message: string,
  code: DevotchiMessage extends { code: infer C } ? C : string
): void {
  send(ws, { type: "error", message, code: code as any });
}

// --- Connection Handling ---

function handleRegister(ws: WebSocket, msg: DevotchiMessage): void {
  if (msg.type !== "register") {
    sendError(ws, "First message must be register", "INVALID_MESSAGE");
    ws.close();
    return;
  }

  if (!isValidPairingCode(msg.pairingCode)) {
    sendError(ws, "Invalid pairing code format", "INVALID_PAIRING_CODE");
    ws.close();
    return;
  }

  const room = getOrCreateRoom(msg.pairingCode);
  const client: ClientInfo = {
    ws,
    role: msg.role,
    pairingCode: msg.pairingCode,
    alive: true,
    messageTimestamps: [],
  };

  if (msg.role === "producer") {
    if (room.producer) {
      // Send error and close cleanly with a recognizable code
      send(room.producer.ws, {
        type: "error",
        message: "Replaced by new producer",
        code: "INVALID_MESSAGE",
      });
      room.producer.ws.close(4001, "replaced");
      clients.delete(room.producer.ws);
    }
    room.producer = client;
  } else {
    room.consumers.add(client);
  }

  clients.set(ws, client);

  send(ws, {
    type: "register_ack",
    role: msg.role,
    pairingCode: msg.pairingCode,
    connectedPeers: peerCount(room) - 1,
  });

  console.log(
    `[register] ${msg.role} joined ${msg.pairingCode} (${peerCount(room)} in room)`
  );
}

function handleMessage(ws: WebSocket, data: string): void {
  const client = clients.get(ws);

  let msg: DevotchiMessage;
  try {
    msg = JSON.parse(data);
  } catch {
    sendError(ws, "Invalid JSON", "INVALID_MESSAGE");
    return;
  }

  if (!msg || typeof msg !== "object" || !("type" in msg)) {
    sendError(ws, "Missing type field", "INVALID_MESSAGE");
    return;
  }

  // Not yet registered — must register first
  if (!client) {
    handleRegister(ws, msg);
    return;
  }

  // Handle system messages
  if (msg.type === "pong") {
    client.alive = true;
    return;
  }

  if (msg.type === "ping") {
    send(ws, { type: "pong" });
    return;
  }

  // Rate limit
  if (isRateLimited(client)) {
    sendError(ws, "Rate limited", "RATE_LIMITED");
    return;
  }

  // Route messages
  const room = rooms.get(client.pairingCode);
  if (!room) return;

  if (client.role === "producer") {
    // Forward to all consumers
    for (const consumer of room.consumers) {
      send(consumer.ws, msg);
    }
  } else {
    // Forward to producer
    if (room.producer) {
      send(room.producer.ws, msg);
    } else {
      sendError(ws, "No producer connected", "NO_PRODUCER");
    }
  }
}

function handleDisconnect(ws: WebSocket): void {
  const client = clients.get(ws);
  if (!client) return;

  const room = rooms.get(client.pairingCode);
  if (room) {
    if (client.role === "producer" && room.producer?.ws === ws) {
      room.producer = null;
    } else {
      room.consumers.delete(client);
    }
    cleanupRoom(client.pairingCode);
  }

  clients.delete(ws);
  console.log(
    `[disconnect] ${client.role} left ${client.pairingCode}`
  );
}

// --- Server Setup ---

const PORT = parseInt(process.env.DEVOTCHI_BROKER_PORT || "8080", 10);

const server = createServer((req, res) => {
  if (req.method === "GET" && req.url === "/") {
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(
      JSON.stringify({
        status: "ok",
        connections: clients.size,
      })
    );
  } else {
    res.writeHead(404);
    res.end();
  }
});

const wss = new WebSocketServer({ server, maxPayload: MAX_MESSAGE_SIZE });

wss.on("connection", (ws: WebSocket, req: IncomingMessage) => {
  console.log(`[connect] new connection from ${req.socket.remoteAddress}`);

  ws.on("message", (data: Buffer) => {
    const str = data.toString();
    if (str.length > MAX_MESSAGE_SIZE) {
      sendError(ws, "Message too large", "INVALID_MESSAGE");
      return;
    }
    handleMessage(ws, str);
  });

  ws.on("close", () => handleDisconnect(ws));
  ws.on("error", (err) => {
    console.error(`[error] ${err.message}`);
    handleDisconnect(ws);
  });
});

// Heartbeat
const heartbeatInterval = setInterval(() => {
  for (const [ws, client] of clients) {
    if (!client.alive) {
      console.log(
        `[heartbeat] ${client.role} ${client.pairingCode} timed out`
      );
      ws.terminate();
      handleDisconnect(ws);
      continue;
    }
    client.alive = false;
    send(ws, { type: "ping" });
  }
}, HEARTBEAT_INTERVAL);

wss.on("close", () => clearInterval(heartbeatInterval));

server.listen(PORT, () => {
  console.log(`Devotchi broker listening on port ${PORT}`);
});
