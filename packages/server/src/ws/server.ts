// WebSocket transport (protocol section 3): upgrade authentication,
// register, per-role routing, socket rate limits and heartbeat.

import type { IncomingMessage, Server } from "node:http";
import type { Duplex } from "node:stream";
import { WebSocketServer, type RawData, type WebSocket } from "ws";
import {
  CLOSE_CODES,
  HARD_FRAME_BYTES,
  ROOM_MAX_CONSUMERS,
  ROOM_MAX_PRODUCERS,
  buildError,
  buildMood,
  buildNotify,
  buildPing,
  buildPong,
  buildRegisterAck,
  buildStatus,
  parseInbound,
  utcOffsetMinutes,
  type Cap,
  type ErrorCode,
  type InboundMessage,
  type MoodMessage,
  type NotifyMessage,
  type RegisterMessage,
  type ServerFeatures,
  type ServerToConsumerMessage,
  type StatusMessage,
  type TokenKind,
  serialize,
} from "@devotchi/shared";
import type { ResolvedConfig } from "../config.js";
import type { Db } from "../db/index.js";
import { bindDeviceOnRegister } from "../db/devices.js";
import { getRoomMode, getRoomTimeZone } from "../db/room-settings.js";
import { extractCredential, type AuthState, type Authenticator } from "../http/auth.js";
import { errorFields, type Logger } from "../log.js";
import { Conn, type Registration } from "../rooms/conn.js";
import type { Room, RoomRegistry } from "../rooms/registry.js";
import { SERVER_VERSION } from "../version.js";

/** Consumer-to-server messages that other modules handle (protocol 3.7). */
export type SystemMessageType =
  | "holding"
  | "permission_response"
  | "permission_defer"
  | "decide"
  | "question_response"
  | "extend"
  | "set_mode"
  | "telemetry"
  | "pets";
export type SystemMessage<T extends SystemMessageType> = Extract<InboundMessage, { type: T }>;
export type SystemHandler<T extends SystemMessageType> = (conn: Conn, msg: SystemMessage<T>, room: Room) => void;
export type ConnHook = (conn: Conn, room: Room) => void;

/** Protocol 14.1: the features this server offers; a switched-off feature is missing. */
export function serverCaps(features: ServerFeatures): Cap[] {
  const caps: Cap[] = ["decide"];
  if (features.questions) caps.push("ask");
  if (features.plans) caps.push("plan");
  caps.push("pets");
  return caps;
}

/** Connections stuck in the close handshake are terminated after this. */
const CLOSE_GRACE_MS = 300;

export interface WsDeps {
  config: ResolvedConfig;
  db: Db;
  rooms: RoomRegistry;
  auth: Authenticator;
  log: Logger;
}

function rejectUpgrade(socket: Duplex, status: number, reason: string, error: string, code: string): void {
  const body = JSON.stringify({ error, code });
  socket.end(
    `HTTP/1.1 ${status} ${reason}\r\n` +
      "Content-Type: application/json\r\n" +
      `Content-Length: ${Buffer.byteLength(body)}\r\n` +
      "Cache-Control: no-store\r\n" +
      "Connection: close\r\n\r\n" +
      body,
  );
}

function toBuffer(data: RawData): Buffer {
  if (Buffer.isBuffer(data)) return data;
  if (Array.isArray(data)) return Buffer.concat(data);
  return Buffer.from(data);
}

export class WsServer {
  private readonly wss: WebSocketServer;
  private readonly handlers = new Map<SystemMessageType, (conn: Conn, msg: InboundMessage, room: Room) => void>();
  private readonly registeredHooks: ConnHook[] = [];
  private readonly closedHooks: ConnHook[] = [];
  private readonly timers: NodeJS.Timeout[] = [];
  private serial = 0;
  private closing = false;

  constructor(private readonly deps: WsDeps) {
    this.wss = new WebSocketServer({ noServer: true, clientTracking: false, maxPayload: HARD_FRAME_BYTES });
  }

  /** Registers the handler of one consumer system message type. */
  handle<T extends SystemMessageType>(type: T, handler: SystemHandler<T>): void {
    this.handlers.set(type, handler as (conn: Conn, msg: InboundMessage, room: Room) => void);
  }

