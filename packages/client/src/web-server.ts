// The local dashboard: static web UI plus a proxy to the server that holds
// the CLI token, so the token never reaches the browser (architecture 3.3).
//
// Threat model: any web page the user visits and any host on the LAN. The
// listener is loopback-only, every request must name the dashboard in Host
// (DNS rebinding), state-changing requests and the WebSocket must come from
// the dashboard's own Origin, and /api and /ws additionally need the
// per-launch secret that only the served index.html carries.

import { randomBytes, timingSafeEqual } from "node:crypto";
import { readFile, stat } from "node:fs/promises";
import { createServer, type IncomingMessage, type Server, type ServerResponse } from "node:http";
import type { AddressInfo, Socket } from "node:net";
import { extname, join, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { WebSocket, WebSocketServer, type RawData } from "ws";
import {
  API_BODY_LIMIT_BYTES,
  HARD_FRAME_BYTES,
  buildRegister,
  serialize,
  type ServerMode,
  type UpdateRoomSettingsRequest,
} from "@devotchi/shared";
import { ApiClient, ApiError, UnreachableError } from "./api.js";
import { serverWsUrl } from "./config.js";
import { CliError } from "./errors.js";
import { CLIENT_VERSION } from "./version.js";

const DEFAULT_WEB_DIR = fileURLToPath(new URL("./web", import.meta.url));

export const SESSION_HEADER = "x-devotchi-session";
export const SESSION_META = "devotchi-session";
/** Close code the proxy uses when the server refused the CLI token (protocol 3.6). */
const CLOSE_TOKEN_REJECTED = 4003;
const CLOSE_UPSTREAM_FAILED = 1011;
/** Frames a browser may send before the upstream socket is registered. */
const MAX_QUEUED_FRAMES = 32;

const MIME_TYPES: Record<string, string> = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json",
  ".map": "application/json",
  ".png": "image/png",
  ".svg": "image/svg+xml",
  ".webp": "image/webp",
  ".ico": "image/x-icon",
  ".woff2": "font/woff2",
  ".txt": "text/plain; charset=utf-8",
};

/** Vite names build outputs `name-<hash>.ext`; those never change content. */
const HASHED_ASSET_RE = /-[A-Za-z0-9_-]{8,}\.[a-z0-9]+$/;

export interface DashboardOptions {
  serverUrl: string;
  token: string;
  /** 0 picks a free port. */
  port: number;
  local: boolean;
  /** Serve /dev-session so the Vite dev server can proxy with the secret. */
  dev?: boolean;
  webDir?: string;
}

export interface Dashboard {
  port: number;
  url: string;
  secret: string;
  address(): AddressInfo;
  close(): Promise<void>;
}

class HttpError extends Error {
  constructor(
    readonly status: number,
    readonly code: string,
    message: string,
  ) {
    super(message);
  }
}

function sameSecret(given: string | null | undefined, secret: Buffer): boolean {
  if (typeof given !== "string") return false;
  const buf = Buffer.from(given);
  return buf.length === secret.length && timingSafeEqual(buf, secret);
}

function securityHeaders(port: number): Record<string, string> {
  const self = [`ws://localhost:${port}`, `ws://127.0.0.1:${port}`].join(" ");
  return {
    "X-Content-Type-Options": "nosniff",
    "Referrer-Policy": "no-referrer",
    "X-Frame-Options": "DENY",
    "Content-Security-Policy":
      `default-src 'self'; connect-src 'self' ${self}; img-src 'self' data:; ` +
      "object-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'",
  };
}

function sendJson(res: ServerResponse, status: number, body: unknown, headers: Record<string, string>): void {
  if (status === 204) {
    res.writeHead(204, headers);
    res.end();
    return;
  }
  res.writeHead(status, { ...headers, "Content-Type": "application/json", "Cache-Control": "no-store" });
  res.end(JSON.stringify(body));
}

function sendError(res: ServerResponse, status: number, code: string, message: string, headers: Record<string, string>): void {
  sendJson(res, status, { error: message, code }, headers);
}

