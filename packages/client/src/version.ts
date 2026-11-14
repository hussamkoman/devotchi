import { readFileSync } from "node:fs";

/** Set by scripts/bundle.mjs from package.json at build time. */
declare const __DEVOTCHI_CLIENT_VERSION__: string | undefined;

function readVersion(): string {
  if (typeof __DEVOTCHI_CLIENT_VERSION__ === "string") return __DEVOTCHI_CLIENT_VERSION__;
  const pkg = JSON.parse(readFileSync(new URL("../package.json", import.meta.url), "utf8")) as { version?: unknown };
  return typeof pkg.version === "string" ? pkg.version : "0.0.0";
}

/** Version of this client package, sent in `register.client.version` and the MCP server info. */
export const CLIENT_VERSION = readVersion();
