// In-memory room state of one server instance: who is connected, the last
// status and the room mood (protocol 4.3-4.5), and per-session state.

import {
  buildPeers,
  serialize,
  type LastRequest,
  type Mood,
  type Peers,
  type ServerToConsumerMessage,
  type StatusMessage,
} from "@devotchi/shared";
import type { PendingRequest } from "../permissions/manager.js";
import type { Conn } from "./conn.js";

/** An idle room keeps its last request and diagnostics this long, for `devotchi status` and `doctor`. */
const HISTORY_MAX_AGE_MS = 24 * 60 * 60_000;

export interface RoomMood {
  value: Mood;
  reason?: string;
  /** Epoch ms when it was set. */
  at: number;
}

/** Per Claude Code session state (protocol 8.2), keyed by sid ("" when the hook sent no session_id). */
export interface RoomSession {
  sid: string;
  project?: string;
  lastSeen: number;
  /** Time of the last UserPromptSubmit, for the Stop duration. */
  lastPromptAt: number | null;
  /** Consecutive PostToolUseFailure events. */
  failures: number;
  /** A terminal permission dialog of this session may be on screen (protocol 8.4). */
  dialogMaybeOpen: boolean;
}

export interface RoomDiagnosticsState {
  dialogWhilePending: { count: number; lastSeenAt: number | null };
}

export class Room {
  readonly producers = new Set<Conn>();
  readonly consumers = new Set<Conn>();
  lastStatus: { msg: StatusMessage; at: number } | null = null;
  /** The mood of the last message sent to consumers that carried one. */
  mood: RoomMood | null = null;
  readonly sessions = new Map<string, RoomSession>();
  /** Pending permission requests, oldest first; owned by the PermissionManager. */
  readonly pending: PendingRequest[] = [];
  lastRequest: LastRequest | null = null;
  /** In memory only: starts at zero after a restart. */
  readonly diagnostics: RoomDiagnosticsState = { dialogWhilePending: { count: 0, lastSeenAt: null } };

  constructor(readonly id: string) {}

  get isEmpty(): boolean {
    return this.producers.size === 0 && this.consumers.size === 0;
  }

  peers(): Peers {
    let devices = 0;
    for (const conn of this.consumers) {
      const kind = conn.registration?.kind;
      if (kind === "device" || kind === "sim") devices++;
    }
    return { producers: this.producers.size, consumers: this.consumers.size, devices };
  }

  /** Every registered connection. */
  *members(): Iterable<Conn> {
    yield* this.producers;
    yield* this.consumers;
  }
}

export class RoomRegistry {
  private readonly rooms = new Map<string, Room>();
  /** Every open WebSocket of the instance, registered or not. */
  readonly sockets = new Set<Conn>();

  get(roomId: string): Room | undefined {
    return this.rooms.get(roomId);
  }

  /** Every room the instance currently keeps. */
  all(): IterableIterator<Room> {
    return this.rooms.values();
  }

  getOrCreate(roomId: string): Room {
    let room = this.rooms.get(roomId);
    if (!room) this.rooms.set(roomId, (room = new Room(roomId)));
    return room;
  }

  /** Adds a registered connection to its room and tells the others. */
  join(conn: Conn): Room {
    const room = this.getOrCreate(conn.roomId);
    (conn.role === "producer" ? room.producers : room.consumers).add(conn);
    this.sendPeers(room, conn);
    return room;
  }

  /** Forgets a closed connection; tells the room if it had registered. */
  leave(conn: Conn): void {
    this.sockets.delete(conn);
    const room = this.rooms.get(conn.roomId);
    if (!room) return;
    const removed = room.producers.delete(conn) || room.consumers.delete(conn);
    if (removed) this.sendPeers(room);
  }

  /** Open connections, registered or not, that authenticated with the token. */
  connsOfToken(tokenId: string): Conn[] {
    return [...this.sockets].filter((conn) => conn.tokenId === tokenId);
  }

  private sendPeers(room: Room, except?: Conn): void {
    const frame = serialize(buildPeers(room.peers()));
    for (const conn of room.members()) if (conn !== except) conn.sendFrame(frame);
  }

  /**
   * The one way content reaches consumers. Records the last status and the
   * room mood (protocol 4.5) and returns how many consumers it was written to.
   */
  broadcastToConsumers(room: Room, msg: ServerToConsumerMessage, now = Date.now()): number {
    if (msg.type === "status") room.lastStatus = { msg, at: now };
    if ((msg.type === "status" || msg.type === "notify" || msg.type === "mood") && msg.mood !== undefined) {
      room.mood = { value: msg.mood, at: now };
      if (msg.type === "mood" && msg.reason !== undefined) room.mood.reason = msg.reason;
    }
    const frame = serialize(msg);
    let delivered = 0;
    for (const conn of room.consumers) if (conn.sendFrame(frame)) delivered++;
    return delivered;
  }

  sendToProducers(room: Room, msg: ServerToConsumerMessage): number {
    const frame = serialize(msg);
    let delivered = 0;
    for (const conn of room.producers) if (conn.sendFrame(frame)) delivered++;
    return delivered;
  }

  /**
   * Forgets silent sessions, then drops rooms nobody is connected to that
   * have no pending request and whose state has aged out.
   */
  prune(now: number, maxAgeMs: number, sessionTtlMs: number): void {
    for (const [id, room] of this.rooms) {
      for (const [sid, session] of room.sessions) {
        if (now - session.lastSeen >= sessionTtlMs) room.sessions.delete(sid);
      }
      if (!room.isEmpty || room.sessions.size > 0 || room.pending.length > 0) continue;
      const replayable = Math.max(room.lastStatus?.at ?? -Infinity, room.mood?.at ?? -Infinity);
      const history = Math.max(
        room.lastRequest ? Date.parse(room.lastRequest.at) : -Infinity,
        room.diagnostics.dialogWhilePending.lastSeenAt ?? -Infinity,
      );
      if (now - replayable >= maxAgeMs && now - history >= HISTORY_MAX_AGE_MS) this.rooms.delete(id);
    }
  }
}
