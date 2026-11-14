import type { IncomingMessage } from "node:http";
import { basename } from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { WebSocketServer, type WebSocket as ServerSocket } from "ws";
import { buildMood, buildStatus } from "@devotchi/shared";
import { consumer, startTestServer, type TestServer } from "../../server/test/helpers.js";
import { ConfigStore } from "../src/config.js";
import { ProducerConnection, type LinkState } from "../src/connection.js";
import { ServerCredentialSource, type CredentialSource } from "../src/credentials.js";
import { CLIENT_VERSION } from "../src/version.js";
import { makeSandbox, until, type Sandbox } from "./helpers.js";

let box: Sandbox;
let store: ConfigStore;
let t: TestServer | null = null;
const connections: ProducerConnection[] = [];

beforeEach(async () => {
  box = await makeSandbox();
  store = new ConfigStore(box.env);
});

afterEach(async () => {
  await Promise.all(connections.splice(0).map((c) => c.close()));
  await t?.close();
  t = null;
  await box.cleanup();
});

function connect(serverUrl: string, credentials?: CredentialSource): ProducerConnection {
  const connection = new ProducerConnection({
    serverUrl,
    credentials: credentials ?? new ServerCredentialSource(store, serverUrl),
    version: CLIENT_VERSION,
    minBackoffMs: 50,
    maxBackoffMs: 200,
  });
  connections.push(connection);
  connection.start();
  return connection;
}

function stateIs(connection: ProducerConnection, state: LinkState) {
  return until(() => connection.state === state, 5000, `state ${state} (now ${connection.state}, ${connection.lastError})`);
}

const fixed = (token: string): CredentialSource => ({
  get: async () => ({ token, roomId: "", source: "env" }),
  renew: async () => {
    throw new Error("renew not expected");
  },
});

/** A bare WebSocket server that records the upgrade and the frames it receives. */
async function fakeServer(onConnection: (socket: ServerSocket, req: IncomingMessage, frames: unknown[]) => void) {
  const wss = new WebSocketServer({ host: "127.0.0.1", port: 0 });
  await new Promise<void>((resolve) => wss.once("listening", () => resolve()));
  const port = (wss.address() as { port: number }).port;
  const seen: { req: IncomingMessage; frames: unknown[] }[] = [];
  wss.on("connection", (socket, req) => {
    const entry = { req, frames: [] as unknown[] };
    seen.push(entry);
    socket.on("message", (data) => entry.frames.push(JSON.parse(data.toString())));
    onConnection(socket, req, entry.frames);
  });
  return {
    url: `http://127.0.0.1:${port}`,
    seen,
    close: () =>
      new Promise<void>((resolve) => {
        for (const client of wss.clients) client.terminate();
        wss.close(() => resolve());
      }),
  };
}

