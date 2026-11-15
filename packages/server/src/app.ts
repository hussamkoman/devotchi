// Package entry: createServer() builds one self-contained server instance.
// Importing this module opens no socket and starts no timer.

import { readFileSync } from "node:fs";
import { createServer as createHttpServer, type Server } from "node:http";
import type { AddressInfo } from "node:net";
import Koa from "koa";
import { AccountService } from "./accounts/service.js";
import { resolveConfig, type ResolvedConfig, type ServerConfigInput } from "./config.js";
import { openDatabase, type Db } from "./db/index.js";
import { DeviceService } from "./devices/service.js";
import { HookDispatcher } from "./hooks/dispatcher.js";
import { Authenticator } from "./http/auth.js";
import { bodyParser, rejectDuplicateQuery } from "./http/body.js";
import { errorMiddleware } from "./http/errors.js";
import { RateLimits } from "./http/rate-limit.js";
import { requestLog } from "./http/request-log.js";
import { cors, securityHeaders } from "./http/security.js";
import { Logger, errorFields } from "./log.js";
import { PairingService } from "./pairing/service.js";
import { PermissionManager } from "./permissions/manager.js";
import { TimeService } from "./time/service.js";
import { RoomRegistry } from "./rooms/registry.js";
import { accountRoutes } from "./routes/accounts.js";
import { deviceRoutes } from "./routes/devices.js";
import { healthRoutes } from "./routes/health.js";
import { hookRoutes } from "./routes/hooks.js";
import { pairingRoutes } from "./routes/pairing.js";
import { roomRoutes } from "./routes/room.js";
import { tokenRoutes } from "./routes/tokens.js";
import { StartupError } from "./startup-error.js";
import { WsServer } from "./ws/server.js";

export { ConfigError, loadConfig, resolveConfig, type ServerConfigInput, type ResolvedConfig } from "./config.js";
export { DatabaseError } from "./db/index.js";
export { StartupError } from "./startup-error.js";
export { SERVER_VERSION } from "./version.js";
export type { LogSink } from "./log.js";

export type ShutdownHook = () => void | Promise<void>;

/** Building blocks of a running instance, for tests and for the modules wired into it. */
export interface ServerInternals {
  config: ResolvedConfig;
  db: Db;
  log: Logger;
  rooms: RoomRegistry;
  ws: WsServer;
  permissions: PermissionManager;
  time: TimeService;
  devices: DeviceService;
  pairing: PairingService;
  /** Multi-user mode only. */
  accounts: AccountService | null;
  auth: Authenticator;
  limits: RateLimits;
  http: Server;
  /** Runs at the start of close(), before sockets are closed and the database. */
  onShutdown(hook: ShutdownHook): void;
}

export interface DevotchiServer {
  /** The port actually bound (useful with port 0). */
  port: number;
  /** Base HTTP URL reachable from this machine. */
  url: string;
  /** Idempotent; resolves when sockets, HTTP server and database are closed. */
  close(): Promise<void>;
  internals: ServerInternals;
}

/** In-flight HTTP responses get this long to finish after the shutdown hooks ran. */
const DRAIN_MS = 500;
const MAINTENANCE_MS = 60_000;
const ACCOUNT_PAGE = new URL("../public/account.html", import.meta.url);

/** Set by the client's bundler, which ships the server without its public/ directory. */
declare const __DEVOTCHI_ACCOUNT_PAGE__: string | undefined;

function readAccountPage(): string {
  if (typeof __DEVOTCHI_ACCOUNT_PAGE__ === "string") return __DEVOTCHI_ACCOUNT_PAGE__;
  try {
    return readFileSync(ACCOUNT_PAGE, "utf8");
  } catch (err) {
    throw new StartupError(`cannot read the account page ${ACCOUNT_PAGE.pathname}: ${(err as Error).message}`);
  }
}

function listen(server: Server, port: number, host: string): Promise<void> {
  return new Promise((resolve, reject) => {
    const onError = (err: NodeJS.ErrnoException) => {
      server.off("listening", onListening);
      if (err.code === "EADDRINUSE") reject(new StartupError(`port ${port} on ${host} is already in use`));
      else if (err.code === "EACCES") reject(new StartupError(`no permission to listen on ${host}:${port}`));
      else if (err.code === "EADDRNOTAVAIL") reject(new StartupError(`address ${host} is not available on this machine`));
      else reject(new StartupError(`cannot listen on ${host}:${port}: ${err.message}`));
    };
    const onListening = () => {
      server.off("error", onError);
      resolve();
    };
    server.once("error", onError);
    server.once("listening", onListening);
    server.listen(port, host);
  });
}

function baseUrl(host: string, port: number): string {
  const reachable = host === "0.0.0.0" ? "127.0.0.1" : host === "::" ? "::1" : host;
  return `http://${reachable.includes(":") ? `[${reachable}]` : reachable}:${port}`;
}

/**
 * Starts a server. Throws ConfigError for invalid configuration and
 * StartupError (DatabaseError included) when the database or the port
 * cannot be used.
 */