async function readJsonBody(req: IncomingMessage): Promise<Record<string, unknown>> {
  const type = req.headers["content-type"] ?? "";
  if (!/^application\/json\b/i.test(type)) throw new HttpError(400, "invalid_request", "Content-Type must be application/json");
  const chunks: Buffer[] = [];
  let size = 0;
  for await (const chunk of req as AsyncIterable<Buffer>) {
    size += chunk.length;
    if (size > API_BODY_LIMIT_BYTES) throw new HttpError(413, "payload_too_large", "body too large");
    chunks.push(chunk);
  }
  let body: unknown;
  try {
    body = JSON.parse(Buffer.concat(chunks).toString("utf8"));
  } catch {
    throw new HttpError(400, "invalid_request", "body is not valid JSON");
  }
  if (typeof body !== "object" || body === null || Array.isArray(body)) {
    throw new HttpError(400, "invalid_request", "body must be a JSON object");
  }
  return body as Record<string, unknown>;
}

function optionalString(body: Record<string, unknown>, key: string): string | undefined {
  const value = body[key];
  if (value === undefined || typeof value === "string") return value;
  throw new HttpError(400, "invalid_request", `${key} must be a string`);
}

/** Valid codes for a close frame we send (RFC 6455 7.4); 1005/1006 only describe what happened. */
function forwardableCloseCode(code: number): number {
  return (code >= 1000 && code <= 1003) || (code >= 1007 && code <= 1014) || (code >= 3000 && code <= 4999)
    ? code
    : CLOSE_UPSTREAM_FAILED;
}

function listen(server: Server, port: number): Promise<number> {
  return new Promise((resolvePort, reject) => {
    server.once("error", (err: NodeJS.ErrnoException) => {
      if (err.code === "EADDRINUSE") reject(new CliError(`dashboard port ${port} is already in use; choose another with --port <n>`));
      else if (err.code === "EACCES") reject(new CliError(`no permission to listen on dashboard port ${port}; choose another with --port <n>`));
      else reject(new CliError(`cannot start the dashboard on port ${port}: ${err.message}`));
    });
    // Loopback only: the dashboard acts with the CLI token, so it must never be reachable from the LAN.
    server.listen(port, "127.0.0.1", () => resolvePort((server.address() as AddressInfo).port));
  });
}

