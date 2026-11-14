import { randomBytes } from "node:crypto";
import { createServer, type IncomingMessage, type ServerResponse } from "node:http";
import { WebSocketServer, WebSocket } from "ws";
import {
  CONTENT_RATE_PER_MIN,
  DEFAULT_HOLD_MS,
  HARD_FRAME_BYTES,
  HEARTBEAT_INTERVAL_MS,
  buildDismiss,
  buildError,
  buildMood,
  buildNotify,
  buildPermission,
  buildPing,
  buildPong,
  buildRegisterAck,
  buildStatus,
  formatToolCall,
  parseInbound,
  serialize,
  type ErrorCode,
  type PermissionResponseMessage,
  type Role,
  type ServerToConsumerMessage,
} from "@devotchi/shared";

interface ClientInfo {
  ws: WebSocket;
  role: Role;
  roomId: string;
  alive: boolean;
  messageTimestamps: number[];
}

interface PairingRoom {
  producer: ClientInfo | null;
  consumers: Set<ClientInfo>;
}

export function startLocalBroker(port: number): Promise<ReturnType<typeof createServer>> {
  const rooms = new Map<string, PairingRoom>();
  const clients = new Map<WebSocket, ClientInfo>();
  const pendingHookRequests = new Map<
    string,
    { resolve: (msg: PermissionResponseMessage | null) => void; timer: ReturnType<typeof setTimeout> }
  >();

  function getOrCreateRoom(roomId: string): PairingRoom {
    let room = rooms.get(roomId);
    if (!room) {
      room = { producer: null, consumers: new Set() };
      rooms.set(roomId, room);
    }
    return room;
  }

  function cleanupRoom(roomId: string): void {
    const room = rooms.get(roomId);
    if (room && !room.producer && room.consumers.size === 0) {
      rooms.delete(roomId);
    }
  }

  function isRateLimited(client: ClientInfo): boolean {
    const now = Date.now();
    client.messageTimestamps = client.messageTimestamps.filter((t) => now - t < 60_000);
    if (client.messageTimestamps.length >= CONTENT_RATE_PER_MIN) return true;
    client.messageTimestamps.push(now);
    return false;
  }

  function send(ws: WebSocket, msg: ServerToConsumerMessage): void {
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(serialize(msg));
    }
  }

  function sendToConsumers(roomId: string, msg: ServerToConsumerMessage): void {
    const room = rooms.get(roomId);
    if (!room) return;
    for (const consumer of room.consumers) {
      send(consumer.ws, msg);
    }
  }

  function sendError(ws: WebSocket, message: string, code: ErrorCode, id?: string): void {
    send(ws, buildError(code, message, id));
  }

  function generateId(): string {
    return randomBytes(16).toString("hex");
  }

  function readBody(req: IncomingMessage): Promise<string> {
    return new Promise((resolve, reject) => {
      const chunks: Buffer[] = [];
      let size = 0;
      req.on("data", (chunk: Buffer) => {
        size += chunk.length;
        if (size > 64 * 1024) {
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

  function jsonResponse(res: ServerResponse, status: number, body: unknown): void {
    res.writeHead(status, {
      "Content-Type": "application/json",
      "Access-Control-Allow-Origin": "*",
    });
    res.end(JSON.stringify(body));
  }

  // Local mode: no token validation
  function handleRegister(ws: WebSocket, data: string): void {
    let msg: unknown;
    try {
      msg = JSON.parse(data);
    } catch {
      sendError(ws, "Invalid JSON", "INVALID_MESSAGE");
      return;
    }
    const fields = typeof msg === "object" && msg !== null ? (msg as Record<string, unknown>) : {};

    if (fields.type !== "register") {
      sendError(ws, "First message must be register", "INVALID_MESSAGE");
      ws.close();
      return;
    }

    // Protocol v2 ignores register.roomId; every local client shares one room.
    const roomId = "local";
    const role: Role = fields.role === "producer" ? "producer" : "consumer";
    const room = getOrCreateRoom(roomId);
    const client: ClientInfo = {
      ws,
      role,
      roomId,
      alive: true,
      messageTimestamps: [],
    };

    if (role === "producer") {
      if (room.producer) {
        sendError(room.producer.ws, "Replaced by new producer", "INVALID_MESSAGE");
        room.producer.ws.close(4001, "replaced");
        clients.delete(room.producer.ws);
      }
      room.producer = client;
    } else {
      room.consumers.add(client);
    }

    clients.set(ws, client);
    send(ws, buildRegisterAck({
      role,
      roomId,
      serverVersion: "0.1.0",
      serverTime: Date.now(),
      mode: "ask",
      peers: { producers: room.producer ? 1 : 0, consumers: room.consumers.size, devices: 0 },
    }));
  }

  function handleMessage(ws: WebSocket, data: string): void {
    const client = clients.get(ws);

    if (!client) {
      handleRegister(ws, data);
      return;
    }

    const result = parseInbound(data, { role: client.role });
    if (!result.ok) {
      sendError(ws, result.message, result.code, result.id);
      return;
    }
    const msg = result.msg;

    if (msg.type === "pong") { client.alive = true; return; }
    if (msg.type === "ping") { send(ws, buildPong()); return; }

    const room = rooms.get(client.roomId);
    if (!room) return;

    if (msg.type === "permission_response") {
      const pending = pendingHookRequests.get(msg.id);
      if (pending) {
        clearTimeout(pending.timer);
        pendingHookRequests.delete(msg.id);
        pending.resolve(msg);
      } else {
        sendError(ws, "not pending", "UNKNOWN_REQUEST", msg.id);
      }
      return;
    }

    if (msg.type !== "status" && msg.type !== "notify" && msg.type !== "mood") return;

    if (isRateLimited(client)) {
      sendError(ws, "Rate limited", "RATE_LIMITED");
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
      send(consumer.ws, out);
    }
  }

  function handleDisconnect(ws: WebSocket): void {
    const client = clients.get(ws);
    if (!client) return;

    const room = rooms.get(client.roomId);
    if (room) {
      if (client.role === "producer" && room.producer?.ws === ws) {
        room.producer = null;
      } else {
        room.consumers.delete(client);
      }
      cleanupRoom(client.roomId);
    }

    clients.delete(ws);
  }

  // Local broker hook endpoints use a simple room lookup
  // In local mode, find the first room (there's usually only one)
  function findRoomForHook(): string | null {
    for (const [roomId] of rooms) {
      return roomId;
    }
    return null;
  }

  async function handleHookPermission(req: IncomingMessage, res: ServerResponse): Promise<void> {
    const roomId = findRoomForHook();
    if (!roomId) {
      jsonResponse(res, 200, {});
      return;
    }

    const room = rooms.get(roomId);
    if (!room || room.consumers.size === 0) {
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
    const formatted = formatToolCall(body.tool_name, body.tool_input, typeof body.cwd === "string" ? body.cwd : undefined);

    sendToConsumers(
      roomId,
      buildPermission({ id, ...formatted, timeout: DEFAULT_HOLD_MS, timestamp: new Date().toISOString() })
    );

    const response = await new Promise<PermissionResponseMessage | null>((resolve) => {
      const timer = setTimeout(() => {
        pendingHookRequests.delete(id);
        sendToConsumers(roomId, buildDismiss({ id, reason: "timeout" }));
        resolve(null);
      }, DEFAULT_HOLD_MS);

      pendingHookRequests.set(id, { resolve, timer });

      res.on("close", () => {
        if (res.writableEnded) return;
        const pending = pendingHookRequests.get(id);
        if (pending) {
          clearTimeout(pending.timer);
          pendingHookRequests.delete(id);
          sendToConsumers(roomId, buildDismiss({ id, reason: "cancelled" }));
          resolve(null);
        }
      });
    });

    if (response) {
      jsonResponse(res, 200, {
        hookSpecificOutput: {
          hookEventName: "PermissionRequest",
          decision: { behavior: response.granted ? "allow" : "deny" },
        },
      });
    } else {
      jsonResponse(res, 200, {});
    }
  }

  function handleHookStatus(req: IncomingMessage, res: ServerResponse): void {
    const roomId = findRoomForHook();
    readBody(req)
      .then((raw) => {
        if (!roomId) return;
        const body = JSON.parse(raw);
        const toolName = body.tool_name || "unknown";
        const toolInput = body.tool_input || {};

        let description = toolName;
        if (toolName === "Bash" && toolInput.command) {
          description = toolInput.command.slice(0, 200);
        } else if ((toolName === "Write" || toolName === "Edit") && toolInput.file_path) {
          description = `${toolName}: ${toolInput.file_path}`;
        }

        sendToConsumers(roomId, {
          type: "status",
          msg: description.slice(0, 200),
          mood: "working",
          timestamp: new Date().toISOString(),
        });
      })
      .catch(() => {})
      .finally(() => {
        jsonResponse(res, 200, {});
      });
  }

  function handleHookStop(req: IncomingMessage, res: ServerResponse): void {
    const roomId = findRoomForHook();
    if (roomId) {
      sendToConsumers(roomId, {
        type: "notify",
        event: "task_complete",
        mood: "idle",
        detail: "Agent stopped",
        timestamp: new Date().toISOString(),
      });
      sendToConsumers(roomId, {
        type: "mood",
        mood: "sleeping",
        reason: "Waiting for instructions",
        timestamp: new Date().toISOString(),
      });
    }

    readBody(req).catch(() => {}).finally(() => {
      jsonResponse(res, 200, {});
    });
  }

  const server = createServer(async (req, res) => {
    const url = req.url || "/";
    const pathname = url.split("?")[0];

    if (req.method === "OPTIONS") {
      res.writeHead(204, {
        "Access-Control-Allow-Origin": "*",
        "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
        "Access-Control-Allow-Headers": "Content-Type, Authorization",
      });
      res.end();
      return;
    }

    if (req.method === "GET" && pathname === "/") {
      jsonResponse(res, 200, { status: "ok", connections: clients.size });
      return;
    }

    // Local broker: accept token-based hook URLs (ignore token validation)
    if (req.method === "POST" && pathname.startsWith("/hook/")) {
      switch (pathname) {
        case "/hook/permission":
          await handleHookPermission(req, res);
          return;
        case "/hook/status":
          handleHookStatus(req, res);
          return;
        case "/hook/stop":
          handleHookStop(req, res);
          return;
      }
    }

    // Local broker: simple token endpoint (returns a dummy token)
    if (req.method === "POST" && pathname === "/api/auth/token") {
      jsonResponse(res, 201, { token: "local-token", id: "local", roomId: "local" });
      return;
    }

    if (req.method === "POST" && pathname === "/api/pair") {
      jsonResponse(res, 201, { pairingCode: "LOCAL", expiresAt: new Date(Date.now() + 300000).toISOString() });
      return;
    }

    res.writeHead(404);
    res.end();
  });

  const wss = new WebSocketServer({ server, maxPayload: HARD_FRAME_BYTES });

  wss.on("connection", (ws: WebSocket) => {
    ws.on("message", (data: Buffer) => {
      handleMessage(ws, data.toString());
    });
    ws.on("close", () => handleDisconnect(ws));
    ws.on("error", () => handleDisconnect(ws));
  });

  const heartbeatInterval = setInterval(() => {
    for (const [ws, client] of clients) {
      if (!client.alive) {
        ws.terminate();
        handleDisconnect(ws);
        continue;
      }
      client.alive = false;
      send(ws, buildPing());
    }
  }, HEARTBEAT_INTERVAL_MS);

  wss.on("close", () => clearInterval(heartbeatInterval));

  return new Promise((resolve) => {
    server.listen(port, () => {
      console.log(`Devotchi local broker listening on port ${port}`);
      resolve(server);
    });
  });
}
