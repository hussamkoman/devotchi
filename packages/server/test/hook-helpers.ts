// Helpers for the hook and permission tests: hook request bodies, a consumer
// that acknowledges requests, and room inspection.

import { createHash } from "node:crypto";
import { HoldingAckScheduler, buildHolding, type Room } from "@devotchi/shared";
import { delay, type Json, type TestServer, type TestSocket } from "./helpers.js";

export const ALLOW = { hookSpecificOutput: { hookEventName: "PermissionRequest", decision: { behavior: "allow" } } };
export const DENY = {
  hookSpecificOutput: {
    hookEventName: "PermissionRequest",
    decision: { behavior: "deny", message: "Denied on Devotchi. Do not retry this call; try another way or ask the user." },
  },
};

export function sidOf(sessionId: string): string {
  return createHash("sha256").update(sessionId).digest("hex").slice(0, 6);
}

/** A hook input with the common fields; `fields` overrides or adds. */
export function hookInput(event: string, fields: Json = {}): Json {
  return { hook_event_name: event, session_id: "session-1", cwd: "/home/dev/devotchi", ...fields };
}

export function permissionInput(fields: Json = {}): Json {
  return hookInput("PermissionRequest", { tool_name: "Bash", tool_input: { command: "rm -rf dist" }, ...fields });
}

export function postHook(t: TestServer, token: string, body: unknown, init: RequestInit = {}): Promise<Response> {
  return t.fetch("/hook/event", { method: "POST", json: body, token, ...init });
}

/** Sends a hook and returns its JSON reply; fails unless the status is 200. */
export async function hook(t: TestServer, token: string, body: unknown): Promise<unknown> {
  const res = await postHook(t, token, body);
  if (res.status !== 200) throw new Error(`hook answered ${res.status}: ${await res.text()}`);
  return res.json();
}

export async function roomState(t: TestServer, token: string): Promise<Room> {
  const res = await t.fetch("/api/room", { token });
  if (res.status !== 200) throw new Error(`/api/room answered ${res.status}`);
  return (await res.json()) as Room;
}

export interface Held {
  id: string;
  permission: Json;
  /** The hook's reply body. */
  reply: Promise<unknown>;
}

/** Starts a PermissionRequest hook and waits until the consumer received the request. */
export async function requestPermission(t: TestServer, token: string, c: TestSocket, fields: Json = {}): Promise<Held> {
  const reply = hook(t, token, permissionInput(fields));
  const permission = await c.awaitMessage("permission");
  return { id: permission.id as string, permission, reply };
}

export function hold(c: TestSocket, ...ids: string[]): void {
  c.send({ type: "holding", ids });
}

/** Keeps sending `holding` for the ids until stopped. */
export function heartbeat(c: TestSocket, ids: string[], everyMs: number): () => void {
  hold(c, ...ids);
  const timer = setInterval(() => hold(c, ...ids), everyMs);
  return () => clearInterval(timer);
}

/** Acknowledges every `permission` the way a real consumer does (protocol 4.7), with the shared scheduler. */
export function followAckRule(c: TestSocket): { frames: string[][]; stop(): void } {
  const scheduler = new HoldingAckScheduler();
  const frames: string[][] = [];
  let timer: NodeJS.Timeout | undefined;
  const arm = () => {
    clearTimeout(timer);
    const due = scheduler.nextDueAt();
    if (due === null) return;
    timer = setTimeout(() => {
      const ids = scheduler.onTick(Date.now());
      if (ids) {
        frames.push(ids);
        c.send(buildHolding(ids) as unknown as Json);
      }
      arm();
    }, Math.max(0, due - Date.now()));
  };
  c.ws.on("message", (data) => {
    const msg = JSON.parse(data.toString()) as Json;
    if (msg.type === "permission") scheduler.onPermission(msg.id as string, Date.now());
    else if (msg.type === "dismiss") scheduler.onRemove(msg.id as string);
    else return;
    arm();
  });
  return { frames, stop: () => clearTimeout(timer) };
}

/** Polls until the predicate holds. */
export async function until(predicate: () => boolean | Promise<boolean>, timeoutMs = 2000): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  while (!(await predicate())) {
    if (Date.now() > deadline) throw new Error("condition not reached in time");
    await delay(10);
  }
}
