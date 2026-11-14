// Rate limiting primitives shared by the HTTP routes (protocol 7.8) and the
// WebSocket router (protocol 3.5). All take the clock as an argument.

/** Sliding window log: at most `limit` events in any `windowMs` span. */
export class RateLog {
  private readonly times: number[] = [];

  constructor(
    private readonly limit: number,
    private readonly windowMs: number,
  ) {}

  private prune(now: number): void {
    while (this.times.length > 0 && this.times[0] <= now - this.windowMs) this.times.shift();
  }

  /** 0 when another event fits now, otherwise ms until one does. */
  retryAfter(now: number): number {
    this.prune(now);
    return this.times.length < this.limit ? 0 : this.times[0] + this.windowMs - now;
  }

  record(now: number): void {
    this.times.push(now);
  }

  /** Records the event if it fits; returns retryAfter. */
  hit(now: number): number {
    const wait = this.retryAfter(now);
    if (wait === 0) this.record(now);
    return wait;
  }

  isIdle(now: number): boolean {
    this.prune(now);
    return this.times.length === 0;
  }
}

/** One RateLog per key (client IP, token id, email). */
export class KeyedRateLimiter {
  private readonly logs = new Map<string, RateLog>();

  constructor(
    readonly limit: number,
    readonly windowMs: number,
  ) {}

  private log(key: string): RateLog {
    let log = this.logs.get(key);
    if (!log) this.logs.set(key, (log = new RateLog(this.limit, this.windowMs)));
    return log;
  }

  retryAfter(key: string, now: number): number {
    return this.logs.get(key)?.retryAfter(now) ?? 0;
  }

  record(key: string, now: number): void {
    this.log(key).record(now);
  }

  hit(key: string, now: number): number {
    return this.log(key).hit(now);
  }

  /** Forgets keys without events in the window, so memory stays bounded. */
  sweep(now: number): void {
    for (const [key, log] of this.logs) if (log.isIdle(now)) this.logs.delete(key);
  }
}

/** Token bucket: bursts up to `capacity`, refilled one token per `refillMs`. */
export class TokenBucket {
  private tokens: number;
  private updatedAt: number;

  constructor(
    private readonly capacity: number,
    private readonly refillMs: number,
    now: number,
  ) {
    this.tokens = capacity;
    this.updatedAt = now;
  }

  take(now: number): boolean {
    this.tokens = Math.min(this.capacity, this.tokens + (now - this.updatedAt) / this.refillMs);
    this.updatedAt = now;
    if (this.tokens < 1) return false;
    this.tokens -= 1;
    return true;
  }
}

/** At most one event per `intervalMs`. */
export class MinInterval {
  private last = -Infinity;

  constructor(private readonly intervalMs: number) {}

  take(now: number): boolean {
    if (now - this.last < this.intervalMs) return false;
    this.last = now;
    return true;
  }
}
