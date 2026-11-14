import { join } from "node:path";
import { homedir } from "node:os";
import {
  DEFAULT_SERVER_HOST,
  DEFAULT_SERVER_TIMING,
  type ServerConfig,
  type ServerMode,
} from "@devotchi/shared";

export function loadConfig(): ServerConfig {
  const mode = (process.env.DEVOTCHI_MODE || "single-user") as ServerMode;
  const port = parseInt(process.env.DEVOTCHI_PORT || "8080", 10);
  const dbPath =
    process.env.DEVOTCHI_DB_PATH || join(homedir(), ".devotchi", "devotchi.db");
  const corsOrigins = process.env.DEVOTCHI_CORS_ORIGINS
    ? process.env.DEVOTCHI_CORS_ORIGINS.split(",").map((s) => s.trim())
    : [];

  return {
    host: process.env.DEVOTCHI_HOST || DEFAULT_SERVER_HOST,
    port,
    mode,
    dbPath,
    corsOrigins,
    trustProxy: process.env.DEVOTCHI_TRUST_PROXY === "1",
    adminSecret: process.env.DEVOTCHI_ADMIN_SECRET,
    registration: "open",
    inviteCode: process.env.DEVOTCHI_INVITE_CODE,
    logLevel: "info",
    timing: { ...DEFAULT_SERVER_TIMING },
  };
}
