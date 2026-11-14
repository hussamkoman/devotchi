import { readFileSync } from "node:fs";

function readVersion(): string {
  const pkg = JSON.parse(readFileSync(new URL("../package.json", import.meta.url), "utf8")) as { version?: unknown };
  return typeof pkg.version === "string" ? pkg.version : "0.0.0";
}

/** Version of this client package, sent in `register.client.version` and the MCP server info. */
export const CLIENT_VERSION = readVersion();
