// The device registry seen from the API (protocol 7.5): rows from the
// database merged with the live state of their sockets (online, last frame,
// last holding, holding count, telemetry).

import { buildError, buildIdentify, type Device, type DeviceTelemetry, type TelemetryMessage } from "@devotchi/shared";
import type { Db } from "../db/index.js";
import {
  deleteDevice,
  findDevice,
  listDeviceRows,
  renameDevice,
  writeDeviceSeen,
  writeDeviceTelemetry,
  type DeviceRow,
} from "../db/devices.js";
import { errorFields, type Logger } from "../log.js";
import type { PermissionManager } from "../permissions/manager.js";
import { PetService } from "../pets/service.js";
import type { Conn } from "../rooms/conn.js";
import type { Room, RoomRegistry } from "../rooms/registry.js";
import type { WsServer } from "../ws/server.js";

export interface DeviceServiceDeps {
  db: Db;
  rooms: RoomRegistry;
  ws: WsServer;
  permissions: PermissionManager;
  log: Logger;
  /** Minimum spacing of `last_seen` and telemetry writes per socket. */
  writeIntervalMs: number;
}

export type IdentifyResult = "sent" | "offline" | "unknown";

const iso = (ms: number) => new Date(ms).toISOString();

/** The later of two ISO timestamps (both are toISOString() output, so they sort as strings). */
function later(a: string | null, b: string | null): string | null {
  if (a === null) return b;
  if (b === null) return a;
  return a > b ? a : b;
}

export class DeviceService {
  private timer: NodeJS.Timeout | null = null;

  constructor(private readonly deps: DeviceServiceDeps) {}

  /** Starts the periodic flush of `last_seen` and throttled telemetry. */
  start(): void {
    this.timer ??= setInterval(() => this.flush(Date.now()), Math.max(10, this.deps.writeIntervalMs));
    this.timer.unref();
  }

  /** Stops the timer and writes what is still only in memory. */
  stop(): void {
    if (this.timer) clearInterval(this.timer);
    this.timer = null;
    this.flush(Date.now(), true);
  }

  list(roomId: string): Device[] {
    const room = this.deps.rooms.get(roomId);
    return listDeviceRows(this.deps.db, roomId).map((row) => this.toDevice(row, room));
  }

  get(roomId: string, id: string): Device | undefined {
    const row = findDevice(this.deps.db, roomId, id);
    return row ? this.toDevice(row, this.deps.rooms.get(roomId)) : undefined;
  }

  rename(roomId: string, id: string, name: string): Device | undefined {
    return renameDevice(this.deps.db, roomId, id, name) ? this.get(roomId, id) : undefined;
  }

  /** DELETE /api/devices/:id: the row and its token go, its sockets close with 4003. */
  remove(roomId: string, id: string): boolean {
    const tokenId = deleteDevice(this.deps.db, roomId, id);
    if (tokenId === null) return false;
    this.deps.ws.closeToken(tokenId, undefined, "device unpaired");
    return true;
  }

  identify(roomId: string, id: string, durationMs: number): IdentifyResult {
    const row = findDevice(this.deps.db, roomId, id);
    if (!row) return "unknown";
    const conns = this.connsOf(this.deps.rooms.get(roomId), row.token_id);
    let sent = 0;
    for (const conn of conns) if (conn.send(buildIdentify(durationMs))) sent++;
    return sent > 0 ? "sent" : "offline";
  }

  /** Protocol 4.12: stored per identity, never forwarded. */
  onTelemetry(conn: Conn, msg: TelemetryMessage, now = Date.now()): void {
    const kind = conn.registration?.kind;
    if (kind !== "device" && kind !== "sim") {
      conn.send(buildError("NOT_ALLOWED", "only devices and sims send telemetry"));
      return;
    }
    const { type: _type, ...data } = msg;
    conn.lastTelemetry = { data, at: now };
    if (conn.tokenKind !== "device") return;
    if (now - (conn.telemetryWrittenAt ?? -Infinity) >= this.deps.writeIntervalMs) this.writeTelemetry(conn);
  }

  /** A device socket closed: everything still only in memory is written. */
  onClosed(conn: Conn): void {
    if (conn.tokenKind !== "device") return;
    this.flushConn(conn, true);
  }

  /** Writes `last_seen` and pending telemetry of every device socket; `force` ignores the throttle. */
  flush(now: number, force = false): void {
    for (const conn of this.deps.rooms.sockets) {
      if (conn.tokenKind !== "device" || !conn.registration) continue;
      this.flushConn(conn, force || now - (conn.telemetryWrittenAt ?? -Infinity) >= this.deps.writeIntervalMs);
    }
  }

  private flushConn(conn: Conn, withTelemetry: boolean): void {
    try {
      if (conn.lastSeenWrittenAt !== conn.lastFrameAt) {
        conn.lastSeenWrittenAt = conn.lastFrameAt;
        writeDeviceSeen(this.deps.db, conn.tokenId, conn.lastFrameAt);
      }
      if (withTelemetry && conn.lastTelemetry && conn.lastTelemetry.at !== conn.telemetryWrittenAt) {
        this.writeTelemetry(conn);
      }
    } catch (err) {
      // A closed database during shutdown must not turn a socket close into an uncaught error.
      this.deps.log.warn("device state write failed", errorFields(err));
    }
  }

  private writeTelemetry(conn: Conn): void {
    const sample = conn.lastTelemetry;
    if (!sample) return;
    conn.telemetryWrittenAt = sample.at;
    writeDeviceTelemetry(this.deps.db, conn.tokenId, sample.data, sample.at);
  }

  /** Registered sockets of one device token. */
  private connsOf(room: Room | undefined, tokenId: string): Conn[] {
    return room ? [...room.consumers].filter((conn) => conn.tokenId === tokenId) : [];
  }

  private toDevice(row: DeviceRow, room: Room | undefined): Device {
    const conns = this.connsOf(room, row.token_id);
    let lastSeen = row.last_seen;
    let lastHoldAt = row.last_hold_at;
    let telemetry: DeviceTelemetry | null = row.telemetry === null ? null : (JSON.parse(row.telemetry) as DeviceTelemetry);
    let telemetryAt = row.telemetry_at;
    let holding = 0;
    for (const conn of conns) {
      lastSeen = later(lastSeen, iso(conn.lastFrameAt));
      if (conn.lastHoldAt !== null) lastHoldAt = later(lastHoldAt, iso(conn.lastHoldAt));
      if (conn.lastTelemetry) {
        const at = iso(conn.lastTelemetry.at);
        if (telemetryAt === null || at > telemetryAt) {
          telemetry = conn.lastTelemetry.data;
          telemetryAt = at;
        }
      }
      if (room) holding += this.deps.permissions.holdingCount(conn, room);
    }
    const pets = PetService.stateOf(conns);
    return {
      id: row.id,
      name: row.name,
      model: row.model,
      fw: row.fw,
      createdAt: row.created_at,
      online: conns.length > 0,
      lastSeen,
      lastHoldAt,
      holding,
      telemetry,
      telemetryAt,
      ...(pets ? { pets } : {}),
    };
  }
}
