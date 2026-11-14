import { request as httpRequest } from "node:http";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { StartupError, createServer } from "../src/app.js";
import { TestSocket, consumer, delay, producer, startTestServer, type StartOptions, type TestServer } from "./helpers.js";

let ts: TestServer | undefined;
const sockets: TestSocket[] = [];

async function start(options: StartOptions = {}): Promise<TestServer> {
  ts = await startTestServer(options);
  return ts;
}

afterEach(async () => {
  for (const socket of sockets.splice(0)) socket.close();
  await ts?.close();
  ts = undefined;
});

const UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;

/** Raw request, for bodies and headers fetch will not send. */
function raw(
  url: string,
  options: { method: string; path: string; headers?: Record<string, string>; body?: string | Buffer },
): Promise<{ status: number; headers: Record<string, string | string[] | undefined>; body: string }> {
  return new Promise((resolve, reject) => {
    const req = httpRequest(url + options.path, { method: options.method, headers: options.headers }, (res) => {
      const chunks: Buffer[] = [];
      res.on("data", (c: Buffer) => chunks.push(c));
      res.on("end", () => resolve({ status: res.statusCode ?? 0, headers: res.headers, body: Buffer.concat(chunks).toString() }));
    });
    req.on("error", reject);
    req.end(options.body);
  });
}

describe("health", () => {
  it("answers /health and / with proto 2", async () => {
    const t = await start();
    for (const path of ["/health", "/"]) {
      const res = await t.fetch(path);
      expect(res.status).toBe(200);
      expect(await res.json()).toEqual({
        status: "ok",
        version: expect.any(String),
        proto: 2,
        mode: "single-user",
        uptimeS: expect.any(Number),
      });
    }
  });

  it("sets security headers and no-store", async () => {
    const t = await start();
    const res = await t.fetch("/health");
    expect(res.headers.get("x-content-type-options")).toBe("nosniff");
    expect(res.headers.get("cache-control")).toBe("no-store");
  });

  it("answers unknown routes with a JSON 404", async () => {
    const t = await start();
    const res = await t.fetch("/nope");
    expect(res.status).toBe(404);
    expect(await res.json()).toEqual({ error: "Not found", code: "not_found" });
    const wrongMethod = await t.fetch("/health", { method: "DELETE" });
    expect(wrongMethod.status).toBe(404);
    expect((await wrongMethod.json()).code).toBe("not_found");
  });
});

