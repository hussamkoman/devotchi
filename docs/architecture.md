# System Architecture

## Overview

Devotchi has 4 layers. The software stack covers Layers 1-3. Layer 4 (ESP32 firmware) is built separately in C.

```
┌──────────┐  MCP     ┌──────────┐  WebSocket  ┌────────────┐
│  Claude   │ <------> │  MCP     │ ----------> │            │
│  Code     │  stdio   │  Server  │             │   Broker   │
└──────────┘          └──────────┘             └─────┬──────┘
   AI Agent            Layer 1                   Layer 2
   (any MCP client)    (runs on your PC)         (deployable)
                                                     │
                              ┌───────────────────────┼────────────────┐
                              v                       v                v
                     ┌──────────────┐       ┌──────────────┐  ┌──────────────┐
                     │  Simulator   │       │  ESP32       │  │  Phone PWA   │
                     │  (browser)   │       │  (firmware)  │  │  (future)    │
                     └──────────────┘       └──────────────┘  └──────────────┘
                      Dev tool               Layer 4           Layer 3
                                             (C, ESP-IDF)      (future)
```

## Layer 1: MCP Server (`packages/mcp-server/`)

The bridge between the AI agent and the Devotchi ecosystem.

### Responsibilities
- Exposes 5 MCP tools via stdio transport
- Translates tool calls into protocol messages
- Sends messages to the broker via WebSocket
- Manages blocking request/response flow for approvals
- Handles reconnection if the broker drops
- Generates and stores pairing code

### Tools

| Tool | Blocking | Returns |
|---|---|---|
| `show_status(message, mood?)` | No | `{ ok: true }` |
| `request_approval(description, details?)` | **Yes** | `{ approved: boolean, timestamp: string }` |
| `request_permission(action, details?)` | **Yes** | `{ granted: boolean, timestamp: string }` |
| `notify(event, mood, detail?)` | No | `{ ok: true }` |
| `set_mood(mood, reason?)` | No | `{ ok: true }` |

### Blocking behavior
- `request_approval` and `request_permission` create a Promise internally
- The MCP server sends the request to the broker, then waits for a matching response (keyed by request ID)
- Configurable timeout (default 5 minutes) — returns `{ approved: false, reason: "timeout" }` on expiry
- The AI agent is blocked during this time, which is the desired behavior — it should wait for human input

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

### CLI commands
- `devotchi pair` — show or generate pairing code
- `devotchi config` — show/edit config
- `devotchi status` — check broker connection and paired clients
- `devotchi test` — send a test notification

### Graceful degradation
- If the broker is unreachable, non-blocking tools return immediately with `{ ok: true, brokerConnected: false }`
- Blocking tools return `{ approved: false, reason: "broker_disconnected" }` immediately — the agent should not hang forever

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
- Configurable port via `DEVOTCHI_BROKER_PORT` env var (default 8080)
- Deployable to Railway, Render, Fly.io, or any Node.js host
- CORS headers for browser WebSocket connections

### Message routing rules
- Producer messages → forwarded to ALL consumers with the same pairing code
- Consumer messages → forwarded to the ONE producer with the same pairing code
- System messages (register, ping, pong, error) → handled by broker, never forwarded

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
- Single `index.html` file — no build step, no framework
- Connects to broker via WebSocket
- Shows Tamagotchi-style character (CSS/canvas based)
- Clickable approve/reject buttons
- Message log
- Pairing code input
- Connection status indicator

---

## Data Flow Examples

### Status update (non-blocking)
```
Claude Code calls show_status("Refactoring auth...", "focused")
  → MCP Server receives tool call
  → MCP Server sends {"type":"status","msg":"Refactoring auth...","mood":"focused"} to broker
  → Broker forwards to all consumers
  → Device/simulator updates display
  → MCP Server returns {ok: true} to Claude Code immediately
```

### Approval request (blocking)
```
Claude Code calls request_approval("Delete 3 test files?", "test1.js, test2.js, test3.js")
  → MCP Server receives tool call
  → MCP Server generates ID "a1b2c3", sends {"type":"approval","id":"a1b2c3","desc":"Delete 3 test files?",...} to broker
  → Broker forwards to all consumers
  → Device shows approval modal, vibrates
  → User presses Approve button
  → Device sends {"type":"approval_response","id":"a1b2c3","approved":true} to broker
  → Broker forwards to producer
  → MCP Server resolves the pending Promise
  → MCP Server returns {approved: true, timestamp: "..."} to Claude Code
  → Claude Code proceeds with deletion
```

### Timeout on approval
```
(same as above, but user doesn't respond within 5 minutes)
  → MCP Server's timeout fires
  → MCP Server returns {approved: false, reason: "timeout"} to Claude Code
  → Claude Code decides not to proceed (or asks again)
```
