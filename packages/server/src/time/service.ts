// Protocol 14.9: `time` to every consumer that declared a cap, at least
// hourly, when the room's UTC offset changes (daylight saving) and when the
// room's time zone setting changes.

import { buildTime, serialize, utcOffsetMinutes } from "@devotchi/shared";
import type { Db } from "../db/index.js";
import { getRoomTimeZone } from "../db/room-settings.js";
import type { Room, RoomRegistry } from "../rooms/registry.js";

export interface TimeServiceDeps {
  db: Db;
  rooms: RoomRegistry;
  checkMs: number;
  intervalMs: number;
}

interface RoomClock {
  offset: number;
  sentAt: number;
}

function capableConsumers(room: Room) {
  return [...room.consumers].filter((conn) => conn.caps.size > 0);
}

export class TimeService {
  private timer: NodeJS.Timeout | null = null;
  private readonly clocks = new WeakMap<Room, RoomClock>();

  constructor(private readonly deps: TimeServiceDeps) {}

  start(): void {
    this.timer = setInterval(() => this.tick(Date.now()), this.deps.checkMs);
    this.timer.unref();
  }

  stop(): void {
    if (this.timer) clearInterval(this.timer);
    this.timer = null;
  }

  /** The offset of the room's zone at `now`. */
  offsetOf(roomId: string, now: number): number {
    return utcOffsetMinutes(getRoomTimeZone(this.deps.db, roomId), now);
  }

  /**
   * One check of every room. A room's clock starts when it first has a
   * capable consumer: those consumers just got the offset in register_ack.
   */
  tick(now: number): void {
    for (const room of this.deps.rooms.all()) {
      if (capableConsumers(room).length === 0) {
        this.clocks.delete(room);
        continue;
      }
      const offset = this.offsetOf(room.id, now);
      const clock = this.clocks.get(room);
      if (!clock) this.clocks.set(room, { offset, sentAt: now });
      else if (offset !== clock.offset || now - clock.sentAt >= this.deps.intervalMs) this.send(room, now, offset);
    }
  }

  /** The room's time zone setting changed. */
  onZoneChanged(roomId: string, now = Date.now()): void {
    const room = this.deps.rooms.get(roomId);
    if (room) this.send(room, now, this.offsetOf(roomId, now));
  }

  private send(room: Room, now: number, offset: number): void {
    this.clocks.set(room, { offset, sentAt: now });
    const frame = serialize(buildTime(now, offset));
    for (const conn of capableConsumers(room)) conn.sendFrame(frame);
  }
}
