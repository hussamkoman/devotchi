# Devotchi

A Tamagotchi-style portable device that connects to AI coding agents (Claude Code, Cursor, etc.) via the Model Context Protocol (MCP). Your Devotchi shows what the agent is doing, handles approval requests via physical buttons, and its mood reflects the project's health.

Carry it in your pocket, clip it to your bag, or leave it on your desk — no need to stare at a terminal waiting for approval prompts. It vibrates, shows a nervous face, and waits for you to press a button.

## How it works

```
AI Agent <--stdio--> MCP Server <--WebSocket--> Broker <--WebSocket--> Device
```

1. The **MCP server** runs on your PC alongside your AI coding agent
2. It sends status updates, approval requests, and notifications to the **broker**
3. The broker forwards them to your **Devotchi device** over WiFi
4. You approve or reject directly on the device with physical buttons
5. The response goes back to the agent, which continues (or stops)

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

TBD — software stack not yet built.

## Documentation

- [Architecture](docs/architecture.md) — system overview, layers, data flow
- [Protocol](docs/protocol.md) — wire protocol specification
- [Hardware](docs/hardware.md) — components, pin assignments, PCB design notes

## License

TBD
