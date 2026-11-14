// Multi-user mode (architecture 2.5, protocol 7.3): accounts, login
// sessions, room isolation and the /account page.

import { createHash } from "node:crypto";
import bcrypt from "bcryptjs";
import { afterEach, describe, expect, it } from "vitest";
import { TestSocket, UpgradeRejected, consumer, delay, startTestServer, type Json, type StartOptions, type TestServer } from "./helpers.js";
import { hold, permissionInput, postHook, roomState, until } from "./hook-helpers.js";

let ts: TestServer | undefined;
const sockets: TestSocket[] = [];

async function start(options: StartOptions = {}): Promise<TestServer> {
  ts = await startTestServer({ mode: "multi-user", bcryptCost: 4, ...options });
  return ts;
}

afterEach(async () => {
  for (const socket of sockets.splice(0)) socket.close();
  await ts?.close();
  ts = undefined;
});

async function post(t: TestServer, path: string, json: unknown, token?: string): Promise<{ status: number; body: Json }> {
  const res = await t.fetch(path, { method: "POST", json, token });
  const text = await res.text();
  return { status: res.status, body: text === "" ? {} : (JSON.parse(text) as Json) };
}

const PASSWORD = "correct horse battery";

const register = (t: TestServer, email: string, password = PASSWORD, extra: Json = {}) =>
  post(t, "/api/auth/register", { email, password, ...extra });
const login = (t: TestServer, email: string, password = PASSWORD) => post(t, "/api/auth/login", { email, password });

/** Registers and logs in; returns the session token and the user's room. */
async function account(t: TestServer, email: string): Promise<{ session: string; roomId: string; userId: string }> {
  expect((await register(t, email)).status).toBe(201);
  const { body } = await login(t, email);
  const me = (await (await t.fetch("/api/me", { token: body.sessionToken as string })).json()) as Json;
  return { session: body.sessionToken as string, roomId: me.roomId as string, userId: body.userId as string };
}

async function mint(t: TestServer, session: string): Promise<{ token: string; id: string; roomId: string }> {
  const res = await post(t, "/api/auth/token", { label: "cli" }, session);
  expect(res.status).toBe(201);
  return res.body as { token: string; id: string; roomId: string };
}

describe("single-user mode", () => {
  it("answers the account routes and page with 404", async () => {
    const t = await startTestServer();
    ts = t;
    for (const path of ["/api/auth/register", "/api/auth/login", "/api/auth/logout"]) {
      const res = await post(t, path, { email: "a@example.com", password: PASSWORD });
      expect(res.status, path).toBe(404);
      expect(res.body.code).toBe("not_found");
    }
    expect((await t.fetch("/account")).status).toBe(404);
  });

  it("rejects session tokens", async () => {
    const t = await startTestServer();
    ts = t;
    expect((await t.fetch("/api/me", { token: "dvs_" + "a".repeat(43) })).status).toBe(401);
  });
});

describe("account page", () => {
  it("is served in multi-user mode with a strict CSP that allows exactly its inline script", async () => {
    const t = await start();
    const res = await t.fetch("/account");
    expect(res.status).toBe(200);
    expect(res.headers.get("content-type")).toMatch(/^text\/html/);
    const html = await res.text();
    const csp = res.headers.get("content-security-policy") ?? "";
    expect(csp).toContain("default-src 'none'");
    expect(csp).toContain("frame-ancestors 'none'");
    expect(csp).toContain("connect-src 'self'");
    expect(csp).not.toContain("unsafe-inline");
    const script = /<script>([\s\S]*?)<\/script>/.exec(html)?.[1] ?? "";
    expect(script.length).toBeGreaterThan(100);
    expect(csp).toContain(`'sha256-${createHash("sha256").update(script).digest("base64")}'`);
    expect(html).not.toMatch(/\son[a-z]+=/i);
    expect(html).not.toContain("innerHTML");
  });
});

