# System Architecture

## Overview

Devotchi has 4 layers. The software stack covers Layers 1-3. Layer 4 (ESP32 firmware) is built separately in C.

```
                                            ┌────────────────────────────────────┐
┌──────────┐  Hook HTTP POST                │            Broker                  │
│  Claude   │ ──────────────────────────────>│  /hook/permission (long-poll)      │
│  Code     │ <─────────────────────────────│  /hook/status     (fire & forget)  │
│           │  Hook JSON response            │  /hook/stop       (fire & forget)  │
│           │                                │                                    │
│           │  MCP stdio    ┌──────────┐ WS  │                                    │
│           │ <──────────── │  MCP     │ ──> │   WebSocket router                 │
│           │  (voluntary)  │  Server  │     │                                    │
└──────────┘               └──────────┘     └──────────────┬─────────────────────┘
   AI Agent                 Layer 1              Layer 2    │
   (any MCP client)         (mood/status/notify) (deployable)
                                                            │
                              ┌──────────────────────────────┼────────────────┐
                              v                              v                v
                     ┌──────────────┐              ┌──────────────┐  ┌──────────────┐
                     │  Simulator   │              │  ESP32       │  │  Phone PWA   │
                     │  (browser)   │              │  (firmware)  │  │  (future)    │
                     └──────────────┘              └──────────────┘  └──────────────┘
                      Dev tool                      Layer 4           Layer 3
                                                    (C, ESP-IDF)      (future)
```

## Layer 1: MCP Server (`packages/mcp-server/`)

Voluntary tools for AI-initiated actions (mood, status, notifications). Approvals are handled automatically via hooks — see "Claude Code Hooks" below.

### Responsibilities
- Exposes 3 MCP tools via stdio transport (voluntary, non-blocking)
- Translates tool calls into protocol messages
- Sends messages to the broker via WebSocket as a producer
- Handles reconnection if the broker drops
- Generates and stores pairing code

### Tools

| Tool | Returns |
|---|---|
| `show_status(message, mood?)` | `{ ok: true }` |
| `notify(event, mood, detail?)` | `{ ok: true }` |
| `set_mood(mood, reason?)` | `{ ok: true }` |

### Configuration

Stored in `~/.devotchi/config.json`:
```json
{
  "pairingCode": "A7X9K2",
  "brokerUrl": "ws://localhost:8080",
  "approvalTimeout": 300000,
  "logLevel": "info"
}
```

### Graceful degradation
- If the broker is unreachable, tools return immediately with `{ ok: true, brokerConnected: false }`

---

## Layer 2: Broker (`packages/broker/`)

A stateless WebSocket message router. Its only job is to match producers and consumers by pairing code and forward messages between them.

### How pairing works
1. MCP server connects and sends: `{ type: "register", role: "producer", pairingCode: "A7X9K2" }`
2. Broker responds with: `{ type: "register_ack", role: "producer", pairingCode: "A7X9K2", connectedPeers: 0 }`
3. Device/simulator connects and sends: `{ type: "register", role: "consumer", pairingCode: "A7X9K2" }`
4. Broker responds with: `{ type: "register_ack", role: "consumer", pairingCode: "A7X9K2", connectedPeers: 1 }`
5. Messages flow: producer -> broker -> all consumers with that code
6. Responses flow: any consumer -> broker -> producer

**Note:** Consumers include the simulator (for dev/testing), the ESP32 physical device (via WiFi WebSocket), and potentially a phone PWA in the future. Multiple consumers can be connected simultaneously.