describe("tokens", () => {
  it("mints a cli token in a new room each time", async () => {
    const t = await start();
    const res = await t.fetch("/api/auth/token", { method: "POST", json: { label: "laptop" } });
    expect(res.status).toBe(201);
    const a = await res.json();
    expect(a.token).toMatch(UUID_RE);
    expect(a.id).toMatch(/^[0-9a-f]{8}$/);
    expect(a.roomId).toMatch(/^[0-9a-f]{12}$/);
    const b = await t.mintToken();
    expect(b.roomId).not.toBe(a.roomId);
    const empty = await t.fetch("/api/auth/token", { method: "POST" });
    expect(empty.status).toBe(201);
  });

  it("rate limits minting to 10 per minute per IP with Retry-After", async () => {
    const t = await start();
    for (let i = 0; i < 10; i++) await t.mintToken();
    const res = await t.fetch("/api/auth/token", { method: "POST", json: {} });
    expect(res.status).toBe(429);
    expect(await res.json()).toEqual({ error: expect.any(String), code: "rate_limited" });
    expect(Number(res.headers.get("retry-after"))).toBeGreaterThanOrEqual(1);
  });

  it("requires X-Devotchi-Admin when an admin secret is configured", async () => {
    const t = await start({ adminSecret: "s3cret-value" });
    expect((await t.fetch("/api/auth/token", { method: "POST", json: {} })).status).toBe(401);
    const wrong = await t.fetch("/api/auth/token", { method: "POST", json: {}, headers: { "X-Devotchi-Admin": "nope" } });
    expect(wrong.status).toBe(401);
    const ok = await t.fetch("/api/auth/token", { method: "POST", json: {}, headers: { "X-Devotchi-Admin": "s3cret-value" } });
    expect(ok.status).toBe(201);
  });

  it("requires a session to mint in multi-user mode", async () => {
    const t = await start({ mode: "multi-user" });
    expect((await t.fetch("/api/auth/token", { method: "POST", json: {} })).status).toBe(401);
    const session = await t.fetch("/api/auth/token", { method: "POST", json: {}, token: "dvs_" + "a".repeat(43) });
    expect(session.status).toBe(401);
    expect((await session.json()).code).toBe("unauthorized");
  });

  it("validates the label", async () => {
    const t = await start();
    for (const json of [{ label: { a: 1 } }, { label: 7 }, { label: "x".repeat(65) }, [1, 2], "label"]) {
      const res = await t.fetch("/api/auth/token", { method: "POST", json });
      expect(res.status).toBe(400);
      expect((await res.json()).code).toBe("invalid_request");
    }
    expect((await t.fetch("/api/auth/token", { method: "POST", json: { label: "x".repeat(64) } })).status).toBe(201);
  });

  it("answers /api/me for a cli token", async () => {
    const t = await start();
    const minted = await t.mintToken("cli");
    const res = await t.fetch("/api/me", { token: minted.token });
    expect(res.status).toBe(200);
    expect(await res.json()).toEqual({
      kind: "token",
      roomId: minted.roomId,
      tokenId: minted.id,
      label: "cli",
      serverMode: "single-user",
    });
    expect((await t.fetch("/api/me")).status).toBe(401);
    const bad = await t.fetch("/api/me", { token: "00000000-0000-4000-8000-000000000000" });
    expect(bad.status).toBe(401);
    expect(await bad.json()).toEqual({ error: "Invalid token", code: "unauthorized" });
    const query = await t.fetch(`/api/me?token=${minted.token}`);
    expect(query.status).toBe(401);
  });

  it("rejects device tokens on REST routes with 403", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const device = t.deviceToken(cli.roomId);
    for (const path of ["/api/me", "/api/tokens", "/api/devices"]) {
      const res = await t.fetch(path, { token: device.token });
      expect(res.status).toBe(403);
      expect((await res.json()).code).toBe("forbidden");
    }
    const hook = await t.fetch("/hook/status", { method: "POST", json: {}, token: device.token });
    expect(hook.status).toBe(403);
  });

  it("lists the room's tokens", async () => {
    const t = await start();
    const cli = await t.mintToken("laptop");
    t.deviceToken(cli.roomId);
    await t.mintToken("other-room");
    const res = await t.fetch("/api/tokens", { token: cli.token });
    const { tokens } = await res.json();
    expect(tokens).toHaveLength(2);
    expect(tokens).toContainEqual({ id: cli.id, label: "laptop", kind: "cli", createdAt: expect.any(String), lastUsed: expect.any(String) });
    expect(tokens.map((x: { kind: string }) => x.kind).sort()).toEqual(["cli", "device"]);
  });

  it("revokes a token: 204, its sockets close with 4003, and it stops working", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const device = t.deviceToken(cli.roomId, "dv-00aa11");
    const sock = await consumer(t, device.token, { kind: "device", deviceId: "dv-00aa11" });
    sockets.push(sock);
    const res = await t.fetch(`/api/tokens/${device.id}`, { method: "DELETE", token: cli.token });
    expect(res.status).toBe(204);
    expect((await sock.closed).code).toBe(4003);
    const again = await TestSocket.connect(t.wsUrl, { token: device.token }).catch((e: { status: number }) => e);
    expect((again as { status: number }).status).toBe(401);
    expect(t.server.internals.db.get("SELECT 1 FROM devices WHERE id = 'dv-00aa11'")).toBeUndefined();
    expect((await t.fetch(`/api/tokens/${device.id}`, { method: "DELETE", token: cli.token })).status).toBe(404);
  });

  it("does not revoke a token of another room", async () => {
    const t = await start();
    const a = await t.mintToken();
    const b = await t.mintToken();
    expect((await t.fetch(`/api/tokens/${b.id}`, { method: "DELETE", token: a.token })).status).toBe(404);
    expect((await t.fetch("/api/me", { token: b.token })).status).toBe(200);
  });

  it("writes last_used at most once per interval", async () => {
    const t = await start({ timing: { lastUsedWriteMs: 60_000 } });
    const cli = await t.mintToken();
    await t.fetch("/api/me", { token: cli.token });
    const first = t.server.internals.db.get<{ last_used: string }>("SELECT last_used FROM tokens WHERE id = ?", cli.id);
    await delay(10);
    await t.fetch("/api/me", { token: cli.token });
    const second = t.server.internals.db.get<{ last_used: string }>("SELECT last_used FROM tokens WHERE id = ?", cli.id);
    expect(first?.last_used).toMatch(/Z$/);
    expect(second?.last_used).toBe(first?.last_used);
  });
});

