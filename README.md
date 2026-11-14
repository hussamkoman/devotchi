# Devotchi

A Tamagotchi-style portable device for AI coding agents. It hooks into Claude Code's permission prompts and talks to agents through the Model Context Protocol (MCP). Your Devotchi shows what the agent is doing, handles approval requests via physical buttons, and its mood reflects the project's health.

Carry it in your pocket, clip it to your bag, or leave it on your desk — no need to stare at a terminal waiting for approval prompts. It vibrates, shows a nervous face, and waits for you to press a button.

## How it works

```
+------------------+          +---------------------+
|  Your PC         |  hooks   |  Devotchi server    |
|  Claude Code  ---|--------->|  /hook/event        |
|                  |          |  WebSocket rooms    |
|  MCP server   ---|--ws----->|  SQLite             |
|  dashboard    <--|--ws------|                     |
+------------------+          +---------+-----------+
                                        | WebSocket (WiFi)
                                        v
                              +---------------------+
                              |  Devotchi device    |
                              |  (ESP32-S3 + LCD)   |
                              +---------------------+
```

1. **Claude Code hooks** (`devotchi setup`) send each permission request to the server over HTTP.
2. The server shows it on your **Devotchi device** and on the local dashboard.
3. You approve or reject with a button; Claude Code continues or stops.
4. The **MCP server** lets the agent send status lines, moods and notifications.

### What happens when nobody answers

Devotchi never allows or denies a request on its own. Only a button press
decides. In every other case the hook returns the neutral reply and Claude
Code shows its own terminal prompt:

- after the **hold time** when a device shows the request and nobody answers
  (default **30 s**, 10 s to 5 min, `devotchi hold <s>` or the dashboard);
- at once when no device is online, a device does not acknowledge the
  request within 3 s, or its heartbeat stops;
- at once in **desk mode** (`devotchi mode desk`), for when you are at the keyboard;
- when you press **send to terminal** on the device or the dashboard.

`devotchi doctor` reports whether Claude Code showed its own prompt while a
request was still pending, and when a longer hold time is safe.

## Quick start (LAN)

```bash
pnpm install && pnpm run build            # from a clone; Node >= 22.13, pnpm 10
alias devotchi="node $PWD/packages/client/dist/cli.js"

devotchi start --local --lan              # server on :8080 (all interfaces) + dashboard on 127.0.0.1:3456
devotchi setup                            # install the Claude Code hooks for this project
```

Power the device and set up WiFi over BLE with the code on its screen. The
device connects to its configured server URL (`ws://<your PC's IP>:8080`; set
it from the USB console with `server ws://<ip>:8080`). It then shows a
pairing code:

```bash
devotchi pair <CODE>                      # or type the code into the dashboard
devotchi status                           # device online, last ack, battery
```

For a server on the internet with TLS and accounts, see
[docs/deploy.md](docs/deploy.md).

## MCP registration

```bash
claude mcp add devotchi -- devotchi mcp
```

Tools: `show_status`, `notify`, `set_mood`, `get_device_state`.

## CLI

| Command | What it does |
|---|---|
| `devotchi start [--local [--lan]] [--server <url>]` | Dashboard on 127.0.0.1; `--local` runs the server in the same process |
| `devotchi setup [--dir D] [--check \| --remove]` | Install, check or remove the nine Claude Code hooks |
| `devotchi pair <CODE>` / `pair --new` | Confirm a device's code / create a code for the device console |
| `devotchi devices [list \| rename \| identify \| rm]` | Paired devices with online state, last seen, last ack, battery |
| `devotchi status [--json]` | Server, token, peers, mode, hold time, devices, last request |
| `devotchi doctor` | Read-only checks of configuration, server, token, WebSocket, hooks and devices |
| `devotchi mode [ask \| desk]` | Requests go to devices (ask) or straight to the terminal (desk) |
| `devotchi hold [seconds]` | How long a request waits on a device (10..300 s) |
| `devotchi notify <event> [detail]` | Send a notification from a script |
| `devotchi run -- <cmd...>` | Run a command and notify its result |
| `devotchi sim` | Headless virtual device for testing (JSON lines) |
| `devotchi mcp` | MCP server on stdio |
| `devotchi login` / `logout` | Accounts on a multi-user server |
| `devotchi config [get \| set \| path]` | Show or change the configuration (`~/.devotchi/config.json`) |

The default server is `http://localhost:8080`; every command takes
`--server <url>`. When something does not work, run `devotchi doctor` first.

## Hardware

First prototype PCB, designed in KiCad and manufactured by JLCPCB.

![3D render of Devotchi PCB](hardware/pcb-3d-render.png)

- **MCU:** ESP32-S3-MINI-1 (dual-core 240MHz, WiFi + BLE, 4MB flash, 2MB PSRAM)
- **Display:** 2.0" IPS TFT LCD, 320x240, ST7789 driver, SPI interface
- **Input:** 3 tactile buttons (approve, reject, action) — any button press wakes from deep sleep
- **Power:** LiPo battery, USB-C charging (TP4056), DW01A protection, ME6211 3.3V LDO
- **Output:** Status LED, vibration motor

See [hardware/README.md](hardware/README.md) for full component details, pin assignments, and BOM.

## Project status

Work in progress. The first prototype PCB is built. The server, CLI, MCP
server, hook integration and dashboard are complete for single-user LAN use;
multi-user mode and the Docker/Caddy deployment are written and tested
locally. The firmware is plain C on ESP-IDF v6.0 with LVGL 9 (see
[firmware/CONVENTIONS.md](firmware/CONVENTIONS.md) and
[docs/architecture.md](docs/architecture.md) section 4). Open work is in
[TODO.md](TODO.md).

## Repository structure

```
hardware/          KiCad schematics, PCB layout, BOM
firmware/          ESP32-S3 firmware (C, ESP-IDF v6.0, LVGL 9) and host tests
packages/
  shared/          Protocol v2 types, validator, text and tool formatting
  server/          WebSocket server + REST API + SQLite
  client/          CLI, MCP server, web dashboard
case/              3D-printable enclosure exploration
docs/              Architecture, protocol spec, datasheets
```

## Development

```bash
pnpm install       # Node >= 22.13, pnpm 10
pnpm run build
pnpm test          # All package tests (vitest)
pnpm run typecheck
pnpm run lint
pnpm run server    # Start the server on :8080
pnpm run client    # Run the CLI from source, e.g. pnpm run client start --local
pnpm run dev       # Web UI with HMR against `devotchi start --dev` (DEVOTCHI_DEV_DASHBOARD=<port>)
```

## Documentation

- [Architecture](docs/architecture.md) — system overview, layers, data flow
- [Protocol](docs/protocol.md) — wire protocol specification
- [Deployment](docs/deploy.md) — LAN self-hosting, Docker Compose with Caddy, multi-user mode, backups
- [Hardware](hardware/README.md) — components, pin assignments, PCB design
- [Firmware conventions](firmware/CONVENTIONS.md) — plain-C rules, building, flashing and host tests

## License

[MIT](LICENSE)