### Key design decisions
- **Stateless** — no database, no persistence. All state is in-memory WebSocket connections.
- **Multi-consumer** — one producer can have multiple consumers (e.g., simulator + physical device simultaneously)
- **Single producer per code** — only one MCP server per pairing code (last one wins if there's a conflict)
- **No auth** — pairing code is the only security. Fine for MVP. Codes are 6 characters from a 31-char alphabet = ~887 million possibilities.

### Features
- Heartbeat ping/pong every 30 seconds to detect dead connections
- Clean up disconnected clients automatically
- Basic rate limiting to prevent flooding
- HTTP health check endpoint: `GET /` returns `{ status: "ok", connections: N }`
- HTTP hook endpoints for Claude Code hooks (see below)
- Configurable port via `DEVOTCHI_BROKER_PORT` env var (default 8080)
- Deployable to Railway, Render, Fly.io, or any Node.js host
- CORS headers for browser WebSocket connections

### Message routing rules
- Producer messages → forwarded to ALL consumers with the same pairing code
- Consumer messages → first checked against pending hook requests (by ID), otherwise forwarded to producer
- System messages (register, ping, pong, error) → handled by broker, never forwarded

---

## Claude Code Hooks

Approvals and status updates are handled automatically via Claude Code `http` hooks — no AI involvement, zero context cost. Configured in `.claude/settings.local.json`.

### How it works

1. Claude Code fires a `PermissionRequest` hook → HTTP POST to broker's `/hook/permission?code=XXXXXX`
2. Broker translates to a Devotchi `permission` message, sends to all consumers via WebSocket
3. User approves/rejects on device/simulator
4. Consumer sends `permission_response` back via WebSocket
5. Broker returns hook JSON response: `{ decision: { behavior: "allow"|"deny" } }`
6. Claude Code proceeds or blocks — all within one HTTP request/response cycle

### Hook endpoints

| Endpoint | Hook Event | Behavior |
|---|---|---|
| `POST /hook/permission?code=X` | `PermissionRequest` | Long-poll: waits for device response, returns allow/deny |
| `POST /hook/status?code=X` | `PostToolUse` | Fire-and-forget: sends status update to device |
| `POST /hook/stop?code=X` | `Stop` | Fire-and-forget: notifies device agent stopped |

### Graceful degradation
- If no consumers are connected, `/hook/permission` returns `{}` (allow) — the agent is not blocked when nobody is watching
- `/hook/status` and `/hook/stop` always return `{}` immediately

---

## Phone PWA (future — `packages/pwa/`)

A companion phone app. Not part of the current build. May be added later as:
- A phone-based Tamagotchi UI (WebSocket to broker)
- A BLE bridge between phone and ESP32 device
- Installable as PWA with push notifications for approvals

This is a future idea — the physical device is the core product.

---

## Layer 4: ESP32 Firmware (separate — `firmware/`)

Written in C using ESP-IDF (not Arduino). Runs on the physical device.

### Responsibilities
- Connect to WiFi and/or BLE
- Receive protocol messages (via WebSocket over WiFi, or via BLE from phone)
- Render Tamagotchi character on SSD1351/SSD1327 OLED display
- Handle button presses (approve/reject/action)
- Send responses back
- Manage power (deep sleep, wake on event)
- Drive vibration motor and status LED

See [hardware.md](hardware.md) for component details and pin assignments.

---

## Dev Tool: Simulator (`packages/simulator/`)

A browser-based simulator that acts as a "device" consumer. Essential for testing the software stack without physical hardware.

### Implementation
- Vite + TypeScript app
- Connects to broker via WebSocket
- Shows Tamagotchi-style character (CSS/canvas based)
- Clickable approve/reject buttons
- Message log
- Pairing code input
- Connection status indicator

---

## Data Flow Examples

### Permission request via hook (automatic)
```
Claude Code wants to run "rm -rf dist"
  → PermissionRequest hook fires
  → HTTP POST to broker /hook/permission?code=A7X9K2
  → Broker generates ID, sends {"type":"permission","id":"a1b2c3","action":"Bash",...} to consumers
  → Device/simulator shows permission modal
  → User presses Approve
  → Device sends {"type":"permission_response","id":"a1b2c3","granted":true} to broker
  → Broker returns { decision: { behavior: "allow" } } as HTTP response
  → Claude Code proceeds with the command
```

### Status update via hook (automatic)
```
Claude Code finishes running a Bash command
  → PostToolUse hook fires
  → HTTP POST to broker /hook/status?code=A7X9K2
  → Broker sends {"type":"status","msg":"echo hello world","mood":"working"} to consumers
  → Device/simulator updates display
  → Broker returns {} immediately
```

### Voluntary status update via MCP
```
Claude Code calls show_status("Refactoring auth...", "focused")
  → MCP Server sends {"type":"status","msg":"Refactoring auth...","mood":"focused"} to broker
  → Broker forwards to all consumers
  → Device/simulator updates display
  → MCP Server returns {ok: true} to Claude Code
```

### Timeout on permission
```
(same as permission request, but user doesn't respond within 5 minutes)
  → Broker's timeout fires
  → Broker returns { decision: { behavior: "deny" } } as HTTP response
  → Claude Code blocks the action
```
