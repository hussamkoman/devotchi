import { execFile } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import net from "node:net";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";
import { afterEach, describe, expect, it, vi } from "vitest";

const run = promisify(execFile);
const serverDir = fileURLToPath(new URL("..", import.meta.url));
const clientDir = fileURLToPath(new URL("../../client", import.meta.url));

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

  it("resolves through the client's workspace link in plain Node, with types, without listening", async () => {
    const pkg = JSON.parse(readFileSync(join(serverDir, "package.json"), "utf8")) as Record<string, unknown>;
    expect(pkg.main).toBeUndefined();
    expect(pkg.exports).toEqual({ ".": { types: "./dist/app.d.ts", import: "./dist/app.js" } });
    expect(existsSync(join(serverDir, "dist", "app.d.ts")), "run `pnpm run build` before the tests").toBe(true);

    // Runs outside vitest so Node's own resolver follows node_modules/@devotchi/server.
    const script = `
      import net from "node:net";
      let calls = 0;
      const listen = net.Server.prototype.listen;
      net.Server.prototype.listen = function (...args) { calls++; return listen.apply(this, args); };
      const mod = await import("@devotchi/server");
      await new Promise((resolve) => setTimeout(resolve, 50));
      process.stdout.write(JSON.stringify({
        createServer: typeof mod.createServer,
        calls,
        resolved: import.meta.resolve("@devotchi/server"),
      }));
    `;
    const { stdout } = await run(process.execPath, ["--no-warnings", "--input-type=module", "-e", script], {
      cwd: clientDir,
      timeout: 10_000,
    });
    const result = JSON.parse(stdout) as { createServer: string; calls: number; resolved: string };
    expect(result.createServer).toBe("function");
    expect(result.calls).toBe(0);
    expect(fileURLToPath(result.resolved)).toBe(join(serverDir, "dist", "app.js"));
  });
});
