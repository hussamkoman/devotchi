// `devotchi config [get <key> | set <key> <value> | path]`.

import { ConfigStore, maskToken, normaliseServerUrl } from "../config.js";
import type { Io } from "../context.js";
import { UsageError } from "../errors.js";

const KEYS = ["serverUrl", "webPort"] as const;
type Key = (typeof KEYS)[number];

function asKey(key: string): Key {
  if (!(KEYS as readonly string[]).includes(key)) throw new UsageError(`unknown key "${key}"; keys: ${KEYS.join(", ")}`);
  return key as Key;
}

export async function showConfig(io: Io): Promise<number> {
  const store = new ConfigStore(io.env);
  const config = await store.load();
  const servers = Object.fromEntries(
    Object.entries(config.servers).map(([url, creds]) => [url, { token: maskToken(creds.token), roomId: creds.roomId }]),
  );
  io.out(JSON.stringify({ path: store.path, ...config, servers }, null, 2));
  return 0;
}

export async function configPath(io: Io): Promise<number> {
  io.out(new ConfigStore(io.env).path);
  return 0;
}

export async function configGet(key: string, io: Io): Promise<number> {
  const config = await new ConfigStore(io.env).load();
  io.out(String(config[asKey(key)]));
  return 0;
}

export async function configSet(key: string, value: string, io: Io): Promise<number> {
  const name = asKey(key);
  const store = new ConfigStore(io.env);
  if (name === "serverUrl") {
    const url = normaliseServerUrl(value);
    await store.update((config) => {
      config.serverUrl = url;
    });
    io.out(`serverUrl = ${url}`);
  } else {
    if (!/^\d+$/.test(value) || Number(value) < 1 || Number(value) > 65535) {
      throw new UsageError("webPort must be a port number (1..65535)");
    }
    await store.update((config) => {
      config.webPort = Number(value);
    });
    io.out(`webPort = ${value}`);
  }
  return 0;
}
