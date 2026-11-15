// The dashboard's security boundary and its proxy, against a real server.
// Covers the three audited attacks: path traversal reads, cross-origin
// WebSocket approval, and LAN access to the dashboard.

import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { connect, createServer as createTcpServer } from "node:net";
import { networkInterfaces, tmpdir } from "node:os";
import { join } from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { WebSocket } from "ws";
import { startTestServer, type TestServer } from "../../server/test/helpers.js";
import { startWebServer, type Dashboard } from "../src/web-server.js";

const SECRET_TEXT = "TOP-SECRET-OUTSIDE-WEBDIR";

let t: TestServer;
let root: string;
let webDir: string;
let dash: Dashboard;
let token: string;
let roomId: string;

beforeAll(async () => {
  t = await startTestServer();
  ({ token, roomId } = t.cliToken("dashboard-test"));
  root = await mkdtemp(join(tmpdir(), "devotchi-web-"));
  webDir = join(root, "web");
  await mkdir(join(webDir, "assets"), { recursive: true });
  await mkdir(join(root, "web-evil"));
  await writeFile(join(root, "secret.txt"), SECRET_TEXT);
  await writeFile(join(root, "web-evil", "x.txt"), SECRET_TEXT);
  await writeFile(join(webDir, "index.html"), "<!DOCTYPE html><html><head><title>t</title></head><body></body></html>");
  await writeFile(join(webDir, "assets", "index-AbCd1234.js"), "export {};");
  await writeFile(join(webDir, "assets", "index-AbCd1234.js.map"), "{}");
  await writeFile(join(webDir, "font.woff2"), "w");
  await writeFile(join(webDir, "pic.webp"), "p");
  await writeFile(join(webDir, "favicon.svg"), "<svg/>");
  await writeFile(join(webDir, "notes.txt"), "n");
  dash = await startWebServer({ serverUrl: t.url, token, port: 0, local: false, webDir });
});

afterAll(async () => {
  await dash?.close();
  await t?.close();
  await rm(root, { recursive: true, force: true });
});

const origin = () => `http://localhost:${dash.port}`;

/** A raw HTTP/1.1 request, so the path reaches the server exactly as written (curl --path-as-is). */
function raw(path: string, opts: { method?: string; host?: string; headers?: string[] } = {}): Promise<{ status: number; text: string }> {
  return new Promise((resolve, reject) => {
    const socket = connect(dash.port, "127.0.0.1");
    let data = "";
    socket.on("data", (chunk) => (data += chunk.toString("latin1")));
    socket.on("error", reject);
    socket.on("end", () => resolve({ status: Number(/^HTTP\/1\.1 (\d{3})/.exec(data)?.[1] ?? 0), text: data }));
    const lines = [
      `${opts.method ?? "GET"} ${path} HTTP/1.1`,
      `Host: ${opts.host ?? `localhost:${dash.port}`}`,
      "Connection: close",
      ...(opts.headers ?? []),
      "",
      "",
    ];
    socket.write(lines.join("\r\n"));
  });
}

function apiFetch(path: string, init: RequestInit & { json?: unknown; secret?: string | null } = {}): Promise<Response> {
  const headers = new Headers(init.headers);
  if (init.secret !== null) headers.set("X-Devotchi-Session", init.secret ?? dash.secret);
  if (init.method !== undefined && init.method !== "GET") headers.set("Origin", origin());
  if (init.json !== undefined) headers.set("Content-Type", "application/json");
  return fetch(`http://127.0.0.1:${dash.port}${path}`, {
    ...init,
    headers,
    body: init.json === undefined ? init.body : JSON.stringify(init.json),
  });
}

function openWs(path: string, wsOrigin: string | null = origin()): Promise<{ ws: WebSocket; status: number | null }> {
  return new Promise((resolve) => {
    const ws = new WebSocket(`ws://127.0.0.1:${dash.port}${path}`, wsOrigin === null ? {} : { origin: wsOrigin });
    ws.once("unexpected-response", (_req, res) => {
      resolve({ ws, status: res.statusCode ?? 0 });
      ws.terminate();
    });
    ws.once("open", () => resolve({ ws, status: null }));
    ws.on("error", () => undefined);
  });
}