  /** Runs after register_ack and the status/mood replay to a consumer. */
  onConsumerRegistered(hook: ConnHook): void {
    this.registeredHooks.push(hook);
  }

  /** Runs after a registered connection closed and left its room. */
  onConnectionClosed(hook: ConnHook): void {
    this.closedHooks.push(hook);
  }

  attach(server: Server): void {
    server.on("upgrade", (req: IncomingMessage, socket: Duplex, head: Buffer) => this.onUpgrade(req, socket, head));
    const { timing } = this.deps.config;
    this.timers.push(setInterval(() => this.pingAll(), timing.heartbeatIntervalMs));
    const sweepMs = Math.max(5, Math.min(1000, Math.floor(timing.heartbeatDeadMs / 10)));
    this.timers.push(setInterval(() => this.reapSilent(Date.now()), sweepMs));
  }

  /** Closes every open socket of the token, e.g. with 4003 on revocation. */
  closeToken(tokenId: string, code: number = CLOSE_CODES.REVOKED, reason = "token revoked"): number {
    const conns = this.deps.rooms.connsOfToken(tokenId);
    for (const conn of conns) conn.close(code, reason);
    return conns.length;
  }

  /** Closes every socket with 1001 and waits briefly for the handshakes. */
  async shutdown(): Promise<void> {
    this.closing = true;
    for (const timer of this.timers) clearInterval(timer);
    const conns = [...this.deps.rooms.sockets];
    await Promise.all(
      conns.map(
        (conn) =>
          new Promise<void>((resolve) => {
            if (conn.ws.readyState === conn.ws.CLOSED) return resolve();
            const timer = setTimeout(() => {
              conn.ws.terminate();
              resolve();
            }, CLOSE_GRACE_MS);
            conn.ws.once("close", () => {
              clearTimeout(timer);
              resolve();
            });
            conn.close(CLOSE_CODES.GOING_AWAY, "server shutting down");
          }),
      ),
    );
    this.wss.close();
  }

  private onUpgrade(req: IncomingMessage, socket: Duplex, head: Buffer): void {
    socket.on("error", (err) => this.deps.log.debug("upgrade socket error", errorFields(err)));
    const { rooms, auth, config } = this.deps;
    if (this.closing) return rejectUpgrade(socket, 503, "Service Unavailable", "Server is shutting down", "unavailable");

    let tokens: string[];
    try {
      tokens = new URL(req.url ?? "/", "http://localhost").searchParams.getAll("token");
    } catch {
      return rejectUpgrade(socket, 400, "Bad Request", "Malformed request URL", "invalid_request");
    }
    if (tokens.length > 1) {
      return rejectUpgrade(socket, 400, "Bad Request", 'Query parameter "token" is given more than once', "invalid_request");
    }
    const credential = extractCredential(req.headers.authorization, tokens[0], true);
    let state: AuthState | null;
    try {
      state = credential.ok ? auth.authenticate(credential.value) : null;
    } catch (err) {
      // Thrown from an "upgrade" listener this would be an uncaught exception and end the process.
      this.deps.log.error("upgrade authentication failed", errorFields(err));
      return rejectUpgrade(socket, 500, "Internal Server Error", "Internal server error", "internal");
    }
    // Sessions are for REST; sockets authenticate with API tokens only.
    if (!state || state.kind === "session") {
      return rejectUpgrade(socket, 401, "Unauthorized", "Invalid token", "unauthorized");
    }
    const tokenKind: TokenKind = state.kind;
    // handleUpgrade completes synchronously, so the set size cannot lag behind.
    if (rooms.sockets.size >= config.maxSockets) {
      this.deps.log.warn("socket limit reached", { limit: config.maxSockets });
      return rejectUpgrade(socket, 503, "Service Unavailable", "Too many connections", "unavailable");
    }

    this.wss.handleUpgrade(req, socket, head, (ws) => {
      const conn = new Conn(++this.serial, ws, state.tokenId, tokenKind, state.roomId, config.timing, Date.now());
      this.onConnection(ws, conn);
    });
  }

