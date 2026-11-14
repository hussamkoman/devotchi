// Drives `holding` (protocol 4.7) for the dashboard: the delivery ack and the
// 4 s heartbeat come from the shared HoldingAckScheduler, and a request only
// counts as presented while the tab is visible. A hidden tab names nothing,
// so the server falls back to a device or to the terminal instead of waiting
// for a person who cannot see the request.

import { HoldingAckScheduler } from "@devotchi/shared";

export interface HoldingDriverOptions {
  send(ids: string[]): void;
  visible(): boolean;
  now?: () => number;
  setTimer?: (fn: () => void, ms: number) => unknown;
  clearTimer?: (handle: unknown) => void;
}

export class HoldingDriver {
  private readonly scheduler = new HoldingAckScheduler();
  private readonly known = new Set<string>();
  private timer: unknown = null;
  private readonly now: () => number;
  private readonly setTimer: (fn: () => void, ms: number) => unknown;
  private readonly clearTimer: (handle: unknown) => void;

  constructor(private readonly opts: HoldingDriverOptions) {
    this.now = opts.now ?? Date.now;
    this.setTimer = opts.setTimer ?? ((fn, ms) => setTimeout(fn, ms));
    this.clearTimer = opts.clearTimer ?? ((h) => clearTimeout(h as ReturnType<typeof setTimeout>));
  }

  /** Brings the held set in line with the requests currently in the list. */
  sync(ids: readonly string[]): void {
    const wanted = new Set(ids);
    for (const id of [...this.known]) {
      if (!wanted.has(id)) {
        this.known.delete(id);
        this.scheduler.onRemove(id);
      }
    }
    const visible = this.opts.visible();
    for (const id of ids) {
      if (this.known.has(id)) continue;
      this.known.add(id);
      if (visible) this.scheduler.onPermission(id, this.now());
    }
    this.arm();
  }

  /** Call on visibilitychange. Becoming visible presents (and so acks) every request again. */
  visibilityChanged(): void {
    if (this.opts.visible()) {
      const now = this.now();
      for (const id of this.known) this.scheduler.onPermission(id, now);
    } else {
      this.scheduler.clear();
    }
    this.arm();
  }

  /** The socket closed; the server forgets this consumer's holdings with it. */
  reset(): void {
    this.known.clear();
    this.scheduler.clear();
    this.arm();
  }

  private arm(): void {
    if (this.timer !== null) this.clearTimer(this.timer);
    this.timer = null;
    const due = this.scheduler.nextDueAt();
    if (due === null || !this.opts.visible()) return;
    this.timer = this.setTimer(() => {
      this.timer = null;
      const ids = this.opts.visible() ? this.scheduler.onTick(this.now()) : null;
      if (ids !== null && ids.length > 0) this.opts.send(ids);
      this.arm();
    }, Math.max(0, due - this.now()));
  }
}
