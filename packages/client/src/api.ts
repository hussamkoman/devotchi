// Typed REST client for the server API (protocol section 7).

import {
  PROTO_VERSION,
  type CreatePairCodeResponse,
  type CreateTokenResponse,
  type Device,
  type HealthResponse,
  type ListDevicesResponse,
  type LoginResponse,
  type MeResponse,
  type NotifyRequest,
  type NotifyResponse,
  type PairConfirmResponse,
  type Room,
  type RoomSettings,
  type UpdateRoomSettingsRequest,
} from "@devotchi/shared";
import { CliError } from "./errors.js";

const DEFAULT_TIMEOUT_MS = 10_000;

/** The server answered with a non-2xx status. */
export class ApiError extends CliError {
  override name = "ApiError";

  constructor(
    message: string,
    readonly status: number,
    readonly code: string,
    readonly retryAfterS?: number,
    /** The server's own `error` text, without the request context. */
    readonly reason: string = message,
  ) {
    super(message, 1, status === 429 || status >= 500);
  }
}

/** No HTTP answer at all: connection refused, DNS failure, timeout. */
export class UnreachableError extends CliError {
  override name = "UnreachableError";

  constructor(message: string) {
    super(message, 1, true);
  }
}

function networkReason(err: unknown): string {
  if (err instanceof Error && err.name === "TimeoutError") return "timed out";
  const cause = (err as { cause?: { code?: unknown; message?: unknown } } | null)?.cause;
  if (typeof cause?.code === "string") return cause.code;
  if (typeof cause?.message === "string") return cause.message;
  return err instanceof Error ? err.message : String(err);
}

export interface ApiOptions {
  token?: string;
  timeoutMs?: number;
}

export class ApiClient {
  private readonly timeoutMs: number;

  constructor(
    readonly baseUrl: string,
    private readonly options: ApiOptions = {},
  ) {
    this.timeoutMs = options.timeoutMs ?? DEFAULT_TIMEOUT_MS;
  }

  withToken(token: string): ApiClient {
    return new ApiClient(this.baseUrl, { ...this.options, token });
  }

  async request<T>(method: string, path: string, body?: unknown, extraHeaders: Record<string, string> = {}): Promise<T> {
    const headers: Record<string, string> = { Accept: "application/json", ...extraHeaders };
    if (this.options.token !== undefined) headers.Authorization = `Bearer ${this.options.token}`;
    if (body !== undefined) headers["Content-Type"] = "application/json";
    let res: Response;
    try {
      res = await fetch(this.baseUrl + path, {
        method,
        headers,
        body: body === undefined ? undefined : JSON.stringify(body),
        signal: AbortSignal.timeout(this.timeoutMs),
      });
    } catch (err) {
      throw new UnreachableError(`cannot reach ${this.baseUrl} (${networkReason(err)})`);
    }
    const text = await res.text().catch(() => "");
    let data: unknown = undefined;
    if (text !== "") {
      try {
        data = JSON.parse(text);
      } catch {
        data = undefined;
      }
    }
    if (!res.ok) {
      const payload = (data ?? {}) as { error?: unknown; code?: unknown };
      const reason = typeof payload.error === "string" ? payload.error : res.statusText || "request failed";
      const code = typeof payload.code === "string" ? payload.code : "http_" + res.status;
      const retry = Number(res.headers.get("retry-after"));
      throw new ApiError(
        `${method} ${path} on ${this.baseUrl} failed: ${reason} (HTTP ${res.status})`,
        res.status,
        code,
        Number.isFinite(retry) && retry > 0 ? retry : undefined,
        reason,
      );
    }
    if (data === undefined && res.status !== 204) {
      throw new CliError(
        `${method} ${path} on ${this.baseUrl} returned a body that is not JSON; is this a Devotchi server?`,
        1,
        true,
      );
    }
    return data as T;
  }

  /** GET /health, refusing a server that does not speak protocol 2. */
  async health(): Promise<HealthResponse> {
    const health = await this.request<HealthResponse>("GET", "/health");
    if (typeof health !== "object" || health === null || health.status !== "ok") {
      throw new CliError(`${this.baseUrl}/health did not answer like a Devotchi server`, 1, true);
    }
    if (health.proto !== PROTO_VERSION) {
      throw new CliError(
        `${this.baseUrl} speaks protocol ${String(health.proto ?? 1)}, this client needs protocol ${PROTO_VERSION}; update the server`,
      );
    }
    return health;
  }

  me(): Promise<MeResponse> {
    return this.request("GET", "/api/me");
  }

  mintToken(label: string): Promise<CreateTokenResponse> {
    return this.request("POST", "/api/auth/token", { label });
  }

  room(): Promise<Room> {
    return this.request("GET", "/api/room");
  }

  updateRoomSettings(changes: UpdateRoomSettingsRequest): Promise<RoomSettings> {
    return this.request("PUT", "/api/room/settings", changes);
  }

  createPairCode(): Promise<CreatePairCodeResponse> {
    return this.request("POST", "/api/pair", {});
  }

  confirmPairing(code: string, name?: string): Promise<PairConfirmResponse> {
    return this.request("POST", "/api/pair/confirm", { code, ...(name !== undefined ? { name } : {}) });
  }

  async devices(): Promise<Device[]> {
    return (await this.request<ListDevicesResponse>("GET", "/api/devices")).devices;
  }

  renameDevice(id: string, name: string): Promise<Device> {
    return this.request("PATCH", `/api/devices/${encodeURIComponent(id)}`, { name });
  }

  identifyDevice(id: string, durationMs?: number): Promise<{ sent: true }> {
    return this.request("POST", `/api/devices/${encodeURIComponent(id)}/identify`, durationMs === undefined ? {} : { durationMs });
  }

  async removeDevice(id: string): Promise<void> {
    await this.request("DELETE", `/api/devices/${encodeURIComponent(id)}`);
  }

  notify(body: NotifyRequest): Promise<NotifyResponse> {
    return this.request("POST", "/api/notify", body);
  }

  login(email: string, password: string): Promise<LoginResponse> {
    return this.request("POST", "/api/auth/login", { email, password });
  }

  async logout(): Promise<void> {
    await this.request("POST", "/api/auth/logout");
  }

  async deleteToken(id: string): Promise<void> {
    await this.request("DELETE", `/api/tokens/${encodeURIComponent(id)}`);
  }
}