export async function startWebServer(options: DashboardOptions): Promise<Dashboard> {
  const webDir = resolve(options.webDir ?? DEFAULT_WEB_DIR);
  const api = new ApiClient(options.serverUrl, { token: options.token });
  const health = await api.health();
  const serverMode: ServerMode = health.mode;
  const secretHex = randomBytes(32).toString("hex");
  const secret = Buffer.from(secretHex);
  let port = 0;
  let headers: Record<string, string> = {};

  const allowedHosts = () => [`localhost:${port}`, `127.0.0.1:${port}`];
  const allowedOrigins = () => allowedHosts().map((h) => `http://${h}`);
  const hostOk = (req: IncomingMessage) => allowedHosts().includes((req.headers.host ?? "").toLowerCase());
  const originOk = (req: IncomingMessage) => allowedOrigins().includes((req.headers.origin ?? "").toLowerCase());

  async function serveStatic(req: IncomingMessage, res: ServerResponse, rawPath: string): Promise<void> {
    if (req.method !== "GET" && req.method !== "HEAD") {
      res.writeHead(405, { ...headers, Allow: "GET, HEAD", "Content-Type": "text/plain" });
      res.end("Method not allowed");
      return;
    }
    let decoded: string;
    try {
      decoded = decodeURIComponent(rawPath);
    } catch {
      res.writeHead(400, { ...headers, "Content-Type": "text/plain" });
      res.end("Bad request");
      return;
    }
    const target = resolve(webDir, "." + (decoded === "/" ? "/index.html" : decoded));
    if (decoded.includes("\0") || !target.startsWith(webDir + sep)) {
      res.writeHead(403, { ...headers, "Content-Type": "text/plain" });
      res.end("Forbidden");
      return;
    }
    let data: Buffer;
    try {
      if (!(await stat(target)).isFile()) throw new Error("not a file");
      data = await readFile(target);
    } catch {
      res.writeHead(404, { ...headers, "Content-Type": "text/plain" });
      res.end("Not found");
      return;
    }
    const isIndex = target === join(webDir, "index.html");
    if (isIndex) {
      const meta = `<meta name="${SESSION_META}" content="${secretHex}">`;
      data = Buffer.from(data.toString("utf8").replace("</head>", `  ${meta}\n</head>`));
    }
    const cache = isIndex ? "no-store" : HASHED_ASSET_RE.test(target) ? "public, max-age=31536000, immutable" : "no-cache";
    res.writeHead(200, {
      ...headers,
      "Content-Type": MIME_TYPES[extname(target)] ?? "application/octet-stream",
      "Content-Length": String(data.length),
      "Cache-Control": cache,
    });
    res.end(req.method === "HEAD" ? undefined : data);
  }

  async function handleApi(req: IncomingMessage, res: ServerResponse, path: string): Promise<void> {
    const method = req.method ?? "GET";
    const deviceMatch = /^\/api\/devices\/([^/]{1,64})(\/identify)?$/.exec(path);
    let deviceId = "";
    if (deviceMatch !== null) {
      try {
        deviceId = decodeURIComponent(deviceMatch[1]);
      } catch {
        throw new HttpError(400, "invalid_request", "malformed device id");
      }
    }
    const route = `${method} ${deviceMatch === null ? path : deviceMatch[2] === undefined ? "/api/devices/:id" : "/api/devices/:id/identify"}`;

    switch (route) {
      case "GET /api/config":
        return sendJson(res, 200, { serverUrl: options.serverUrl, serverVersion: health.version, mode: serverMode, local: options.local }, headers);
      case "GET /api/room":
        return sendJson(res, 200, await api.room(), headers);
      case "PUT /api/room/settings":
        return sendJson(res, 200, await api.updateRoomSettings((await readJsonBody(req)) as UpdateRoomSettingsRequest), headers);
      case "GET /api/devices":
        return sendJson(res, 200, { devices: await api.devices() }, headers);
      case "PATCH /api/devices/:id": {
        const name = optionalString(await readJsonBody(req), "name");
        if (name === undefined) throw new HttpError(400, "invalid_request", "name is required");
        return sendJson(res, 200, await api.renameDevice(deviceId, name), headers);
      }
      case "DELETE /api/devices/:id":
        await api.removeDevice(deviceId);
        return sendJson(res, 204, null, headers);
      case "POST /api/devices/:id/identify": {
        const durationMs = (await readJsonBody(req)).durationMs;
        if (durationMs !== undefined && typeof durationMs !== "number") throw new HttpError(400, "invalid_request", "durationMs must be a number");
        return sendJson(res, 202, await api.identifyDevice(deviceId, durationMs), headers);
      }
      case "POST /api/pair/confirm": {
        const body = await readJsonBody(req);
        const code = optionalString(body, "code");
        if (code === undefined) throw new HttpError(400, "invalid_request", "code is required");
        return sendJson(res, 200, await api.confirmPairing(code, optionalString(body, "name")), headers);
      }
      default:
        throw new HttpError(404, "not_found", "no such dashboard endpoint");
    }
  }

  const server = createServer((req, res) => {
    const rawPath = (req.url ?? "/").split("?")[0];
    if (!hostOk(req)) return sendError(res, 403, "forbidden", "unexpected Host header", headers);
    const isRead = req.method === "GET" || req.method === "HEAD";
    if (!isRead && !originOk(req)) return sendError(res, 403, "forbidden", "cross-origin request refused", headers);

    if (rawPath === "/dev-session" && options.dev === true && req.method === "GET") {
      return sendJson(res, 200, { secret: secretHex }, headers);
    }
    if (rawPath === "/api" || rawPath.startsWith("/api/")) {
      const given = req.headers[SESSION_HEADER];
      if (!sameSecret(Array.isArray(given) ? given[0] : given, secret)) {
        return sendError(res, 403, "forbidden", "missing or wrong dashboard session", headers);
      }
      handleApi(req, res, rawPath).catch((err: unknown) => {
        if (res.headersSent) return;
        if (err instanceof HttpError) sendError(res, err.status, err.code, err.message, headers);
        else if (err instanceof ApiError) sendError(res, err.status, err.code, err.reason, headers);
        else if (err instanceof UnreachableError) sendError(res, 502, "internal", err.message, headers);
        else sendError(res, 500, "internal", err instanceof Error ? err.message : String(err), headers);
      });
      return;
    }
    serveStatic(req, res, rawPath).catch(() => {
      if (!res.headersSent) sendError(res, 500, "internal", "cannot read the web UI", headers);
    });
  });

  const wss = new WebSocketServer({ noServer: true, maxPayload: HARD_FRAME_BYTES });

  server.on("upgrade", (req: IncomingMessage, socket: Socket, head: Buffer) => {
    const url = new URL(req.url ?? "/", "http://dashboard");
    const refuse = (status: string) => {
      socket.end(`HTTP/1.1 ${status}\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`);
      socket.destroy();
    };
    if (url.pathname !== "/ws") return refuse("404 Not Found");
    if (!hostOk(req) || !originOk(req) || !sameSecret(url.searchParams.get("s"), secret)) return refuse("403 Forbidden");
    wss.handleUpgrade(req, socket, head, (browser) => relay(browser));
  });

  function relay(browser: WebSocket): void {
    const upstream = new WebSocket(serverWsUrl(options.serverUrl), {
      headers: { Authorization: `Bearer ${options.token}` },
      maxPayload: HARD_FRAME_BYTES,
    });
    let registered = false;
    let queue: string[] = [];

    const closeBrowser = (code: number, reason: string) => {
      queue = [];
      if (browser.readyState === WebSocket.OPEN || browser.readyState === WebSocket.CONNECTING) {
        browser.close(forwardableCloseCode(code), reason.slice(0, 120));
      }
    };

    upstream.on("unexpected-response", (request, response) => {
      const status = response.statusCode ?? 0;
      response.resume();
      request.destroy();
      if (status === 401) closeBrowser(CLOSE_TOKEN_REJECTED, "the server rejected the dashboard token");
      else closeBrowser(CLOSE_UPSTREAM_FAILED, `the server refused the WebSocket (HTTP ${status})`);
    });
    upstream.on("open", () => {
      upstream.send(serialize(buildRegister({ role: "consumer", client: { kind: "dashboard", version: CLIENT_VERSION } })));
    });
    upstream.on("message", (data: RawData) => {
      const text = data.toString();
      if (!registered && text.includes('"register_ack"')) {
        try {
          registered = (JSON.parse(text) as { type?: unknown }).type === "register_ack";
        } catch {
          registered = false;
        }
        if (registered) {
          for (const frame of queue) upstream.send(frame);
          queue = [];
        }
      }
      if (browser.readyState === WebSocket.OPEN) browser.send(text);
    });
    upstream.on("close", (code: number, reason: Buffer) => {
      const text = reason.toString();
      closeBrowser(code, text !== "" ? text : code === 1006 ? "lost the connection to the server" : "");
    });
    upstream.on("error", () => {
      // 'close' follows every error; closeBrowser reports it to the browser.
    });

    browser.on("message", (data: RawData, isBinary: boolean) => {
      if (isBinary) return;
      const text = data.toString();
      if (registered && upstream.readyState === WebSocket.OPEN) upstream.send(text);
      else if (!registered && queue.length < MAX_QUEUED_FRAMES) queue.push(text);
    });
    browser.on("close", () => {
      queue = [];
      if (upstream.readyState === WebSocket.CONNECTING) upstream.terminate();
      else if (upstream.readyState === WebSocket.OPEN) upstream.close(1000);
    });
    browser.on("error", () => {
      // 'close' follows and tears the upstream down.
    });
  }

  port = await listen(server, options.port);
  headers = securityHeaders(port);
  return {
    port,
    url: `http://localhost:${port}`,
    secret: secretHex,
    address: () => server.address() as AddressInfo,
    close: () =>
      new Promise<void>((done) => {
        for (const client of wss.clients) client.terminate();
        wss.close();
        server.close(() => done());
        server.closeAllConnections();
      }),
  };
}