function nextMessage(ws: WebSocket, type: string, timeoutMs = 3000): Promise<Record<string, unknown>> {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`no ${type} within ${timeoutMs} ms`)), timeoutMs);
    const onMessage = (data: Buffer) => {
      const msg = JSON.parse(data.toString()) as Record<string, unknown>;
      if (msg.type !== type) return;
      clearTimeout(timer);
      ws.off("message", onMessage);
      resolve(msg);
    };
    ws.on("message", onMessage);
  });
}

function closed(ws: WebSocket): Promise<{ code: number; reason: string }> {
  return new Promise((resolve) => ws.once("close", (code, reason) => resolve({ code, reason: reason.toString() })));
}

describe("static files", () => {
  it("never serves a file outside the web directory (path traversal)", async () => {
    const attempts = [
      "/../secret.txt",
      "/assets/../../secret.txt",
      "/%2e%2e/secret.txt",
      "/%2E%2E%2Fsecret.txt",
      "/..%2fsecret.txt",
      "/assets/%2e%2e/%2e%2e/secret.txt",
      "/..%5csecret.txt",
      "/../web-evil/x.txt",
      "/%2e%2e/web-evil/x.txt",
      "//etc/passwd",
      "/%2e%2e/%2e%2e/%2e%2e/%2e%2e/etc/passwd",
      "/index.html%00.js",
      "/%",
    ];
    for (const path of attempts) {
      const res = await raw(path);
      expect([400, 403, 404], path).toContain(res.status);
      expect(res.text, path).not.toContain(SECRET_TEXT);
      expect(res.text, path).not.toContain("root:");
    }
    expect((await raw("/../secret.txt")).status).toBe(403);
  });

  it("serves GET and HEAD only", async () => {
    expect((await raw("/", { method: "POST", headers: [`Origin: ${origin()}`, "Content-Length: 0"] })).status).toBe(405);
    expect((await raw("/", { method: "DELETE", headers: [`Origin: ${origin()}`] })).status).toBe(405);
    const head = await raw("/", { method: "HEAD" });
    expect(head.status).toBe(200);
    expect(head.text).not.toContain("<html>");
  });

  it("injects the session secret into index.html with safe headers", async () => {
    const res = await fetch(`http://127.0.0.1:${dash.port}/`, { headers: { Host: `localhost:${dash.port}` } });
    expect(res.status).toBe(200);
    const html = await res.text();
    expect(html).toContain(`<meta name="devotchi-session" content="${dash.secret}">`);
    expect(dash.secret).toMatch(/^[0-9a-f]{64}$/);
    expect(res.headers.get("cache-control")).toBe("no-store");
    expect(res.headers.get("x-content-type-options")).toBe("nosniff");
    const csp = res.headers.get("content-security-policy") ?? "";
    expect(csp).toContain("default-src 'self'");
    expect(csp).toContain(`connect-src 'self' ws://localhost:${dash.port} ws://127.0.0.1:${dash.port}`);
    expect(csp).toContain("frame-ancestors 'none'");
  });

  it("uses the right MIME types and caches hashed assets forever", async () => {
    const get = (p: string) => fetch(`http://127.0.0.1:${dash.port}${p}`);
    const js = await get("/assets/index-AbCd1234.js");
    expect(js.headers.get("content-type")).toMatch(/^text\/javascript/);
    expect(js.headers.get("cache-control")).toContain("immutable");
    expect((await get("/assets/index-AbCd1234.js.map")).headers.get("content-type")).toBe("application/json");
    expect((await get("/font.woff2")).headers.get("content-type")).toBe("font/woff2");
    expect((await get("/pic.webp")).headers.get("content-type")).toBe("image/webp");
    expect((await get("/favicon.svg")).headers.get("content-type")).toBe("image/svg+xml");
    const txt = await get("/notes.txt");
    expect(txt.headers.get("content-type")).toMatch(/^text\/plain/);
    expect(txt.headers.get("cache-control")).toBe("no-cache");
    expect((await get("/missing.js")).status).toBe(404);
    expect((await get("/assets")).status).toBe(404);
  });
});

