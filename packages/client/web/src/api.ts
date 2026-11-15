// Calls to the dashboard's own /api proxy, authenticated with the per-launch
// session secret that the dashboard embedded in index.html.

export const SESSION_META = "devotchi-session";

export function sessionSecret(): string {
  return document.querySelector<HTMLMetaElement>(`meta[name="${SESSION_META}"]`)?.content ?? "";
}

export class DashboardApiError extends Error {
  constructor(
    message: string,
    readonly status: number,
    readonly code: string,
  ) {
    super(message);
  }
}

export async function api<T>(method: string, path: string, body?: unknown): Promise<T> {
  const headers: Record<string, string> = { Accept: "application/json", "X-Devotchi-Session": sessionSecret() };
  if (body !== undefined) headers["Content-Type"] = "application/json";
  let res: Response;
  try {
    res = await fetch(path, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) });
  } catch {
    throw new DashboardApiError("the dashboard is not reachable; is devotchi start still running?", 0, "unreachable");
  }
  if (res.status === 204) return undefined as T;
  const data = (await res.json().catch(() => null)) as { error?: unknown; code?: unknown } | null;
  if (!res.ok) {
    const message = typeof data?.error === "string" ? data.error : `HTTP ${res.status}`;
    throw new DashboardApiError(message, res.status, typeof data?.code === "string" ? data.code : `http_${res.status}`);
  }
  return data as T;
}

/** GET of a binary body (a pet pack). */
export async function apiBytes(path: string): Promise<Uint8Array> {
  return new Uint8Array(await (await raw("GET", path, undefined, "application/octet-stream")).arrayBuffer());
}

/** POST of a pet pack as application/octet-stream. */
export async function apiUpload<T>(path: string, bytes: Uint8Array): Promise<T> {
  return (await (await raw("POST", path, bytes, "application/json")).json()) as T;
}

async function raw(method: string, path: string, body: Uint8Array | undefined, accept: string): Promise<Response> {
  const headers: Record<string, string> = { Accept: accept, "X-Devotchi-Session": sessionSecret() };
  if (body !== undefined) headers["Content-Type"] = "application/octet-stream";
  let res: Response;
  try {
    res = await fetch(path, { method, headers, body: body === undefined ? undefined : new Uint8Array(body).buffer });
  } catch {
    throw new DashboardApiError("the dashboard is not reachable; is devotchi start still running?", 0, "unreachable");
  }
  if (!res.ok) {
    const data = (await res.json().catch(() => null)) as { error?: unknown; code?: unknown } | null;
    const message = typeof data?.error === "string" ? data.error : `HTTP ${res.status}`;
    throw new DashboardApiError(message, res.status, typeof data?.code === "string" ? data.code : `http_${res.status}`);
  }
  return res;
}