describe("request hardening", () => {
  it("answers bodies over 16 KB on /api with 413", async () => {
    const t = await start();
    const res = await t.fetch("/api/auth/token", { method: "POST", json: { label: "x".repeat(17 * 1024) } });
    expect(res.status).toBe(413);
    expect((await res.json()).code).toBe("payload_too_large");
  });

  it("accepts large hook bodies up to 2 MB and rejects larger ones", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const big = { tool_name: "Write", tool_input: { content: "x".repeat(1024 * 1024) } };
    expect((await t.fetch("/hook/status", { method: "POST", json: big, token: cli.token })).status).toBe(200);
    const huge = { tool_name: "Write", tool_input: { content: "x".repeat(2 * 1024 * 1024 + 10) } };
    const res = await t.fetch("/hook/status", { method: "POST", json: huge, token: cli.token });
    expect(res.status).toBe(413);
  });

  it("rejects a chunked body over the limit", async () => {
    const t = await start();
    const res = await raw(t.url, {
      method: "POST",
      path: "/api/auth/token",
      headers: { "Content-Type": "application/json", "Transfer-Encoding": "chunked" },
      body: JSON.stringify({ label: "y".repeat(20 * 1024) }),
    });
    expect(res.status).toBe(413);
  });

  it("finishes a request whose client disconnects in the middle of the body", async () => {
    const t = await start();
    const req = httpRequest(t.url + "/api/auth/token", {
      method: "POST",
      headers: { "Content-Type": "application/json", "Content-Length": "1000" },
    });
    req.on("error", () => undefined);
    req.write('{"label":"');
    await delay(50);
    req.destroy();
    const deadline = Date.now() + 2000;
    const finished = () =>
      t.logs.map((l) => JSON.parse(l) as Record<string, unknown>).find((l) => l.msg === "request" && l.path === "/api/auth/token");
    while (!finished() && Date.now() < deadline) await delay(10);
    expect(finished()).toMatchObject({ method: "POST", status: 400 });
    expect(t.logs.some((l) => l.includes('"unhandled error"'))).toBe(false);
  });

  it("requires Content-Type application/json for bodies and valid JSON", async () => {
    const t = await start();
    const plain = await t.fetch("/api/auth/token", { method: "POST", body: "{}", headers: { "Content-Type": "text/plain" } });
    expect(plain.status).toBe(400);
    expect((await plain.json()).code).toBe("invalid_request");
    const broken = await t.fetch("/api/auth/token", { method: "POST", body: "{nope", headers: { "Content-Type": "application/json" } });
    expect(broken.status).toBe(400);
  });

  it("rejects duplicated query parameters with 400, not 500", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const res = await t.fetch(`/hook/status?token=${cli.token}&token=${cli.token}`, { method: "POST", json: {} });
    expect(res.status).toBe(400);
    expect((await res.json()).code).toBe("invalid_request");
    expect((await t.fetch("/health?a=1&a=2")).status).toBe(400);
  });

  it("accepts ?token= on hook routes", async () => {
    const t = await start();
    const cli = await t.mintToken();
    const res = await t.fetch(`/hook/status?token=${cli.token}`, { method: "POST", json: { tool_name: "Bash" } });
    expect(res.status).toBe(200);
    expect(await res.json()).toEqual({});
  });

  it("sends no CORS headers by default and only to listed origins", async () => {
    const closed = await start();
    const res = await closed.fetch("/health", { headers: { Origin: "https://evil.example" } });
    expect(res.headers.get("access-control-allow-origin")).toBeNull();
    await closed.close();

    const t = await start({ corsOrigins: ["https://app.example"] });
    const allowed = await t.fetch("/health", { headers: { Origin: "https://app.example" } });
    expect(allowed.headers.get("access-control-allow-origin")).toBe("https://app.example");
    const other = await t.fetch("/health", { headers: { Origin: "https://evil.example" } });
    expect(other.headers.get("access-control-allow-origin")).toBeNull();
    const preflight = await t.fetch("/api/me", {
      method: "OPTIONS",
      headers: { Origin: "https://app.example", "Access-Control-Request-Method": "GET" },
    });
    expect(preflight.status).toBe(204);
    expect(preflight.headers.get("access-control-allow-headers")).toContain("Authorization");
  });

  it("logs requests as JSON lines without tokens or query strings", async () => {
    const t = await start();
    const cli = await t.mintToken();
    await t.fetch(`/hook/status?token=${cli.token}`, { method: "POST", json: { tool_name: "Bash" } });
    const p = await producer(t, cli.token);
    sockets.push(p);
    p.send({ type: "status", msg: "secret message text" });
    await delay(50);
    const lines = t.logs.map((line) => JSON.parse(line) as Record<string, unknown>);
    const hook = lines.find((l) => l.msg === "request" && l.path === "/hook/status");
    expect(hook).toMatchObject({ level: "info", method: "POST", status: 200, ms: expect.any(Number) });
    const all = t.logs.join("\n");
    expect(all).not.toContain(cli.token);
    expect(all).not.toContain("secret message text");
  });

  it("returns 500 internal without a stack for unexpected errors and logs the stack", async () => {
    const t = await start();
    const cli = await t.mintToken();
    t.server.internals.db.close();
    const res = await t.fetch("/api/tokens", { token: cli.token });
    expect(res.status).toBe(500);
    expect(await res.json()).toEqual({ error: "Internal server error", code: "internal" });
    const logged = t.logs.map((l) => JSON.parse(l) as Record<string, unknown>).find((l) => l.msg === "unhandled error");
    expect(logged?.stack).toEqual(expect.any(String));
  });
});

describe("lifecycle", () => {
  it("reports a port in use as a one-line StartupError and releases the database", async () => {
    const t = await start();
    const err = await createServer({ dbPath: join(t.dir, "second.db"), port: t.server.port, logSink: () => undefined }).catch(
      (e: unknown) => e,
    );
    expect(err).toBeInstanceOf(StartupError);
    expect((err as Error).message).toBe(`port ${t.server.port} on 127.0.0.1 is already in use`);
  });

  it("runs shutdown hooks before sockets close, and close() is idempotent", async () => {
    const t = await start();
    const { token } = await t.mintToken();
    const p = await producer(t, token);
    const order: string[] = [];
    t.server.internals.onShutdown(() => {
      order.push(p.isOpen ? "hook:open" : "hook:closed");
    });
    await Promise.all([t.server.close(), t.server.close()]);
    await p.closed;
    order.push("closed");
    expect(order).toEqual(["hook:open", "closed"]);
    expect(t.server.internals.db.isOpen).toBe(false);
    await t.close();
    ts = undefined;
  });
});