describe("request checks", () => {
  it("rejects a foreign or missing Host on every route (DNS rebinding)", async () => {
    for (const host of ["evil.example", `evil.example:${dash.port}`, `localhost:${dash.port + 1}`, `192.168.1.5:${dash.port}`]) {
      expect((await raw("/", { host })).status, host).toBe(403);
      expect((await raw("/api/config", { host, headers: [`X-Devotchi-Session: ${dash.secret}`] })).status, host).toBe(403);
    }
    expect((await raw("/", { host: `127.0.0.1:${dash.port}` })).status).toBe(200);
  });

  it("requires the session secret on /api", async () => {
    expect((await apiFetch("/api/config", { secret: null })).status).toBe(403);
    expect((await apiFetch("/api/config", { secret: "0".repeat(64) })).status).toBe(403);
    expect((await apiFetch("/api/config", { secret: "short" })).status).toBe(403);
    expect((await apiFetch("/api/devices", { secret: null })).status).toBe(403);
    const ok = await apiFetch("/api/config");
    expect(ok.status).toBe(200);
    expect(await ok.json()).toEqual({ serverUrl: t.url, serverVersion: expect.any(String), mode: "single-user", local: false });
  });

  it("refuses state-changing requests from another origin", async () => {
    const body = JSON.stringify({ mode: "desk" });
    const put = (originHeader: string[]) =>
      raw("/api/room/settings", {
        method: "PUT",
        headers: [...originHeader, `X-Devotchi-Session: ${dash.secret}`, "Content-Type: application/json", `Content-Length: ${body.length}`],
      });
    expect((await put(["Origin: http://evil.example"])).status).toBe(403);
    expect((await put([`Origin: https://localhost:${dash.port}`])).status).toBe(403);
    expect((await put([])).status).toBe(403);
    const room = (await (await apiFetch("/api/room")).json()) as { settings: { mode: string } };
    expect(room.settings.mode).toBe("ask");
  });

  it("serves /dev-session only in dev mode", async () => {
    expect((await raw("/dev-session")).status).toBe(404);
    const dev = await startWebServer({ serverUrl: t.url, token, port: 0, local: false, webDir, dev: true });
    try {
      const res = await fetch(`http://127.0.0.1:${dev.port}/dev-session`);
      expect(await res.json()).toEqual({ secret: dev.secret });
      expect(dev.secret).not.toBe(dash.secret);
    } finally {
      await dev.close();
    }
  });
});

describe("network exposure", () => {
  it("listens on loopback only", async () => {
    expect(dash.address().address).toBe("127.0.0.1");
    const lan = Object.values(networkInterfaces())
      .flat()
      .filter((a) => a !== undefined && a.family === "IPv4" && !a.internal)
      .map((a) => a!.address);
    for (const address of lan) {
      const refused = await new Promise<boolean>((resolve) => {
        const s = connect(dash.port, address);
        s.once("connect", () => {
          s.destroy();
          resolve(false);
        });
        s.once("error", () => resolve(true));
      });
      expect(refused, address).toBe(true);
    }
  });

  it("reports a busy port in one line", async () => {
    const blocker = createTcpServer();
    await new Promise<void>((r) => blocker.listen(0, "127.0.0.1", r));
    const port = (blocker.address() as { port: number }).port;
    try {
      await expect(startWebServer({ serverUrl: t.url, token, port, local: false, webDir })).rejects.toThrow(
        `dashboard port ${port} is already in use; choose another with --port <n>`,
      );
    } finally {
      blocker.close();
    }
  });
});

