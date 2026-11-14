import { createServer, type IncomingMessage, type Server, type ServerResponse } from "node:http";
import type { AddressInfo } from "node:net";
import { readFile, stat } from "node:fs/promises";
import { join, extname } from "node:path";
import { fileURLToPath } from "node:url";
import { WebSocketServer, WebSocket } from "ws";
import { HARD_FRAME_BYTES, buildRegister, serialize } from "@devotchi/shared";
import { serverWsUrl } from "./config.js";
import { CliError } from "./errors.js";
import { CLIENT_VERSION } from "./version.js";

const __dirname = fileURLToPath(new URL(".", import.meta.url));
const WEB_DIR = join(__dirname, "web");

const MIME_TYPES: Record<string, string> = {
  ".html": "text/html",
  ".js": "application/javascript",
  ".css": "text/css",
  ".json": "application/json",
  ".png": "image/png",
  ".svg": "image/svg+xml",
  ".ico": "image/x-icon",
};

export interface DashboardOptions {
  serverUrl: string;
  token: string;
  roomId: string;
  /** 0 picks a free port. */
  port: number;
  local: boolean;
}

export interface Dashboard {
  port: number;
  close(): Promise<void>;
}

async function serveStatic(res: ServerResponse, urlPath: string): Promise<void> {
  let filePath = join(WEB_DIR, urlPath === "/" ? "index.html" : urlPath);

  try {
    const s = await stat(filePath);
    if (s.isDirectory()) {
      filePath = join(filePath, "index.html");
    }
  } catch {
    // fall through to 404
  }

  try {
    const data = await readFile(filePath);
    const ext = extname(filePath);
    res.writeHead(200, { "Content-Type": MIME_TYPES[ext] || "application/octet-stream" });
    res.end(data);
  } catch {
    res.writeHead(404, { "Content-Type": "text/plain" });
    res.end("Not found");
  }
}

function listen(server: Server, port: number): Promise<number> {
  return new Promise((resolve, reject) => {
    server.once("error", (err: NodeJS.ErrnoException) => {
      if (err.code === "EADDRINUSE") reject(new CliError(`dashboard port ${port} is already in use; choose another with --port <n>`));
      else reject(new CliError(`cannot start the dashboard on port ${port}: ${err.message}`));
    });
    server.listen(port, "127.0.0.1", () => resolve((server.address() as AddressInfo).port));
  });
}

export async function startWebServer(options: DashboardOptions): Promise<Dashboard> {
  const server = createServer(async (req: IncomingMessage, res: ServerResponse) => {
    const url = req.url || "/";
    const pathname = url.split("?")[0];

    if (req.method === "GET" && pathname === "/api/config") {
      res.writeHead(200, { "Content-Type": "application/json" });
      res.end(JSON.stringify({ roomId: options.roomId, serverUrl: options.serverUrl, local: options.local }));
      return;
    }

    await serveStatic(res, pathname);
  });

  const wss = new WebSocketServer({ server, path: "/ws", maxPayload: HARD_FRAME_BYTES });

  wss.on("connection", (clientWs: WebSocket) => {
    const upstream = new WebSocket(serverWsUrl(options.serverUrl), {
      headers: { Authorization: `Bearer ${options.token}` },
      maxPayload: HARD_FRAME_BYTES,
    });

    upstream.on("open", () => {
      upstream.send(
        serialize(buildRegister({ role: "consumer", client: { kind: "dashboard", version: CLIENT_VERSION } })),
      );
    });

    upstream.on("message", (data) => {
      if (clientWs.readyState === WebSocket.OPEN) {
        clientWs.send(data.toString());
      }
    });

    upstream.on("close", () => {
      if (clientWs.readyState === WebSocket.OPEN) {
        clientWs.close();
      }
    });

    upstream.on("error", (err) => {
      console.error("[devotchi] Upstream WebSocket error:", err.message);
    });

    clientWs.on("message", (data) => {
      if (upstream.readyState === WebSocket.OPEN) {
        upstream.send(data.toString());
      }
    });

    clientWs.on("close", () => {
      upstream.close();
    });
  });

  const port = await listen(server, options.port);
  return {
    port,
    close: () =>
      new Promise<void>((resolve) => {
        for (const client of wss.clients) client.terminate();
        wss.close();
        server.close(() => resolve());
        server.closeAllConnections();
      }),
  };
}
