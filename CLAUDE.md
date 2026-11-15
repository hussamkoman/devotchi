# Devotchi

Tamagotchi-style portable device for AI coding agents. Connects via MCP to show agent status, handle approvals, and reflect project mood on a physical TFT LCD screen with buttons.

## Project overview

- Monorepo using pnpm workspaces
- TypeScript for all software packages, C (ESP-IDF) for firmware
- Packages: `shared` (types/protocol/API contracts), `server` (Koa WebSocket server + SQLite), `client` (CLI + web UI + MCP)
- Firmware in `firmware/` — separate build, not part of pnpm workspaces

## Architecture

See `docs/architecture.md` for the full system architecture.
See `docs/protocol.md` for the wire protocol specification.
See `hardware/README.md` for hardware design and component details.

## Commands

```bash
pnpm install              # Install all workspace dependencies (Node >= 22.13, pnpm 10)
pnpm run build            # Build shared, server and the bundled client (incl. web UI type-check)
pnpm test                 # Run all package tests (vitest; needs a build first)
pnpm run typecheck        # tsc --noEmit for every package, the web UI and the tests
pnpm run lint             # ESLint (typescript-eslint)
pnpm run server           # Start the server (default port 8080)
pnpm run client           # Run the CLI from source (e.g. pnpm run client -- start --local)
pnpm run dev              # Web UI with Vite HMR against `devotchi start --dev` (DEVOTCHI_DEV_DASHBOARD=<port>)
```

Firmware host tests (no ESP-IDF needed):
`cmake -S firmware/host_test -B build-host -DDEVOTCHI_HOST_TEST_FETCH=ON && cmake --build build-host && ctest --test-dir build-host`.

## MCP server registration

```bash
claude mcp add devotchi -- devotchi mcp
# Or from source:
claude mcp add devotchi -- node packages/client/dist/cli.js mcp
```

## Client CLI

The default server is `http://localhost:8080` (there is no hosted default);
`--server <url>`, `DEVOTCHI_SERVER_URL` or `devotchi config set serverUrl` change it.

```bash
devotchi start                    # Dashboard on 127.0.0.1:3456 for the configured server
devotchi start --local [--lan]    # Run the real server in-process (127.0.0.1, or all interfaces with --lan)
devotchi start --server <url>     # Dashboard for another server
devotchi mcp                      # Run MCP server on stdio
devotchi setup [--dir D] [--check|--remove]  # Install, check or remove the nine Claude Code hooks
devotchi pair <CODE> | pair --new # Confirm a device's code / create one for the device console
devotchi devices [list|rename|identify|rm]   # Device registry with liveness
devotchi status | doctor          # Reachability, token, devices, last request / ordered read-only checks
devotchi mode [ask|desk] | hold [s]          # Room mode and hold time (10..300 s, default 30 s)
devotchi notify <event> [detail] | run -- <cmd...>  # Notify from scripts / wrap a command
devotchi sim [--approve all|none|defer|after=MS|manual]  # Headless virtual device (JSON lines)
devotchi login | logout           # Multi-user accounts
devotchi config [get|set|path]    # Show or change configuration
```

Every command that talks to a server takes `--server <url>`. Tests set
`DEVOTCHI_CONFIG_DIR` to a temporary directory and listen on ephemeral ports.

## Key design decisions

- The server routes WebSocket messages by room; the room comes from the token (SQLite-backed, hashed)
- The MCP server gracefully degrades if the server is unreachable
- Approvals are handled automatically via Claude Code hooks (http type) → server HTTP endpoints — no AI involvement needed
- The server holds hook HTTP requests open while a live device holds the request (ack + 4 s heartbeat), up to the hold time
- Devotchi never allows or denies on its own: only a button press decides; every other outcome is the neutral reply and the terminal prompt takes over
- The protocol is intentionally simple — JSON over WebSocket, messages under 2KB
- `devotchi start --local` embeds the real server; the dashboard binds 127.0.0.1 only and proxies with the CLI token
- Token-first auth — anonymous tokens by default, accounts optional for multi-user hosted mode
- Firmware is plain C on ESP-IDF v6.0: ST7789 TFT LCD (2.0", 320x240) via `esp_lcd`, UI with LVGL 9, JSON with cJSON
- Single PCB design — power and logic on one board

## Directory structure

```
packages/shared/src/           — Protocol types, validators, API types, config types
packages/server/src/           — Koa server (routes, WebSocket, SQLite, permissions, devices)
packages/client/src/           — CLI, MCP server, hook setup, dashboard proxy, sim
packages/client/web/           — Dashboard web UI (Vite, framework-free TypeScript)
firmware/                      — ESP32-S3 firmware (C, ESP-IDF v6.0, LVGL 9); host tests in firmware/host_test
docs/                          — Architecture, protocol, deployment, hardware docs + datasheets
```

## Commit message rules

- Short subject line (under 72 chars)
- No co-authored-by or AI attribution
- Use conventional commits: `feat:`, `fix:`, `docs:`, `chore:`, `refactor:`
- No body unless absolutely needed

## Hooks (automatic — no AI action needed)

`devotchi setup` installs nine HTTP hooks into `.claude/settings.local.json` (protocol 8.7). When Claude Code is about to ask for a permission, the `PermissionRequest` hook POSTs to the server, which shows the request on every device and dashboard. A button press there allows or denies; the same path answers Claude's questions (`AskUserQuestion`, options only) and approves or revises plans (`ExitPlanMode`) on consumers that declare the `ask`/`plan` caps (protocol 14). A request nobody answers falls back to Claude Code's own terminal prompt: after the hold time (default 30 s), at once when no device is live or the room is in desk mode, or when the user picks "send to terminal". Devotchi never auto-denies. The other hooks send status lines and cancel requests the terminal already handled.

## Devotchi MCP tool usage (voluntary — for AI agents with Devotchi connected)

When this MCP server is registered in a Claude Code session, the agent should use the tools as follows:

- Call `show_status` when starting a new task or subtask (the text on the pet)
- Call `notify` when tests pass/fail, builds succeed/fail, or tasks complete
- Call `set_mood` to reflect the overall project state
- Call `get_device_state` to see whether a device is online and holding requests

`show_status` without a mood keeps the pet's current mood, so use `set_mood` for the mood and `show_status` for the text.

### Mood guide (one list for every tool)

- `happy` — tests passing, builds green, tasks completing
- `sad` — tests failing, errors, things not working
- `focused` — deep in a complex task
- `thinking` — analyzing, planning, reading code
- `working` — editing and running things
- `anxious` — waiting for long operations, risky changes ahead
- `excited` — just shipped something, breakthrough moment
- `proud` — all tests green after a big refactor
- `angry` — repeated failures, flaky tests
- `idle` — between tasks
- `sleeping` — idle for a long time, waiting for instructions