describe("WebSocket proxy", () => {
  it("refuses a cross-origin socket even with the right secret (cross-origin approval)", async () => {
    expect((await openWs(`/ws?s=${dash.secret}`, "http://evil.example")).status).toBe(403);
    expect((await openWs(`/ws?s=${dash.secret}`, null)).status).toBe(403);
    expect((await openWs(`/ws?s=${"0".repeat(64)}`)).status).toBe(403);
    expect((await openWs("/ws")).status).toBe(403);
    expect((await openWs(`/other?s=${dash.secret}`)).status).toBe(404);
  });

  it("registers as a dashboard consumer and relays frames queued before register_ack", async () => {
    const { ws, status } = await openWs(`/ws?s=${dash.secret}`);
    expect(status).toBeNull();
    const mode = nextMessage(ws, "mode");
    const ack = nextMessage(ws, "register_ack");
    ws.send(JSON.stringify({ type: "set_mode", mode: "desk" }));
    expect(await ack).toMatchObject({ role: "consumer", roomId, proto: 2 });
    expect(await mode).toEqual({ type: "mode", mode: "desk" });
    ws.close();
    expect((await apiFetch("/api/room/settings", { method: "PUT", json: { mode: "ask" } })).status).toBe(200);
  });

  it("registers with the dashboard caps and proxies the full text of a pending request", async () => {
    const { ws } = await openWs(`/ws?s=${dash.secret}`);
    const ack = await nextMessage(ws, "register_ack");
    expect(ack.caps).toEqual(expect.arrayContaining(["decide", "ask", "plan"]));
    const room = (await (await apiFetch("/api/room")).json()) as { capableConsumers: Record<string, number> };
    expect(room.capableConsumers).toMatchObject({ decide: 1, ask: 1, plan: 1, pets: 0 });

    const reply = t.fetch("/hook/event", {
      method: "POST",
      token,
      json: { hook_event_name: "PermissionRequest", session_id: "s", cwd: "/tmp", tool_name: "ExitPlanMode", tool_input: { plan: "# Plan\n- one" } },
    });
    const plan = await nextMessage(ws, "plan");
    const detail = await apiFetch(`/api/room/requests/${String(plan.id)}`);
    expect(await detail.json()).toMatchObject({ id: plan.id, kind: "plan", tool: "ExitPlanMode", toolInput: { plan: "# Plan\n- one" } });
    expect((await apiFetch(`/api/room/requests/${"0".repeat(32)}`)).status).toBe(404);
    expect((await apiFetch("/api/room/requests/not-an-id")).status).toBe(404);
    expect((await apiFetch(`/api/room/requests/${String(plan.id)}`, { secret: null })).status).toBe(403);
    ws.send(JSON.stringify({ type: "permission_defer", id: plan.id }));
    expect(await (await reply).json()).toEqual({});
    ws.close();
  });

  it("maps an upstream 401 to close 4003", async () => {
    const bad = await startWebServer({ serverUrl: t.url, token: "00000000-0000-4000-8000-000000000000", port: 0, local: false, webDir });
    try {
      const ws = new WebSocket(`ws://127.0.0.1:${bad.port}/ws?s=${bad.secret}`, { origin: `http://localhost:${bad.port}` });
      ws.on("error", () => undefined);
      expect((await closed(ws)).code).toBe(4003);
    } finally {
      await bad.close();
    }
  });

  it("propagates the upstream close code to the browser", async () => {
    const t2 = await startTestServer();
    const minted = t2.cliToken("close");
    const d2 = await startWebServer({ serverUrl: t2.url, token: minted.token, port: 0, local: false, webDir });
    try {
      const ws = new WebSocket(`ws://127.0.0.1:${d2.port}/ws?s=${d2.secret}`, { origin: `http://localhost:${d2.port}` });
      ws.on("error", () => undefined);
      await nextMessage(ws, "register_ack");
      const done = closed(ws);
      await t2.close();
      expect((await done).code).toBe(1001);
    } finally {
      await d2.close();
    }
  });
});

