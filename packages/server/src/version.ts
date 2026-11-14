import { readFileSync } from "node:fs";

/** Set by the client's bundler, where import.meta.url points into the client package. */
declare const __DEVOTCHI_SERVER_VERSION__: string | undefined;

function readVersion(): string {
  if (typeof __DEVOTCHI_SERVER_VERSION__ === "string") return __DEVOTCHI_SERVER_VERSION__;
  // src/ and dist/ are both one level below the package root.
  const pkg = JSON.parse(readFileSync(new URL("../package.json", import.meta.url), "utf8")) as { version: string };
  return pkg.version;
}

export const SERVER_VERSION: string = readVersion();
