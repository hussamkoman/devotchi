// Devotchi broker — WebSocket message router + hook HTTP endpoints
import { createServer, type IncomingMessage, type ServerResponse } from "node:http";
import { WebSocketServer, WebSocket } from "ws";
import {
  type DevotchiMessage,
  type ClientRole,
  isValidPairingCode,
  HEARTBEAT_INTERVAL,
  HEARTBEAT_TIMEOUT,
  RATE_LIMIT_PER_MINUTE,
  MAX_MESSAGE_SIZE,
  DEFAULT_APPROVAL_TIMEOUT,
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
const pendingHookRequests = new Map<
  string,
  { resolve: (msg: DevotchiMessage) => void; timer: ReturnType<typeof setTimeout> }
>();

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

function sendToConsumers(code: string, msg: DevotchiMessage): void {
  const room = rooms.get(code);
  if (!room) return;
  for (const consumer of room.consumers) {
    send(consumer.ws, msg);
  }
}

function sendError(
  ws: WebSocket,
  message: string,
  code: DevotchiMessage extends { code: infer C } ? C : string
): void {
  send(ws, { type: "error", message, code: code as any });
}

// --- Helpers ---

function generateId(): string {
  return crypto.randomUUID().replace(/-/g, "").slice(0, 8);
}

function readBody(req: IncomingMessage): Promise<string> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    let size = 0;
    req.on("data", (chunk: Buffer) => {
      size += chunk.length;
      if (size > MAX_MESSAGE_SIZE) {
        req.destroy();
        reject(new Error("Body too large"));
        return;
      }
      chunks.push(chunk);
    });
    req.on("end", () => resolve(Buffer.concat(chunks).toString()));
    req.on("error", reject);
  });
}

function getQueryParam(url: string, param: string): string | null {
  try {
    return new URL(url, "http://localhost").searchParams.get(param);
  } catch {
    return null;
  }
}