describe("register", () => {
  it("validates email and password", async () => {
    const t = await start({ timing: { rateWindowMs: 20 } });
    const cases: unknown[] = [
      {},
      { email: "a@example.com" },
      { password: PASSWORD },
      { email: 5, password: PASSWORD },
      { email: "a@example.com", password: 1234567890 },
      { email: "not-an-email", password: PASSWORD },
      { email: "a b@example.com", password: PASSWORD },
      { email: "a@example", password: PASSWORD },
      { email: `${"x".repeat(250)}@example.com`, password: PASSWORD },
      { email: "a@example.com", password: "123456789" },
      { email: "a@example.com", password: "x".repeat(73) },
      { email: "a@example.com", password: "é".repeat(37) },
      { email: "a@example.com", password: PASSWORD, inviteCode: 5 },
      [],
      "a@example.com",
    ];
    for (const json of cases) {
      const res = await post(t, "/api/auth/register", json);
      expect(res.status, JSON.stringify(json)).toBe(400);
      expect(res.body.code).toBe("invalid_request");
      await delay(5);
    }
    // 10 bytes made of five two-byte characters, and exactly 72 bytes.
    expect((await register(t, "short@example.com", "é".repeat(5))).status).toBe(201);
    await delay(5);
    expect((await register(t, "long@example.com", "x".repeat(72))).status).toBe(201);
  });

  it("normalises the email, returns a 16 hex user id and refuses duplicates", async () => {
    const t = await start();
    const created = await register(t, "  Alice@Example.COM ");
    expect(created).toEqual({ status: 201, body: { userId: expect.stringMatching(/^[0-9a-f]{16}$/) } });
    const row = t.server.internals.db.get<{ email: string; password_hash: string; room_id: string }>(
      "SELECT email, password_hash, room_id FROM users",
    );
    expect(row?.email).toBe("alice@example.com");
    expect(row?.password_hash).toMatch(/^\$2[aby]\$04\$/);
    expect(row?.room_id).toMatch(/^[0-9a-f]{12}$/);
    const duplicate = await register(t, "alice@example.com", "another password");
    expect(duplicate).toEqual({ status: 409, body: { error: expect.any(String), code: "conflict" } });
    expect((await login(t, "ALICE@example.com")).status).toBe(200);
  });

  it("uses bcrypt cost 12 by default", async () => {
    const t = await startTestServer({ mode: "multi-user" });
    ts = t;
    expect(t.server.internals.config.bcryptCost).toBe(12);
  });

  it("requires the invite code in invite mode", async () => {
    const t = await start({ registration: "invite", inviteCode: "join-us-please" });
    expect((await register(t, "a@example.com")).status).toBe(403);
    expect((await register(t, "a@example.com", PASSWORD, { inviteCode: "wrong" })).status).toBe(403);
    expect((await register(t, "a@example.com", PASSWORD, { inviteCode: "join-us-please!" })).status).toBe(403);
    const ok = await register(t, "a@example.com", PASSWORD, { inviteCode: "join-us-please" });
    expect(ok.status).toBe(201);
    const forbidden = await register(t, "b@example.com");
    expect(forbidden.body.code).toBe("forbidden");
  });

  it("refuses every registration when closed", async () => {
    const t = await start({ registration: "closed" });
    const res = await register(t, "a@example.com");
    expect(res).toEqual({ status: 403, body: { error: expect.any(String), code: "forbidden" } });
    expect(t.server.internals.db.get("SELECT COUNT(*) AS n FROM users")).toEqual({ n: 0 });
  });

  it("is limited to 5 per minute per IP", async () => {
    const t = await start();
    for (let i = 0; i < 5; i++) expect((await register(t, `u${i}@example.com`)).status).toBe(201);
    expect((await register(t, "u5@example.com")).status).toBe(429);
  });
});

