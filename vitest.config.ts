import { fileURLToPath } from "node:url";
import { defineConfig } from "vitest/config";

const sharedSrc = fileURLToPath(new URL("./packages/shared/src/index.ts", import.meta.url));

function project(name: string, dir: string) {
  return {
    extends: true as const,
    test: { name, root: dir, include: ["test/**/*.test.ts"] },
  };
}

export default defineConfig({
  resolve: {
    // Tests of every package run against the shared sources, so a stale
    // packages/shared/dist can never make a test pass or fail.
    alias: { "@devotchi/shared": sharedSrc },
  },
  test: {
    projects: [
      project("shared", "packages/shared"),
      project("server", "packages/server"),
      project("client", "packages/client"),
    ],
  },
});
