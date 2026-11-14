import { readFileSync } from "node:fs";

// src/ and dist/ are both one level below the package root.
const pkg = JSON.parse(readFileSync(new URL("../package.json", import.meta.url), "utf8")) as { version: string };

export const SERVER_VERSION: string = pkg.version;
