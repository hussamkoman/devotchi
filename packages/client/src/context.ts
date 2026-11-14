// What every command that talks to a server needs: the selected server URL,
// verified credentials for it and a REST client carrying them.

import type { ResolvedCredentials } from "./credentials.js";
import { ApiClient } from "./api.js";
import { ConfigStore, type Env } from "./config.js";
import { ServerCredentialSource } from "./credentials.js";

export interface Io {
  env: Env;
  /** Command output (stdout). */
  out: (line: string) => void;
  /** Progress and warnings (stderr). */
  note: (line: string) => void;
}

export interface ServerContext {
  store: ConfigStore;
  url: string;
  creds: ResolvedCredentials;
  api: ApiClient;
}

/** Credentials for the selected server, provisioned on first use and verified with GET /api/me. */
export async function connectServer(flag: string | undefined, io: Io): Promise<ServerContext> {
  const store = new ConfigStore(io.env);
  const { url } = await store.resolveServer(flag);
  const source = new ServerCredentialSource(store, url, io.note);
  const creds = await source.verify(await source.get());
  return { store, url, creds, api: new ApiClient(url, { token: creds.token }) };
}

export function printJson(io: Io, value: unknown): void {
  io.out(JSON.stringify(value, null, 2));
}
