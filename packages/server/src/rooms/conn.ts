// One WebSocket connection, from upgrade to close.

import { WebSocket } from "ws";
import {
  CONTENT_RATE_PER_MIN,
  HOLDING_BUCKET,
  PETS_RATE_PER_MIN,
  serialize,
  type Cap,
  type ClientKind,
  type DevicePets,
  type PetsResult,
  type DeviceTelemetry,
  type Role,
  type ServerTiming,
  type ServerToConsumerMessage,
  type TokenKind,
} from "@devotchi/shared";
import { MinInterval, RateLog, TokenBucket } from "../limits.js";

export interface Registration {
  role: Role;
  kind: ClientKind;
  /**
   * The name the server uses for a consumer (protocol 3.2 "Device identity"):
   * registry id, sim id or "dashboard". Undefined for producers.
   */
  identity?: string;
  model?: string;
  version?: string;
  /** Producers: default `project` for their status and notify messages. */
  project?: string;
}

export class Conn {
  lastFrameAt: number;
  /** Time of the last `holding` frame (protocol 7.5 `lastHoldAt`). */
  lastHoldAt: number | null = null;
  /** The `lastHoldAt` value last written to the device registry. */
  lastHoldWrittenAt: number | null = null;
  /** The `lastFrameAt` value last written to the registry's `last_seen`. */
  lastSeenWrittenAt: number | null = null;
  /** Latest telemetry sample (protocol 4.12); for cli-token sims the only copy. */
  lastTelemetry: { data: DeviceTelemetry; at: number } | null = null;
  /** The `lastTelemetry.at` value last written to the registry. */
  telemetryWrittenAt: number | null = null;
  /** Protocol 14.8: the last `pets` message without `result`; memory only. */
  lastPets: DevicePets | null = null;
  /** Protocol 14.8: the last `result` of a `pets` message and when it arrived (ms); memory only. */
  lastPetsResult: { result: PetsResult; at: number } | null = null;
  registration: Registration | null = null;
  /** Protocol 14.1: what the consumer declared and the server offers; empty for phase-1 consumers and producers. */
  caps: ReadonlySet<Cap> = new Set();
  readonly content: RateLog;
  readonly holding: TokenBucket;
  readonly telemetry: MinInterval;
  readonly setMode: MinInterval;
  readonly pets: RateLog;

  constructor(
    /** Per-instance counter that tells connections apart in logs. */
    readonly serial: number,
    readonly ws: WebSocket,
    readonly tokenId: string,
    readonly tokenKind: TokenKind,
    readonly roomId: string,
    timing: ServerTiming,
    openedAt: number,
  ) {
    this.lastFrameAt = openedAt;
    this.content = new RateLog(CONTENT_RATE_PER_MIN, timing.rateWindowMs);
    this.holding = new TokenBucket(HOLDING_BUCKET, timing.holdingRefillMs, openedAt);
    this.telemetry = new MinInterval(timing.systemMessageIntervalMs);
    this.setMode = new MinInterval(timing.systemMessageIntervalMs);
    this.pets = new RateLog(PETS_RATE_PER_MIN, timing.rateWindowMs);
  }

  get role(): Role | undefined {
    return this.registration?.role;
  }

  get isOpen(): boolean {
    return this.ws.readyState === WebSocket.OPEN;
  }

  /** Sends a pre-serialised frame; false when the socket is not open. */
  sendFrame(frame: string): boolean {
    if (!this.isOpen) return false;
    this.ws.send(frame);
    return true;
  }

  send(msg: ServerToConsumerMessage): boolean {
    return this.sendFrame(serialize(msg));
  }

  close(code: number, reason: string): void {
    if (this.ws.readyState === WebSocket.OPEN || this.ws.readyState === WebSocket.CONNECTING) {
      this.ws.close(code, reason);
    }
  }
}