  private onConnection(ws: WebSocket, conn: Conn): void {
    const { rooms, config, log } = this.deps;
    rooms.sockets.add(conn);
    log.debug("socket opened", { conn: conn.serial, kind: conn.tokenKind });

    const deadline = setTimeout(() => {
      if (conn.registration) return;
      this.sendError(conn, "INVALID_MESSAGE", "register was not received in time");
      conn.close(CLOSE_CODES.POLICY_VIOLATION, "register timeout");
    }, config.timing.registerDeadlineMs);

    ws.on("message", (data: RawData, isBinary: boolean) => {
      try {
        this.onFrame(conn, data, isBinary);
      } catch (err) {
        log.error("frame handler failed", { conn: conn.serial, ...errorFields(err) });
      }
    });
    ws.on("error", (err) => log.debug("socket error", { conn: conn.serial, ...errorFields(err) }));
    ws.on("close", (code: number) => {
      clearTimeout(deadline);
      this.onClose(conn, code);
    });
  }

  private sendError(conn: Conn, code: ErrorCode, message: string, id?: string): void {
    conn.send(buildError(code, message, id));
  }

  private onFrame(conn: Conn, data: RawData, isBinary: boolean): void {
    const now = Date.now();
    conn.lastFrameAt = now;
    // Frames that arrive after the server started closing the socket.
    if (!conn.isOpen) return;
    if (isBinary) {
      this.sendError(conn, "INVALID_MESSAGE", "binary frames are not supported");
      if (!conn.registration) conn.close(CLOSE_CODES.POLICY_VIOLATION, "first message must be register");
      return;
    }
    const raw = toBuffer(data);
    const registration = conn.registration;
    if (!registration) return this.onRegister(conn, raw, now);

    const result = parseInbound(raw, { role: registration.role, tokenKind: conn.tokenKind });
    if (!result.ok) return this.sendError(conn, result.code, result.message, result.id);
    const msg = result.msg;
    const room = this.deps.rooms.getOrCreate(conn.roomId);

    switch (msg.type) {
      case "ping":
        conn.send(buildPong());
        return;
      case "pong":
        return;
      case "register":
        // parseInbound rejects a second register; kept for exhaustiveness.
        return;
      case "status":
      case "notify":
      case "mood":
        return this.onContent(conn, registration, room, msg, now);
      default:
        return this.onSystem(conn, room, msg, now);
    }
  }

  private onRegister(conn: Conn, raw: Buffer, now: number): void {
    const { rooms, db, log } = this.deps;
    const result = parseInbound(raw, { role: "unregistered", tokenKind: conn.tokenKind });
    if (!result.ok) {
      this.sendError(conn, result.code, result.message, result.id);
      conn.close(result.close ?? CLOSE_CODES.POLICY_VIOLATION, "register rejected");
      return;
    }
    const msg = result.msg as RegisterMessage;
    const room = rooms.getOrCreate(conn.roomId);
    const full =
      msg.role === "producer" ? room.producers.size >= ROOM_MAX_PRODUCERS : room.consumers.size >= ROOM_MAX_CONSUMERS;
    if (full) {
      this.sendError(conn, "ROOM_FULL", `this room already has the maximum number of ${msg.role}s`);
      conn.close(CLOSE_CODES.POLICY_VIOLATION, "room full");
      return;
    }

    const { client } = msg;
    const registration: Registration = { role: msg.role, kind: client.kind };
    if (client.model !== undefined) registration.model = client.model;
    if (client.version !== undefined) registration.version = client.version;
    if (client.project !== undefined) registration.project = client.project;
    let device: { id: string; name: string } | undefined;
    if (conn.tokenKind === "device") {
      const row = bindDeviceOnRegister(db, conn.tokenId, {
        claimedId: client.deviceId,
        model: client.model,
        version: client.version,
      });
      // A device token always has a registry row; without one the device is unpaired.
      if (!row) {
        this.sendError(conn, "NOT_ALLOWED", "this device is not paired");
        conn.close(CLOSE_CODES.REVOKED, "device unpaired");
        return;
      }
      device = { id: row.id, name: row.name };
      registration.identity = device.id;
    } else if (client.kind === "sim") {
      registration.identity = client.deviceId;
    } else if (client.kind === "dashboard") {
      registration.identity = "dashboard";
    }
    conn.registration = registration;
    const caps = serverCaps(this.deps.config.features);
    // `pets` is for devices and sims only (protocol 14.1).
    const usable = (cap: Cap) => caps.includes(cap) && (cap !== "pets" || client.kind === "device" || client.kind === "sim");
    if (msg.role === "consumer") conn.caps = new Set((client.caps ?? []).filter(usable));
    rooms.join(conn);

    conn.send(
      buildRegisterAck({
        role: msg.role,
        roomId: conn.roomId,
        serverVersion: SERVER_VERSION,
        serverTime: now,
        mode: getRoomMode(db, conn.roomId),
        peers: room.peers(),
        device,
        caps,
        utcOffsetMin: utcOffsetMinutes(getRoomTimeZone(db, conn.roomId), now),
      }),
    );
    log.info("registered", { conn: conn.serial, role: msg.role, kind: client.kind });
    if (msg.role === "consumer") this.replay(conn, room, now);
  }

