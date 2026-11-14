// Loads the built web UI in happy-dom against a real dashboard and server and
// answers a permission request with a click, as a person would.

import { existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { Browser, type BrowserPage } from "happy-dom";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { ALLOW } from "../../../server/test/hook-helpers.js";
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
    settings: { enableJavaScriptEvaluation: true, suppressInsecureJavaScriptEnvironmentWarning: true, disableCSSFileLoading: true },
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
    expect(card.querySelector(".risk-label")?.textContent).toBe("risk hint: low");
    expect(card.querySelector(".summary")?.textContent).toContain("ls -la");

    // The tab is visible, so the dashboard acked: the server shows it as a holder.
    await until(async () => (await world.t.fetch("/api/room", { token: world.token }).then((r) => r.json()) as { pending: { holders: string[] }[] }).pending[0]?.holders.includes("dashboard") === true, 3000, "dashboard holds the request");

    (card.querySelector("button.approve") as unknown as { click(): void }).click();
    expect(await reply).toEqual(ALLOW);
    await until(() => doc().querySelector(".card") === null, 3000, "card removed");
    expect(text("toasts")).toContain("approved, answered on the dashboard");
    expect(text("log-list")).toContain("ls -la");
  });
});