describe("login", () => {
  it("returns an opaque session token valid for 30 days", async () => {
    const t = await start();
    const { body: created } = await register(t, "a@example.com");
    const before = Date.now();
    const res = await login(t, "a@example.com");
    expect(res.status).toBe(200);
    expect(res.body).toEqual({
      sessionToken: expect.stringMatching(/^dvs_[A-Za-z0-9_-]{43}$/),
      userId: created.userId,
      expiresAt: expect.stringMatching(/Z$/),
    });
    const ttl = Date.parse(res.body.expiresAt as string) - before;
    expect(Math.abs(ttl - 30 * 24 * 3600_000)).toBeLessThan(10_000);
    const stored = t.server.internals.db.get<{ token_hash: string }>("SELECT token_hash FROM sessions");
    expect(stored?.token_hash).toBe(createHash("sha256").update(res.body.sessionToken as string).digest("hex"));
    expect(t.logs.some((line) => line.includes(res.body.sessionToken as string))).toBe(false);
  });

  it("answers 401 for a wrong password and an unknown email alike", async () => {
    const t = await start();
    await register(t, "a@example.com");
    const wrong = await login(t, "a@example.com", "not the password");
    const unknown = await login(t, "nobody@example.com");
    const malformed = await login(t, "not-an-email");
    for (const res of [wrong, unknown, malformed]) {
      expect(res).toEqual({ status: 401, body: { error: "Wrong email or password", code: "unauthorized" } });
    }
    for (const json of [{}, { email: "a@example.com" }, { email: 1, password: PASSWORD }, { email: "a@example.com", password: null }]) {
      expect((await post(t, "/api/auth/login", json)).status).toBe(400);
    }
  });

  it("refuses a password longer than 72 bytes even when its first 72 bytes match", async () => {
    const t = await start();
    const password = "y".repeat(72);
    await register(t, "a@example.com", password);
    expect((await login(t, "a@example.com", password)).status).toBe(200);
    expect((await login(t, "a@example.com", password + "zzz")).status).toBe(401);
  });

  it("locks an email after 5 failures in 15 minutes, without affecting other emails", async () => {
    const t = await start();
    await register(t, "a@example.com");
    await register(t, "b@example.com");
    for (let i = 0; i < 5; i++) expect((await login(t, "a@example.com", "wrong password")).status).toBe(401);
    const locked = await login(t, "A@example.com");
    expect(locked.status).toBe(429);
    expect(locked.body.code).toBe("rate_limited");
    expect((await login(t, "b@example.com")).status).toBe(200);
  });

  it("is limited to 10 per minute per IP", async () => {
    const t = await start();
    for (let i = 0; i < 10; i++) expect((await login(t, `n${i}@example.com`)).status).toBe(401);
    expect((await login(t, "n10@example.com")).status).toBe(429);
  });

  it("costs one bcrypt compare for unknown and known emails", async () => {
    const cost = 10;
    const t = await start({ bcryptCost: cost });
    await register(t, "a@example.com");
    await register(t, "b@example.com");
    // The dummy hash is ready once the first unknown-email login returned.
    await login(t, "warmup@example.com");
    const time = async (email: string) => {
      const begin = performance.now();
      expect((await login(t, email, "wrong password")).status).toBe(401);
      return performance.now() - begin;
    };
    const known: number[] = [];
    const unknown: number[] = [];
    for (let i = 0; i < 4; i++) {
      known.push(await time(i % 2 === 0 ? "a@example.com" : "b@example.com"));
      unknown.push(await time(`ghost${i}@example.com`));
    }
    const median = (xs: number[]) => [...xs].sort((a, b) => a - b)[Math.floor(xs.length / 2)];
    const hash = await bcrypt.hash("x", cost);
    const begin = performance.now();
    await bcrypt.compare("y", hash);
    const compareMs = performance.now() - begin;
    expect(median(unknown)).toBeGreaterThan(compareMs * 0.5);
    expect(median(known)).toBeGreaterThan(compareMs * 0.5);
    const ratio = median(unknown) / median(known);
    expect(ratio).toBeGreaterThan(0.5);
    expect(ratio).toBeLessThan(2);
  });
});

describe("sessions", () => {
  it("authenticate REST routes in the user's room", async () => {
    const t = await start();
    const { session, roomId, userId } = await account(t, "a@example.com");
    const me = await t.fetch("/api/me", { token: session });
    expect(await me.json()).toEqual({ kind: "session", userId, email: "a@example.com", roomId, serverMode: "multi-user" });
    for (const path of ["/api/room", "/api/devices", "/api/tokens"]) {
      expect((await t.fetch(path, { token: session })).status, path).toBe(200);
    }
    expect((await post(t, "/api/notify", { event: "tests_passed" }, session)).status).toBe(202);
    const hook = await postHook(t, session, permissionInput());
    expect(hook.status).toBe(200);
    expect(await hook.json()).toEqual({});
    expect((await t.fetch(`/hook/event?token=${session}`, { method: "POST", json: permissionInput() })).status).toBe(401);
  });

  it("are refused on the WebSocket", async () => {
    const t = await start();
    const { session } = await account(t, "a@example.com");
    const err = await TestSocket.connect(t.wsUrl, { token: session }).catch((e: unknown) => e);
    expect(err).toBeInstanceOf(UpgradeRejected);
    expect((err as UpgradeRejected).status).toBe(401);
  });

  it("are required to mint tokens; minted tokens belong to the user's room", async () => {
    const t = await start();
    expect((await post(t, "/api/auth/token", {})).status).toBe(401);
    const { session, roomId, userId } = await account(t, "a@example.com");
    const minted = await mint(t, session);
    expect(minted.roomId).toBe(roomId);
    expect(t.server.internals.db.get("SELECT kind, user_id FROM tokens WHERE id = ?", minted.id)).toEqual({ kind: "cli", user_id: userId });
    // A cli token is not a session.
    expect((await post(t, "/api/auth/token", {}, minted.token)).status).toBe(401);
    const me = await (await t.fetch("/api/me", { token: minted.token })).json();
    expect(me).toMatchObject({ kind: "token", roomId, serverMode: "multi-user" });
    const p = await consumer(t, minted.token, { kind: "sim", deviceId: "sim-a" });
    sockets.push(p);
  });

  it("end with logout", async () => {
    const t = await start();
    const { session } = await account(t, "a@example.com");
    const minted = await mint(t, session);
    expect((await post(t, "/api/auth/logout", {}, minted.token)).status).toBe(401);
    expect((await post(t, "/api/auth/logout", {}, session)).status).toBe(204);
    expect((await t.fetch("/api/me", { token: session })).status).toBe(401);
    expect((await post(t, "/api/auth/logout", {}, session)).status).toBe(401);
    // Tokens minted with the session keep working.
    expect((await t.fetch("/api/me", { token: minted.token })).status).toBe(200);
  });

  it("expire and are pruned", async () => {
    const t = await start({ timing: { loginSessionTtlMs: 150 } });
    const { session } = await account(t, "a@example.com");
    expect((await t.fetch("/api/me", { token: session })).status).toBe(200);
    await delay(200);
    expect((await t.fetch("/api/me", { token: session })).status).toBe(401);
    t.server.internals.accounts?.prune();
    expect(t.server.internals.db.get("SELECT COUNT(*) AS n FROM sessions")).toEqual({ n: 0 });
  });
});

