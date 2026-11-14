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
pnpm install              # Install all workspace dependencies
pnpm run build            # Build all TypeScript packages
pnpm run server           # Start the server on port 8080
pnpm run client           # Start the client dashboard
pnpm run dev              # Start client in dev mode (Vite HMR)
pnpm test                 # Run all package tests (vitest)
pnpm run typecheck        # tsc --noEmit for every package, the web UI and the tests
pnpm run lint             # ESLint (typescript-eslint)
```

## MCP server registration

```bash
claude mcp add devotchi -- devotchi mcp
# Or from source:
claude mcp add devotchi -- node packages/client/dist/cli.js mcp
```

## Client CLI

```bash
devotchi start                    # Start dashboard (connects to the configured server)
devotchi start --local [--lan]    # Run the real server in-process (127.0.0.1, or all interfaces with --lan)
devotchi start --server <url>     # Connect to custom server
devotchi mcp                      # Run MCP server on stdio
devotchi setup [--dir D] [--check|--remove]  # Install, check or remove the nine Claude Code hooks
devotchi pair <CODE> | pair --new # Confirm a device's code / create one for the device console
devotchi devices [list|rename|identify|rm]   # Device registry with liveness
devotchi status | doctor          # Reachability, token, devices, last request / ordered read-only checks
devotchi mode [ask|desk] | hold [s]          # Room mode and hold time (10..300 s)
devotchi notify <event> [detail] | run -- <cmd...>  # Notify from scripts / wrap a command
devotchi sim [--approve all|none|defer|after=MS|manual]  # Headless virtual device (JSON lines)
devotchi login | logout           # Multi-user accounts
devotchi config [get|set|path]    # Show or change configuration
```

Every command that talks to a server takes `--server <url>`. Tests set
`DEVOTCHI_CONFIG_DIR` to a temporary directory and listen on ephemeral ports.

## Key design decisions

- The server routes WebSocket messages by pairing code with SQLite-backed token auth
- The MCP server gracefully degrades if the server is unreachable
- Approvals are handled automatically via Claude Code hooks (http type) → server HTTP endpoints — no AI involvement needed
- The server holds hook HTTP requests open until the device responds (long-polling pattern)
- The protocol is intentionally simple — JSON over WebSocket, messages under 2KB
- The client includes an embedded broker for standalone `--local` mode
- Token-first auth — anonymous tokens by default, accounts optional for multi-user hosted mode
- Display uses ST7789 TFT LCD (2.0", 320x240, color) with ESP-IDF `esp_lcd` driver + LovyanGFX for graphics
- Single PCB design — power and logic on one board

## Directory structure

```
packages/shared/src/           — Protocol types, API types, config types
packages/server/src/           — Koa server (routes, WebSocket, SQLite)
packages/client/src/           — CLI, MCP server, hook config, connection
packages/client/web/           — Web UI (Vite SPA, dashboard + simulator)
firmware/                      — ESP32 firmware (C, ESP-IDF)
docs/                          — Architecture, protocol, hardware docs + datasheets
```

## Commit message rules

- Short subject line (under 72 chars)
- No co-authored-by or AI attribution
- Use conventional commits: `feat:`, `fix:`, `docs:`, `chore:`, `refactor:`
- No body unless absolutely needed

## Hooks (automatic — no AI action needed)

Permission requests are handled automatically via Claude Code hooks configured in `.claude/settings.local.json`. When Claude Code shows a permission prompt, an HTTP hook POSTs to the server, which forwards to the device. The user approves/rejects on the device and Claude Code proceeds or stops. Status updates are also sent automatically after Bash/Write/Edit tool use.

Run `devotchi setup` to auto-configure hooks.

## Devotchi MCP tool usage (voluntary — for AI agents with Devotchi connected)

When this MCP server is registered in a Claude Code session, the agent should use the tools as follows:

- Call `show_status` when starting a new task or subtask
- Call `notify` when tests pass/fail, builds succeed/fail, or tasks complete
- Call `set_mood` to reflect the overall project state

### Mood guide

- `happy` — tests passing, builds green, tasks completing
- `sad` — tests failing, errors, things not working
- `focused` — deep in a complex task
- `thinking` — analyzing, planning, reading code
- `anxious` — waiting for long operations, risky changes ahead
- `excited` — just shipped something, breakthrough moment
- `proud` — all tests green after a big refactor
- `angry` — repeated failures, flaky tests
- `sleeping` — idle, waiting for instructions
