import { existsSync } from "node:fs";
import net from "node:net";
import { fileURLToPath } from "node:url";
import { afterEach, describe, expect, it, vi } from "vitest";

afterEach(() => {
  vi.restoreAllMocks();
});

describe("package entry", () => {
  it("exports createServer from the built package and opens no socket on import", async () => {
    const built = fileURLToPath(new URL("../dist/app.js", import.meta.url));
    expect(existsSync(built), "run `pnpm run build` before the tests").toBe(true);
    const listen = vi.spyOn(net.Server.prototype, "listen");
    // Resolved by Node through the package's own "exports" (self-reference),
    // exactly as a workspace dependent resolves it.
    const mod = (await import("@devotchi/server")) as Record<string, unknown>;
    expect(typeof mod.createServer).toBe("function");
    expect(typeof mod.loadConfig).toBe("function");
    expect(listen).not.toHaveBeenCalled();
  });
});
