# Devotchi

A Tamagotchi-style portable device that connects to AI coding agents (Claude Code, Cursor, etc.) via the Model Context Protocol (MCP). Your Devotchi shows what the agent is doing, handles approval requests via physical buttons, and its mood reflects the project's health.

Carry it in your pocket, clip it to your bag, or leave it on your desk — no need to stare at a terminal waiting for approval prompts. It vibrates, shows a nervous face, and waits for you to press a button.

## How it works

```
User's PC                          Server (hosted or self-hosted)
┌─────────────────────┐            ┌─────────────────────────┐
│  devotchi CLI        │            │  @devotchi/server       │
│  ├─ Web UI (:3456)   │◄──HTTP───│  ├─ REST API             │
│  ├─ MCP server       │           │  ├─ WebSocket router     │
│  └─ Hook config      │──hooks──►│  ├─ Hook endpoints       │
│                      │           │  └─ SQLite               │
│  Claude Code         │──hooks──►│                          │
└─────────────────────┘            └──────────┬──────────────┘
                                              │ WebSocket
                                   ┌──────────▼──────────────┐
                                   │  Devotchi Device (ESP32) │
                                   └─────────────────────────┘
```

1. **Claude Code hooks** automatically forward permission requests to the **server** via HTTP
2. The server sends them to your **Devotchi device** over WiFi (WebSocket)
3. You approve or reject directly on the device with physical buttons
4. The response goes back to Claude Code, which continues (or stops)
5. The **MCP server** (embedded in the client) handles voluntary actions: status updates, mood, notifications

## Quick start

```bash
npx devotchi
```

This starts the dashboard, connects to the server, and opens the web UI. On first run it generates a token and pairing code automatically.

### Auto-configure Claude Code hooks

```bash
npx devotchi setup
```

### Register MCP server

```bash
claude mcp add devotchi -- npx devotchi mcp
```

## Self-hosted / Local mode

Run the server yourself:

```bash
npx @devotchi/server
```

Then point the client at it:

```bash
npx devotchi start --server http://localhost:8080
```

Or run everything locally without a separate server:

```bash
npx devotchi start --local
```

## Development

```bash
git clone <repo-url>
cd devotchi
pnpm install
pnpm run build
```

### Run locally

```bash
# Terminal 1 — Start the server
pnpm run server

# Terminal 2 — Start the client dashboard
pnpm run client
```

The web UI opens at `http://localhost:3456`.

## Project structure

```
devotchi/
├── packages/
│   ├── shared/              # Protocol types, API types, config
│   ├── server/              # Koa server (WebSocket + REST + SQLite)
│   └── client/              # CLI + web UI + MCP server
├── firmware/                # ESP32 firmware (C, ESP-IDF)
├── hardware/                # KiCad schematics and PCB design
├── docs/                    # Architecture, protocol, hardware docs
└── README.md
```

## Documentation

- [Architecture](docs/architecture.md) — system overview, layers, data flow
- [Protocol](docs/protocol.md) — wire protocol specification
- [Hardware](hardware/README.md) — components, pin assignments, PCB design notes

## License

TBD