describe("room isolation", () => {
  it("keeps devices, pending requests, settings and tokens apart", async () => {
    const t = await start({ timing: { ackMs: 5000, aliveMs: 5000, holdMs: 8000 } });
    const a = await account(t, "a@example.com");
    const b = await account(t, "b@example.com");
    expect(a.roomId).not.toBe(b.roomId);
    const aToken = await mint(t, a.session);

    // A pairs a device with flow B through the session.
    const { body: code } = await post(t, "/api/pair", {}, a.session);
    const claimed = await post(t, "/api/pair/claim", { code: code.code, deviceId: "dv-e356e4" });
    expect(claimed.body.roomId).toBe(a.roomId);
    const d = await consumer(t, claimed.body.token as string, { kind: "device", deviceId: "dv-e356e4" });
    sockets.push(d);

    const listA = await (await t.fetch("/api/devices", { token: a.session })).json();
    expect(listA.devices.map((x: { id: string }) => x.id)).toEqual(["dv-e356e4"]);
    expect((await (await t.fetch("/api/devices", { token: b.session })).json()).devices).toEqual([]);
    expect((await t.fetch("/api/devices/dv-e356e4", { method: "PATCH", json: { name: "x" }, token: b.session })).status).toBe(404);
    expect((await post(t, "/api/devices/dv-e356e4/identify", {}, b.session)).status).toBe(404);
    expect((await t.fetch("/api/devices/dv-e356e4", { method: "DELETE", token: b.session })).status).toBe(404);
    expect(d.isOpen).toBe(true);

    const reply = postHook(t, aToken.token, permissionInput());
    const permission = await d.awaitMessage("permission");
    hold(d, permission.id as string);
    await until(async () => (await roomState(t, a.session)).pending.length === 1);
    expect((await roomState(t, b.session)).pending).toEqual([]);
    expect((await roomState(t, b.session)).devices).toEqual([]);
    d.send({ type: "permission_response", id: permission.id, granted: true });
    expect((await reply).status).toBe(200);

    const put = await t.fetch("/api/room/settings", { method: "PUT", json: { holdMs: 60_000, mode: "desk" }, token: a.session });
    expect(await put.json()).toEqual({ mode: "desk", holdMs: 60_000, shareText: false });
    expect((await roomState(t, b.session)).settings).toEqual({ mode: "ask", holdMs: 8000, shareText: false });

    const tokensB = await (await t.fetch("/api/tokens", { token: b.session })).json();
    expect(tokensB.tokens).toEqual([]);
    expect((await t.fetch(`/api/tokens/${aToken.id}`, { method: "DELETE", token: b.session })).status).toBe(404);
    expect((await t.fetch(`/api/tokens/${aToken.id}`, { method: "DELETE", token: a.session })).status).toBe(204);
  });

  it("lets a user confirm a flow A pairing into their own room", async () => {
    const t = await start();
    const a = await account(t, "a@example.com");
    const started = await post(t, "/api/device/pair/start", { deviceId: "dv-e356e4" });
    const confirmed = await post(t, "/api/pair/confirm", { code: started.body.code, name: "desk" }, a.session);
    expect(confirmed.body).toEqual({ deviceId: "dv-e356e4", name: "desk" });
    const delivered = await post(t, "/api/device/pair/poll", { deviceSecret: started.body.deviceSecret });
    expect(delivered.body).toMatchObject({ roomId: a.roomId, deviceId: "dv-e356e4", name: "desk" });
  });
});