export async function createServer(input: ServerConfigInput): Promise<DevotchiServer> {
  const config = resolveConfig(input);
  const log = new Logger(config.logLevel, config.logSink);
  const accountPage = config.mode === "multi-user" ? readAccountPage() : "";
  const db = openDatabase(config.dbPath, log);
  const startedAt = Date.now();
  const rooms = new RoomRegistry();
  const auth = new Authenticator(db, config.mode, config.timing.lastUsedWriteMs);
  const limits = new RateLimits(config.timing.rateWindowMs);
  const ws = new WsServer({ config, db, rooms, auth, log });
  const permissions = new PermissionManager(rooms, db, log, config.timing, config.features);
  const time = new TimeService({
    db,
    rooms,
    checkMs: config.timing.timeCheckMs,
    intervalMs: config.timing.timeIntervalMs,
  });
  const dispatcher = new HookDispatcher({ config, db, rooms, permissions });
  const devices = new DeviceService({ db, rooms, ws, permissions, log, writeIntervalMs: config.timing.deviceWriteMs });
  const pairing = new PairingService({ db, ws, log, timing: config.timing });
  // Nothing of a confirmed pairing's token survived the restart, so those pairings cannot be delivered.
  pairing.prune();
  const accounts = config.mode === "multi-user" ? new AccountService(db, config.bcryptCost, config.timing) : null;
  if (accounts) {
    accounts.prune();
    auth.setSessionResolver((raw, now) => accounts.resolveSession(raw, now));
  }
  ws.handle("holding", (conn, msg) => permissions.onHolding(conn, msg.ids));
  ws.handle("permission_response", (conn, msg) => permissions.onResponse(conn, msg.id, msg.granted));
  ws.handle("permission_defer", (conn, msg) => permissions.onDefer(conn, msg.id));
  ws.handle("decide", (conn, msg) => permissions.onDecide(conn, msg.id, msg.choice));
  ws.handle("question_response", (conn, msg) => permissions.onQuestionResponse(conn, msg.id, msg.answers));
  ws.handle("extend", (conn, msg) => permissions.onExtend(conn, msg.id));
  ws.onConsumerRegistered((conn, room) => permissions.onConsumerRegistered(conn, room));
  ws.handle("telemetry", (conn, msg) => devices.onTelemetry(conn, msg));
  ws.onConnectionClosed((conn, room) => {
    if (conn.role !== "consumer") return;
    permissions.onConsumerGone(conn, room);
    devices.onClosed(conn);
  });
  // Held hooks are answered with the neutral reply before the sockets close.
  const shutdownHooks: ShutdownHook[] = [
    () => {
      dispatcher.shutdown();
      permissions.shutdown();
    },
  ];

  const app = new Koa({ proxy: config.trustProxy, maxIpsCount: 1 });
  // Errors are logged by errorMiddleware; Koa's own handler would print stacks to stderr.
  app.silent = true;
  app.use(requestLog(log));
  app.use(errorMiddleware(log));
  app.use(securityHeaders());
  app.use(cors(config.corsOrigins));
  app.use(rejectDuplicateQuery());
  app.use(bodyParser());

  for (const router of [
    healthRoutes(config.mode, startedAt),
    tokenRoutes({ db, config, auth, limits, ws }),
    pairingRoutes({ auth, limits, pairing }),
    deviceRoutes({ auth, devices }),
    roomRoutes({
      db,
      config,
      auth,
      limits,
      rooms,
      ws,
      permissions,
      listDevices: (roomId) => devices.list(roomId),
      onTimeZoneChanged: (roomId) => time.onZoneChanged(roomId),
    }),
    hookRoutes({ auth, limits, dispatcher, permissions, log }),
    ...(accounts ? [accountRoutes({ config, auth, limits, accounts, accountPage })] : []),
  ]) {
    app.use(router.routes());
  }

  const http = createHttpServer(app.callback());
  http.on("clientError", (err, socket) => {
    log.debug("client error", errorFields(err));
    if (socket.writable) socket.end("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
    else socket.destroy();
  });
  ws.attach(http);
  try {
    await listen(http, config.port, config.host);
  } catch (err) {
    await ws.shutdown();
    db.close();
    throw err;
  }

  const maintenance = setInterval(() => {
    const now = Date.now();
    limits.sweep(now);
    rooms.prune(now, config.timing.replayMaxAgeMs, config.timing.sessionTtlMs);
    try {
      pairing.prune(now);
      accounts?.prune(now);
    } catch (err) {
      log.error("maintenance failed", errorFields(err));
    }
  }, MAINTENANCE_MS);
  maintenance.unref();
  devices.start();
  time.start();

  const port = (http.address() as AddressInfo).port;
  const url = baseUrl(config.host, port);
  log.info("listening", { host: config.host, port, mode: config.mode });

  let closing: Promise<void> | null = null;
  const close = (): Promise<void> => {
    closing ??= (async () => {
      clearInterval(maintenance);
      const httpClosed = new Promise<void>((resolve) => http.close(() => resolve()));
      for (const hook of shutdownHooks) {
        try {
          await hook();
        } catch (err) {
          log.error("shutdown hook failed", errorFields(err));
        }
      }
      await ws.shutdown();
      devices.stop();
      time.stop();
      http.closeIdleConnections();
      let drainTimer: NodeJS.Timeout | undefined;
      await Promise.race([httpClosed, new Promise<void>((resolve) => (drainTimer = setTimeout(resolve, DRAIN_MS)))]);
      clearTimeout(drainTimer);
      http.closeAllConnections();
      await httpClosed;
      db.close();
      log.info("stopped");
    })();
    return closing;
  };

  return {
    port,
    url,
    close,
    internals: {
      config,
      db,
      log,
      rooms,
      ws,
      permissions,
      time,
      devices,
      pairing,
      accounts,
      auth,
      limits,
      http,
      onShutdown: (hook) => shutdownHooks.push(hook),
    },
  };
}
