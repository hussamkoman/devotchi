# Devotchi

A Tamagotchi-style portable device that connects to AI coding agents (Claude Code, Cursor, etc.) via the Model Context Protocol (MCP). Your Devotchi shows what the agent is doing, handles approval requests via physical buttons, and its mood reflects the project's health.

Carry it in your pocket, clip it to your bag, or leave it on your desk — no need to stare at a terminal waiting for approval prompts. It vibrates, shows a nervous face, and waits for you to press a button.

## How it works

```
AI Agent ──Hook HTTP──> Broker ──WebSocket──> Device
AI Agent ──MCP stdio──> MCP Server ──WebSocket──> Broker ──WebSocket──> Device
```

1. **Claude Code hooks** automatically forward permission requests to the **broker** via HTTP
2. The broker sends them to your **Devotchi device** over WiFi (WebSocket)
3. You approve or reject directly on the device with physical buttons
4. The response goes back to Claude Code, which continues (or stops)
5. The **MCP server** handles voluntary actions: status updates, mood, notifications

## Project structure

```
devotchi/
├── packages/
│   ├── shared/              # Protocol types and constants
│   ├── broker/              # WebSocket message broker
│   ├── mcp-server/          # MCP server (Node.js, TypeScript)
│   └── simulator/           # Browser-based device simulator (dev tool)
├── firmware/                # ESP32 firmware (C, ESP-IDF)
├── docs/                    # Architecture, protocol, hardware docs + datasheets
├── CLAUDE.md
└── README.md
```

## Quick start

```bash
pnpm install
pnpm run build
```

**Terminal 1** — Start the broker:
```bash
pnpm run broker
```

**Terminal 2** — Start the simulator:
```bash
pnpm run simulator:dev
```

Open `http://localhost:5173` in your browser.

**Setup Claude Code hooks:**

1. Run the MCP server once to generate your pairing code:
   ```bash
   node packages/mcp-server/dist/index.js
   ```
   Kill it after you see the pairing code. Check `~/.devotchi/config.json` for the code.

2. Register the MCP server:
   ```bash
   claude mcp add devotchi -- node packages/mcp-server/dist/index.js
   ```

3. Copy `.claude/settings.local.json.example` to `.claude/settings.local.json` and replace `YOUR_PAIRING_CODE` with your code.

4. Enter the same pairing code in the simulator and click Connect.

5. Start a Claude Code session — permission requests will appear on the simulator automatically.

## Documentation

- [Architecture](docs/architecture.md) — system overview, layers, data flow
- [Protocol](docs/protocol.md) — wire protocol specification
- [Hardware](hardware/README.md) — components, pin assignments, PCB design notes

## License

TBD
