import { readFile, writeFile, mkdir } from "node:fs/promises";
import { homedir } from "node:os";
import { join } from "node:path";
import { DEFAULT_CLIENT_WEB_PORT, DEFAULT_SERVER_URL } from "@devotchi/shared";

/** The settings file as this CLI reads and writes it. */
export interface ClientSettings {
  serverUrl: string;
  token: string;
  roomId: string;
  pairingCode: string;
  webPort: number;
  localMode: boolean;
}

const CONFIG_DIR = join(homedir(), ".devotchi");
const CONFIG_PATH = join(CONFIG_DIR, "config.json");

export { CONFIG_DIR, CONFIG_PATH };

const DEFAULTS: ClientSettings = {
  serverUrl: DEFAULT_SERVER_URL,
  token: "",
  roomId: "",
  pairingCode: "",
  webPort: DEFAULT_CLIENT_WEB_PORT,
  localMode: false,
};

export async function loadConfig(): Promise<ClientSettings> {
  try {
    const raw = await readFile(CONFIG_PATH, "utf-8");
    const loaded = JSON.parse(raw);
    const config: ClientSettings = { ...DEFAULTS, ...loaded };

    if (typeof config.serverUrl !== "string" || !config.serverUrl.startsWith("http")) {
      config.serverUrl = DEFAULTS.serverUrl;
    }
    if (typeof config.webPort !== "number" || config.webPort < 1 || config.webPort > 65535) {
      config.webPort = DEFAULTS.webPort;
    }

    return config;
  } catch {
    try {
      await mkdir(CONFIG_DIR, { recursive: true });
      await writeFile(CONFIG_PATH, JSON.stringify(DEFAULTS, null, 2));
    } catch {
      // ignore
    }
    return { ...DEFAULTS };
  }
}

export async function saveConfig(config: ClientSettings): Promise<void> {
  await mkdir(CONFIG_DIR, { recursive: true });
  await writeFile(CONFIG_PATH, JSON.stringify(config, null, 2));
}

export async function provisionToken(config: ClientSettings): Promise<ClientSettings> {
  if (config.token && config.roomId) return config;

  const baseUrl = config.serverUrl.replace(/\/$/, "");
  console.error("[devotchi] No token found, provisioning from server...");

  const res = await fetch(`${baseUrl}/api/auth/token`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ label: "cli" }),
  });

  if (!res.ok) {
    throw new Error(`Failed to provision token: ${res.status} ${res.statusText}`);
  }

  const data = await res.json() as { token: string; id: string; roomId: string };
  config.token = data.token;
  config.roomId = data.roomId;

  // Create a pairing code for device setup
  const pairRes = await fetch(`${baseUrl}/api/pair`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "Authorization": `Bearer ${config.token}`,
    },
  });

  if (pairRes.ok) {
    const pairData = await pairRes.json() as { code: string; expiresAt: string };
    config.pairingCode = pairData.code;
    console.error(`[devotchi] Pairing code: ${pairData.code} (expires: ${pairData.expiresAt})`);
  }

  await saveConfig(config);
  console.error(`[devotchi] Token provisioned, room: ${config.roomId}`);

  return config;
}