  /** Protocol 3.3: last status without its mood, then the room mood. */
  private replay(conn: Conn, room: Room, now: number): void {
    const maxAge = this.deps.config.timing.replayMaxAgeMs;
    if (room.lastStatus && now - room.lastStatus.at < maxAge) {
      const { mood: _mood, ...withoutMood } = room.lastStatus.msg;
      conn.send(withoutMood);
    }
    if (room.mood && now - room.mood.at < maxAge) {
      conn.send(
        buildMood({ mood: room.mood.value, reason: room.mood.reason, timestamp: new Date(room.mood.at).toISOString() }),
      );
    }
    for (const hook of this.registeredHooks) hook(conn, room);
  }

  private onContent(
    conn: Conn,
    registration: Registration,
    room: Room,
    msg: StatusMessage | NotifyMessage | MoodMessage,
    now: number,
  ): void {
    if (conn.content.hit(now) > 0) {
      return this.sendError(conn, "RATE_LIMITED", "more than 60 status, notify and mood messages per minute");
    }
    const timestamp = new Date(now).toISOString();
    const project = registration.project;
    let out: ServerToConsumerMessage;
    if (msg.type === "status") out = buildStatus({ ...msg, project: msg.project ?? project, timestamp });
    else if (msg.type === "notify") out = buildNotify({ ...msg, project: msg.project ?? project, timestamp });
    else out = buildMood({ ...msg, timestamp });
    this.deps.rooms.broadcastToConsumers(room, out, now);
  }

  private onSystem(conn: Conn, room: Room, msg: SystemMessage<SystemMessageType>, now: number): void {
    const handler = this.handlers.get(msg.type);
    if (!handler) return this.sendError(conn, "NOT_ALLOWED", `${msg.type} is not supported by this server`);
    const allowed =
      msg.type === "holding"
        ? conn.holding.take(now)
        : msg.type === "telemetry"
          ? conn.telemetry.take(now)
          : msg.type === "set_mode"
            ? conn.setMode.take(now)
            : true;
    if (!allowed) return this.sendError(conn, "RATE_LIMITED", `too many ${msg.type} messages`);
    handler(conn, msg, room);
  }

  private onClose(conn: Conn, code: number): void {
    const { rooms, log } = this.deps;
    const room = rooms.get(conn.roomId);
    rooms.leave(conn);
    if (!conn.registration) return;
    log.info("disconnected", { conn: conn.serial, role: conn.registration.role, code });
    if (room) for (const hook of this.closedHooks) hook(conn, room);
  }

  private pingAll(): void {
    const frame = serialize(buildPing());
    for (const conn of this.deps.rooms.sockets) if (conn.registration) conn.sendFrame(frame);
  }

  private reapSilent(now: number): void {
    const deadMs = this.deps.config.timing.heartbeatDeadMs;
    for (const conn of this.deps.rooms.sockets) {
      if (conn.registration && now - conn.lastFrameAt > deadMs) {
        this.deps.log.info("socket silent, terminating", { conn: conn.serial });
        conn.ws.terminate();
      }
    }
  }
}