function jsonResponse(res: ServerResponse, status: number, body: unknown): void {
  res.writeHead(status, {
    "Content-Type": "application/json",
    "Access-Control-Allow-Origin": "*",
  });
  res.end(JSON.stringify(body));
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
    // Check if this is a response to a hook-originated request
    if (
      (msg.type === "approval_response" || msg.type === "permission_response") &&
      "id" in msg
    ) {
      const pending = pendingHookRequests.get(msg.id);
      if (pending) {
        clearTimeout(pending.timer);
        pendingHookRequests.delete(msg.id);
        pending.resolve(msg);
        return; // Don't also forward to producer
      }
    }

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

// --- Hook HTTP Endpoints ---

async function handleHookPermission(
  req: IncomingMessage,
  res: ServerResponse,
  code: string
): Promise<void> {
  const room = rooms.get(code);

  // Graceful degradation: no consumers → allow
  if (!room || room.consumers.size === 0) {
    console.log(`[hook/permission] no consumers for ${code}, allowing`);
    jsonResponse(res, 200, {});
    return;
  }

  let body: any;
  try {
    body = JSON.parse(await readBody(req));
  } catch {
    jsonResponse(res, 400, { error: "Invalid JSON body" });
    return;
  }

  const id = generateId();
  const toolName = body.tool_name || "unknown";
  const toolInput = body.tool_input || {};
  const details = typeof toolInput === "object"
    ? JSON.stringify(toolInput).slice(0, 1000)
    : String(toolInput).slice(0, 1000);

  // Send permission request to all consumers
  const msg: DevotchiMessage = {
    type: "permission",
    id,
    action: toolName,
    details,
    timeout: DEFAULT_APPROVAL_TIMEOUT,
    timestamp: new Date().toISOString(),
  };

  sendToConsumers(code, msg);
  console.log(`[hook/permission] ${toolName} → ${code} (id=${id})`);

  // Wait for consumer response
  const response = await new Promise<DevotchiMessage | null>((resolve) => {
    const timer = setTimeout(() => {
      pendingHookRequests.delete(id);
      resolve(null);
    }, DEFAULT_APPROVAL_TIMEOUT);

    pendingHookRequests.set(id, { resolve, timer });
  });

  if (response && response.type === "permission_response") {
    const granted = response.granted;
    console.log(`[hook/permission] ${id} → ${granted ? "allow" : "deny"}`);
    jsonResponse(res, 200, {
      hookSpecificOutput: {
        hookEventName: "PermissionRequest",
        decision: {
          behavior: granted ? "allow" : "deny",
        },
      },
    });
  } else {
    console.log(`[hook/permission] ${id} → timeout`);
    jsonResponse(res, 200, {
      hookSpecificOutput: {
        hookEventName: "PermissionRequest",
        decision: { behavior: "deny" },
      },
    });
  }
}

function handleHookStatus(
  req: IncomingMessage,
  res: ServerResponse,
  code: string
): void {
  readBody(req)
    .then((raw) => {
      const body = JSON.parse(raw);
      const toolName = body.tool_name || "unknown";
      const toolInput = body.tool_input || {};

      // Build a short description of what happened
      let description = toolName;
      if (toolName === "Bash" && toolInput.command) {
        description = toolInput.command.slice(0, 200);
      } else if ((toolName === "Write" || toolName === "Edit") && toolInput.file_path) {
        description = `${toolName}: ${toolInput.file_path}`;
      }

      sendToConsumers(code, {
        type: "status",
        msg: description.slice(0, 200),
        mood: "working",
        timestamp: new Date().toISOString(),
      });
    })
    .catch((err) => {
      console.error("[hook/status] error:", err.message);
    })
    .finally(() => {
      jsonResponse(res, 200, {});
    });
}

function handleHookStop(
  req: IncomingMessage,
  res: ServerResponse,
  code: string
): void {
  sendToConsumers(code, {
    type: "notify",
    event: "task_complete",
    mood: "idle",
    detail: "Agent stopped",
    timestamp: new Date().toISOString(),
  });

  sendToConsumers(code, {
    type: "mood",
    mood: "sleeping",
    reason: "Waiting for instructions",
    timestamp: new Date().toISOString(),
  });

  // Drain request body before responding
  readBody(req)
    .catch((err) => {
      console.error("[hook/stop] error:", err.message);
    })
    .finally(() => {
      jsonResponse(res, 200, {});
    });
}

// --- Server Setup ---

const PORT = parseInt(process.env.DEVOTCHI_BROKER_PORT || "8080", 10);

const server = createServer(async (req, res) => {
  const url = req.url || "/";
  const pathname = url.split("?")[0];

  // CORS preflight
  if (req.method === "OPTIONS") {
    res.writeHead(204, {
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
      "Access-Control-Allow-Headers": "Content-Type",
    });
    res.end();
    return;
  }

  // Health check
  if (req.method === "GET" && pathname === "/") {
    jsonResponse(res, 200, {
      status: "ok",
      connections: clients.size,
    });
    return;
  }

  // Hook endpoints
  if (req.method === "POST" && pathname.startsWith("/hook/")) {
    const code = getQueryParam(url, "code");
    if (!code || !isValidPairingCode(code)) {
      jsonResponse(res, 400, { error: "Missing or invalid pairing code" });
      return;
    }

    switch (pathname) {
      case "/hook/permission":
        await handleHookPermission(req, res, code);
        return;
      case "/hook/status":
        handleHookStatus(req, res, code);
        return;
      case "/hook/stop":
        handleHookStop(req, res, code);
        return;
    }
  }

  res.writeHead(404);
  res.end();
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

// Graceful shutdown
function shutdown(): void {
  console.log("[shutdown] closing connections...");
  clearInterval(heartbeatInterval);

  // Reject all pending hook requests
  for (const [id, pending] of pendingHookRequests) {
    clearTimeout(pending.timer);
    pendingHookRequests.delete(id);
  }

  // Close all WebSocket connections
  for (const [ws] of clients) {
    ws.close(1001, "server shutting down");
  }

  wss.close(() => {
    server.close(() => {
      console.log("[shutdown] done");
      process.exit(0);
    });
  });

  // Force exit after 5 seconds
  setTimeout(() => process.exit(0), 5000).unref();
}

process.on("SIGTERM", shutdown);
process.on("SIGINT", shutdown);

server.listen(PORT, () => {
  console.log(`Devotchi broker listening on port ${PORT}`);
});
