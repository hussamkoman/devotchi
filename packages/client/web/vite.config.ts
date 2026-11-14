import { defineConfig, type Plugin } from "vite";

// `pnpm run dev` serves the web UI with HMR and proxies /api and /ws to a
// dashboard started with `devotchi start --dev`, which hands out its session
// secret on /dev-session. The proxy presents the dashboard's own Origin
// because the dashboard refuses every other one.
const dashboardPort = Number(process.env.DEVOTCHI_DEV_DASHBOARD ?? 3456);
const dashboard = `http://127.0.0.1:${dashboardPort}`;

function devSession(): Plugin {
  return {
    name: "devotchi-dev-session",
    apply: "serve",
    async transformIndexHtml() {
      let secret = "";
      try {
        const res = await fetch(`${dashboard}/dev-session`);
        if (res.ok) secret = ((await res.json()) as { secret?: string }).secret ?? "";
      } catch {
        // No dashboard yet: the page loads and the UI reports the failing requests.
      }
      return [{ tag: "meta", attrs: { name: "devotchi-session", content: secret }, injectTo: "head" }];
    },
  };
}

export default defineConfig({
  root: "web",
  build: {
    outDir: "../dist/web",
    emptyOutDir: true,
  },
  plugins: [devSession()],
  server: {
    proxy: {
      "/api": {
        target: dashboard,
        changeOrigin: true,
        configure: (proxy) => proxy.on("proxyReq", (req) => req.setHeader("origin", dashboard)),
      },
      "/ws": {
        target: dashboard,
        ws: true,
        changeOrigin: true,
        configure: (proxy) => proxy.on("proxyReqWs", (req) => req.setHeader("origin", dashboard)),
      },
    },
  },
});
