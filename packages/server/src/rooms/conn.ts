// One WebSocket connection, from upgrade to close.

import { WebSocket } from "ws";
import {
  CONTENT_RATE_PER_MIN,
  HOLDING_BUCKET,
  serialize,
  type ClientKind,
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
  /** Registry row of a device token, when one exists. */
  deviceName?: string;
  /** `client.deviceId` exactly as sent. */
  claimedDeviceId?: string;
  model?: string;
  version?: string;
  /** Producers: default `project` for their status and notify messages. */
  project?: string;
}

export class Conn {
  lastFrameAt: number;
  registration: Registration | null = null;
  readonly content: RateLog;
  readonly holding: TokenBucket;
  readonly telemetry: MinInterval;
  readonly setMode: MinInterval;

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