describe("REST proxy", () => {
  it("proxies room, settings and device routes and passes upstream errors through", async () => {
    const room = await apiFetch("/api/room");
    expect(room.status).toBe(200);
    expect(await room.json()).toMatchObject({ roomId, settings: { mode: "ask", holdMs: 30000 } });

    const settings = await apiFetch("/api/room/settings", { method: "PUT", json: { holdMs: 20000 } });
    expect(await settings.json()).toEqual({ mode: "ask", holdMs: 20000, shareText: false, timeZone: "" });
    const badHold = await apiFetch("/api/room/settings", { method: "PUT", json: { holdMs: 5 } });
    expect(badHold.status).toBe(400);
    expect(await badHold.json()).toMatchObject({ code: "invalid_request", error: expect.any(String) });

    const device = t.deviceToken(roomId, "dv-0a0b0c", "desk");
    const list = (await (await apiFetch("/api/devices")).json()) as { devices: { id: string; online: boolean }[] };
    expect(list.devices.map((d) => d.id)).toContain(device.deviceId);

    const renamed = await apiFetch(`/api/devices/${device.deviceId}`, { method: "PATCH", json: { name: "kitchen" } });
    expect(await renamed.json()).toMatchObject({ id: device.deviceId, name: "kitchen" });
    expect((await apiFetch(`/api/devices/${device.deviceId}`, { method: "PATCH", json: {} })).status).toBe(400);

    const offline = await apiFetch(`/api/devices/${device.deviceId}/identify`, { method: "POST", json: {} });
    expect(offline.status).toBe(409);
    expect(await offline.json()).toMatchObject({ code: "conflict" });

    expect((await apiFetch("/api/devices/dv-ffffff", { method: "PATCH", json: { name: "x" } })).status).toBe(404);
    expect((await apiFetch(`/api/devices/${device.deviceId}`, { method: "DELETE" })).status).toBe(204);
    expect((await apiFetch(`/api/devices/${device.deviceId}`, { method: "DELETE" })).status).toBe(404);

    const pair = await apiFetch("/api/pair/confirm", { method: "POST", json: { code: "ABCDEF" } });
    expect(pair.status).toBe(404);
    expect(await pair.json()).toMatchObject({ code: "not_found" });

    expect((await apiFetch("/api/room/settings", { method: "PUT", body: "{}", headers: { "Content-Type": "text/plain" } })).status).toBe(400);
    expect((await apiFetch("/api/nothing")).status).toBe(404);
    expect((await apiFetch("/api/tokens", { method: "POST", json: {} })).status).toBe(404);
  });

  it("confirms a flow A pairing code", async () => {
    const start = await t.fetch("/api/device/pair/start", { method: "POST", json: { deviceId: "dv-123456" } });
    const { code } = (await start.json()) as { code: string };
    const res = await apiFetch("/api/pair/confirm", { method: "POST", json: { code: code.toLowerCase() } });
    expect(res.status).toBe(200);
    expect(await res.json()).toMatchObject({ deviceId: "dv-123456" });
  });

  it("proxies the pets routes, binary packs included", async () => {
    const list = await apiFetch("/api/pets");
    expect(((await list.json()) as { pets: { id: string }[] }).pets.map((p) => p.id)).toEqual(["devo", "ember", "mochi"]);
    const pack = await apiFetch("/api/pets/devo/pack");
    expect(pack.status).toBe(200);
    expect(pack.headers.get("content-type")).toBe("application/octet-stream");
    const bytes = new Uint8Array(await pack.arrayBuffer());
    expect(bytes.length).toBe(344);
    // A copy of Devo under another id and name, re-sealed.
    const copy = bytes.slice();
    copy.set(new TextEncoder().encode("viaproxy"), 8);
    const { crc32 } = await import("@devotchi/shared");
    const c = crc32(copy.subarray(0, copy.length - 4));
    copy.set([c, c >>> 8, c >>> 16, c >>> 24], copy.length - 4);
    const up = await apiFetch("/api/pets", { method: "POST", headers: { "Content-Type": "application/octet-stream" }, body: copy });
    expect(up.status).toBe(201);
    expect(await up.json()).toMatchObject({ id: "viaproxy", source: "room" });
    const tooBig = await apiFetch("/api/pets", { method: "POST", headers: { "Content-Type": "application/octet-stream" }, body: new Uint8Array(3000) });
    expect(tooBig.status).toBe(413);
    const bad = await apiFetch("/api/pets", { method: "POST", headers: { "Content-Type": "application/octet-stream" }, body: new Uint8Array(400) });
    expect(bad.status).toBe(400);
    expect(((await bad.json()) as { code: string }).code).toBe("invalid_request");
    expect((await apiFetch("/api/pets/viaproxy", { method: "DELETE" })).status).toBe(204);
    expect((await apiFetch("/api/pets/viaproxy/pack")).status).toBe(404);
    const offline = await apiFetch(`/api/devices/dv-ffffff/pet`, { method: "POST", json: { op: "select", pet: "devo" } });
    expect(offline.status).toBe(404);
    const malformed = await apiFetch(`/api/devices/dv-ffffff/pet`, { method: "POST", json: { op: "select", pet: "Devo" } });
    expect(malformed.status).toBe(400);
  });
});
