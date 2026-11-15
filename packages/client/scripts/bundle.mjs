// Bundles the CLI into dist/cli.js with the workspace packages (shared and
// server) inlined, so the packed tarball installs without the monorepo.
// Third-party dependencies stay external and are installed from npm.

import { readFileSync } from "node:fs";
import { URL } from "node:url";
import { build } from "esbuild";

const read = (path) => readFileSync(new URL(path, import.meta.url), "utf8");
const pkg = JSON.parse(read("../package.json"));
const serverPkg = JSON.parse(read("../../server/package.json"));

const missing = Object.keys(serverPkg.dependencies).filter((d) => !d.startsWith("@devotchi/") && !(d in pkg.dependencies));
if (missing.length > 0) throw new Error(`add the server's dependencies to the client package.json: ${missing.join(", ")}`);

await build({
  entryPoints: [new URL("../src/cli.ts", import.meta.url).pathname],
  outdir: new URL("../dist", import.meta.url).pathname,
  // Splitting keeps `import("@devotchi/server")` lazy: node:sqlite then loads
  // only for --local, after local.ts installed its warning filter.
  splitting: true,
  chunkNames: "chunks/[name]-[hash]",
  bundle: true,
  platform: "node",
  target: "node22.13",
  format: "esm",
  sourcemap: true,
  legalComments: "none",
  external: Object.keys(pkg.dependencies),
  define: {
    __DEVOTCHI_CLIENT_VERSION__: JSON.stringify(pkg.version),
    __DEVOTCHI_SERVER_VERSION__: JSON.stringify(serverPkg.version),
    __DEVOTCHI_ACCOUNT_PAGE__: JSON.stringify(read("../../server/public/account.html")),
    // Built by the server package's build (scripts/build-pets.mjs), which runs first.
    __DEVOTCHI_PET_CATALOG__: JSON.stringify(read("../../server/dist/pets.json")),
  },
  logLevel: "warning",
});
