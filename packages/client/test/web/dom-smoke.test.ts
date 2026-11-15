// Loads the built web UI in happy-dom against a real dashboard and server and
// answers a permission request with a click, as a person would.

import { existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { Browser, type BrowserPage } from "happy-dom";
import { canonicalTimeZone, type Room } from "@devotchi/shared";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { ALLOW, hookInput } from "../../../server/test/hook-helpers.js";
import { makeWorld, type World } from "../e2e/helpers.js";
import { until } from "../helpers.js";
import { startWebServer, type Dashboard } from "../../src/web-server.js";

const WEB_DIR = fileURLToPath(new URL("../../dist/web", import.meta.url));

let world: World;
let dash: Dashboard;
let browser: Browser;
let page: BrowserPage;

beforeAll(async () => {
  if (!existsSync(`${WEB_DIR}/index.html`)) throw new Error("build the client first: pnpm run build");
  world = await makeWorld();
  dash = await startWebServer({ serverUrl: world.t.url, token: world.token, port: 0, local: false, webDir: WEB_DIR });
  browser = new Browser({
    settings: {
      enableJavaScriptEvaluation: true,
      suppressInsecureJavaScriptEnvironmentWarning: true,
      disableCSSFileLoading: true,
      // A browser sends Origin on a same-origin PUT/POST/DELETE; happy-dom does not, and the dashboard requires it.
      fetch: {
        interceptor: {
          beforeAsyncRequest: async ({ request, window }) => {
            if (request.method !== "GET" && request.method !== "HEAD") request.headers.set("Origin", window.location.origin);
          },
        },
      },
    },
  });
  page = browser.newPage();
  await page.goto(`http://127.0.0.1:${dash.port}/`);
});

afterAll(async () => {
  // Drop the socket first and let the page settle, so no handler runs after the document is gone.
  await dash?.close();
  if (page !== undefined) await until(() => text("connection-text").startsWith("Reconnecting"), 3000, "page saw the close");
  await browser?.close();
  await world?.close();
});

const doc = () => page.mainFrame.document;
const text = (id: string) => doc().getElementById(id)?.textContent ?? "";

describe("web UI in a DOM", () => {
  it("shows a request, approves it with a click and removes the card on dismiss", async () => {
    await until(() => text("connection-text") === "Connected", 5000, "dashboard connected");
    expect(doc().title).toBe("Devotchi");

    const reply = world.permission({ tool_input: { command: "ls -la" } });
    await until(() => doc().querySelector(".card") !== null, 5000, "request card");
    const card = doc().querySelector(".card")!;
    expect(card.querySelector(".chip")?.textContent).toBe("Bash");
    expect(card.querySelector(".risk-label")?.textContent).toBe("LOW RISK");
    expect(card.querySelector(".cmd")?.textContent).toBe("ls -la");

    // The tab is visible, so the dashboard acked: the server shows it as a holder.
    await until(async () => (await world.t.fetch("/api/room", { token: world.token }).then((r) => r.json()) as { pending: { holders: string[] }[] }).pending[0]?.holders.includes("dashboard") === true, 3000, "dashboard holds the request");

    (card.querySelector("button.approve") as unknown as { click(): void }).click();
    expect(await reply).toEqual(ALLOW);
    await until(() => doc().querySelector(".card") === null, 3000, "card removed");
    expect(text("toasts")).toContain("allowed, answered on the dashboard");
    expect(text("log-list")).toContain("ls -la");
  });

  it("answers a question with clicks: the hook gets the exact reply of protocol 14.4", async () => {
    const questions = [
      { question: "Which framework?", header: "Framework", options: [{ label: "React", description: "Component library" }, { label: "Vue" }], multiSelect: false },
      { question: "Which extras?", header: "Extras", options: [{ label: "Router" }, { label: "Store" }, { label: "Tests" }], multiSelect: true },
      { question: "Where?", options: [{ label: "Here" }, { label: "There" }] },
    ];
    const toolInput = { questions, metadata: { source: "test" } };
    const reply = world.hook(hookInput("PermissionRequest", { tool_name: "AskUserQuestion", tool_input: toolInput }));
    await until(() => doc().querySelector('.card[data-kind="question"]') !== null, 5000, "question card");
    const card = doc().querySelector('.card[data-kind="question"]')!;
    // The full description comes from GET /api/room/requests/:id through the proxy.
    await until(() => card.querySelector(".option-desc")?.textContent === "Component library", 3000, "full text");
    const inputs = [...card.querySelectorAll("fieldset")].map((f) => [...f.querySelectorAll("input")] as unknown as { click(): void }[]);
    inputs[0]![1]!.click();
    inputs[1]![0]!.click();
    inputs[1]![2]!.click();
    inputs[2]![2]!.click();
    const send = [...card.querySelectorAll("button")].find((b) => b.textContent === "Send") as unknown as { disabled: boolean; click(): void };
    expect(send.disabled).toBe(false);
    send.click();
    expect(await reply).toEqual({
      hookSpecificOutput: {
        hookEventName: "PermissionRequest",
        decision: {
          behavior: "allow",
          updatedInput: {
            questions,
            metadata: { source: "test" },
            answers: { "Which framework?": "Vue", "Which extras?": "Router, Tests", "Where?": "No preference, use your judgment" },
          },
        },
      },
    });
    await until(() => doc().querySelector(".card") === null, 3000, "card removed");
    expect(text("toasts")).toContain("Question: answers sent: Vue; Router, Tests; no preference, answered on the dashboard");
  });

  it("gives a room without a time zone the browser's zone (protocol 14.9)", async () => {
    // The world's room starts with timeZone ""; the page adopts its zone once loaded.
    const zone = canonicalTimeZone(new Intl.DateTimeFormat().resolvedOptions().timeZone);
    expect(zone).not.toBeNull();
    const room = async () => (await world.t.fetch("/api/room", { token: world.token }).then((r) => r.json())) as Room;
    await until(async () => (await room()).settings.timeZone === zone, 5000, "room took the browser's time zone");
  });
});