describe("ProducerConnection", () => {
  it("connects to /ws with a Bearer header and registers as a v2 producer", async () => {
    const fake = await fakeServer(() => undefined);
    try {
      connect(fake.url, fixed("tok-123"));
      await until(() => fake.seen[0]?.frames.length === 1);
      const { req, frames } = fake.seen[0]!;
      expect(req.url).toBe("/ws");
      expect(req.headers.authorization).toBe("Bearer tok-123");
      expect(frames[0]).toEqual({
        type: "register",
        role: "producer",
        proto: 2,
        client: { kind: "mcp", version: CLIENT_VERSION, project: basename(process.cwd()) },
      });
      expect(CLIENT_VERSION).toMatch(/^\d+\.\d+\.\d+/);
    } finally {
      await fake.close();
    }
  });

  it("answers the server's ping with pong", async () => {
    const fake = await fakeServer((socket) => socket.send(JSON.stringify({ type: "ping" })));
    try {
      connect(fake.url, fixed("tok"));
      await until(() => fake.seen[0]?.frames.some((f) => (f as { type: string }).type === "pong") === true);
    } finally {
      await fake.close();
    }
  });

  it("registers with the real server, stays alive through its heartbeat and reports peers in send()", async () => {
    t = await startTestServer({ timing: { heartbeatIntervalMs: 100, heartbeatDeadMs: 400 } });
    const connection = connect(t.url);
    let registrations = 0;
    connection.on("registered", () => registrations++);
    await stateIs(connection, "registered");
    expect(connection.send(buildStatus({ msg: "alone" }))).toEqual({ sent: true, consumers: 0, devices: 0 });

    const creds = (await store.getCredentials(t.url))!;
    const sim = await consumer(t, creds.token);
    await until(() => connection.peers?.consumers === 1);
    expect(connection.send(buildStatus({ msg: "seen" }))).toEqual({ sent: true, consumers: 1, devices: 1 });
    expect((await sim.awaitMessage((m) => m.type === "status" && m.msg === "seen")).msg).toBe("seen");
    // Without pong replies the server would drop the socket after 400 ms of silence.
    await new Promise((resolve) => setTimeout(resolve, 1000));
    expect(connection.state).toBe("registered");
    expect(registrations).toBe(1);
    sim.close();
  });

  it("two producers of the same room coexist", async () => {
    t = await startTestServer();
    const a = connect(t.url);
    await stateIs(a, "registered");
    const b = connect(t.url);
    await stateIs(b, "registered");
    const sim = await consumer(t, (await store.getCredentials(t.url))!.token);
    a.send(buildStatus({ msg: "from a" }));
    b.send(buildStatus({ msg: "from b" }));
    await sim.awaitMessage((m) => m.msg === "from a");
    await sim.awaitMessage((m) => m.msg === "from b");
    await until(() => a.peers?.producers === 2 && b.peers?.producers === 2);
    expect(a.state).toBe("registered");
    expect(b.state).toBe("registered");
    sim.close();
  });

  it("resends the last status (without mood) and the last mood after a reconnect", async () => {
    t = await startTestServer({ keepDir: true });
    const { dir, server } = t;
    const port = server.port;
    const connection = connect(t.url);
    await stateIs(connection, "registered");
    connection.send(buildStatus({ msg: "building", mood: "focused" }));
    connection.send(buildMood({ mood: "anxious", reason: "long build" }));
    connection.send(buildStatus({ msg: "still building" }));

    await t.close();
    await until(() => connection.state !== "registered");
    t = await startTestServer({ dir, port });
    const sim = await consumer(t, (await store.getCredentials(t.url))!.token);
    const status = await sim.awaitMessage((m) => m.type === "status" && m.msg === "still building", 5000);
    expect(status).not.toHaveProperty("mood");
    const mood = await sim.awaitMessage((m) => m.type === "mood" && m.mood === "anxious", 5000);
    expect(mood.reason).toBe("long build");
    expect(connection.state).toBe("registered");
    sim.close();
  });

  it("re-provisions once after a 401 on the upgrade (single-user) and reconnects", async () => {
    t = await startTestServer();
    const bogus = "00000000-0000-4000-8000-000000000000";
    await store.setCredentials(t.url, { token: bogus, roomId: "000000000000" });
    const connection = connect(t.url);
    await stateIs(connection, "registered");
    const stored = await store.getCredentials(t.url);
    expect(stored?.token).not.toBe(bogus);
    expect(stored?.roomId).toMatch(/^[0-9a-f]{12}$/);
  });

  it("stops without reconnecting when the token is revoked (4003)", async () => {
    t = await startTestServer();
    const connection = connect(t.url);
    await stateIs(connection, "registered");
    const creds = (await store.getCredentials(t.url))!;
    const tokens = (await (await t.fetch("/api/tokens", { token: creds.token })).json()) as { tokens: { id: string }[] };
    const res = await t.fetch(`/api/tokens/${tokens.tokens[0]!.id}`, { method: "DELETE", token: creds.token });
    expect(res.status).toBe(204);
    await stateIs(connection, "stopped");
    expect(connection.lastError).toMatch(/revoked/);
    await new Promise((resolve) => setTimeout(resolve, 300));
    expect(connection.state).toBe("stopped");
    expect(connection.send(buildStatus({ msg: "x" })).sent).toBe(false);
  });

  it("stops on a protocol version mismatch (4002)", async () => {
    const fake = await fakeServer((socket) => socket.close(4002, "unsupported"));
    try {
      const connection = connect(fake.url, fixed("tok"));
      await stateIs(connection, "stopped");
      expect(connection.lastError).toMatch(/version mismatch/);
      await new Promise((resolve) => setTimeout(resolve, 300));
      expect(fake.seen).toHaveLength(1);
    } finally {
      await fake.close();
    }
  });

  it("keeps retrying with backoff while the server is unreachable", async () => {
    const fake = await fakeServer(() => undefined);
    const url = fake.url;
    await fake.close();
    const connection = connect(url, fixed("tok"));
    await stateIs(connection, "retrying");
    expect(connection.lastError).toMatch(/cannot connect|ECONNREFUSED/);
    expect(connection.send(buildStatus({ msg: "queued" }))).toEqual({ sent: false, consumers: 0, devices: 0 });
  });
});
