// Client-side pacing of producer content messages, so a chatty agent never
// runs into the server's 60 per minute socket budget (protocol 3.5).
// Status and mood are state, so a newer one replaces a queued one (latest
// wins); notifications are events and stay queued in order.

import { CONTENT_RATE_PER_MIN } from "@devotchi/shared";
import type { ContentMessage, SendResult } from "./connection.js";

export interface OutboxResult extends SendResult {
  /** The message was queued (and may be replaced by a newer one) instead of sent now. */
  throttled: boolean;
}

export interface OutboxOptions {
  /** At most 4 status messages per second. */
  statusIntervalMs?: number;
  /** Content messages per window; below the server's budget to leave room for the resend after a reconnect. */
  budget?: number;
  windowMs?: number;
  now?: () => number;
}

const MAX_QUEUED_NOTIFIES = 20;

export class Outbox {
  private readonly statusIntervalMs: number;
  private readonly budget: number;
  private readonly windowMs: number;
  private readonly now: () => number;
  private readonly sentAt: number[] = [];
  private lastStatusAt = -Infinity;
  private queue: ContentMessage[] = [];
  private timer: NodeJS.Timeout | null = null;

  constructor(
    private readonly deliver: (msg: ContentMessage) => SendResult,
    private readonly peers: () => { consumers: number; devices: number },
    options: OutboxOptions = {},
  ) {
    this.statusIntervalMs = options.statusIntervalMs ?? 250;
    this.budget = options.budget ?? CONTENT_RATE_PER_MIN - 10;
    this.windowMs = options.windowMs ?? 60_000;
    this.now = options.now ?? Date.now;
  }

  submit(msg: ContentMessage): OutboxResult {
    const now = this.now();
    if (this.queue.length === 0 && this.dueAt(msg, now) <= now) {
      return { ...this.send(msg, now), throttled: false };
    }
    this.enqueue(msg);
    this.schedule();
    const { consumers, devices } = this.peers();
    return { sent: false, consumers, devices, throttled: true };
  }

  close(): void {
    if (this.timer !== null) clearTimeout(this.timer);
    this.timer = null;
    this.queue = [];
  }

  private send(msg: ContentMessage, now: number): SendResult {
    const result = this.deliver(msg);
    if (result.sent) {
      this.sentAt.push(now);
      if (msg.type === "status") this.lastStatusAt = now;
    }
    return result;
  }

  /** Earliest time `msg` may go out. */
  private dueAt(msg: ContentMessage, now: number): number {
    while (this.sentAt.length > 0 && this.sentAt[0]! <= now - this.windowMs) this.sentAt.shift();
    let due = now;
    if (this.sentAt.length >= this.budget) due = this.sentAt[this.sentAt.length - this.budget]! + this.windowMs;
    if (msg.type === "status") due = Math.max(due, this.lastStatusAt + this.statusIntervalMs);
    return due;
  }

  private enqueue(msg: ContentMessage): void {
    if (msg.type === "status" || msg.type === "mood") {
      this.queue = this.queue.filter((queued) => queued.type !== msg.type);
    } else {
      const notifies = this.queue.filter((queued) => queued.type === "notify");
      if (notifies.length >= MAX_QUEUED_NOTIFIES) this.queue.splice(this.queue.indexOf(notifies[0]!), 1);
    }
    this.queue.push(msg);
  }

  private schedule(): void {
    if (this.timer !== null || this.queue.length === 0) return;
    const now = this.now();
    const delay = Math.max(0, this.dueAt(this.queue[0]!, now) - now);
    this.timer = setTimeout(() => {
      this.timer = null;
      this.flush();
    }, delay);
  }

  private flush(): void {
    const now = this.now();
    while (this.queue.length > 0 && this.dueAt(this.queue[0]!, now) <= now) {
      this.send(this.queue.shift()!, now);
    }
    this.schedule();
  }
}
