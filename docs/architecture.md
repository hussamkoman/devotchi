# System Architecture

Devotchi is a desk/pocket device that shows what an AI coding agent is doing
and lets its owner answer the agent's permission prompts with a button.

```
                 PC                                   LAN or hosted
   +---------------------------+              +---------------------------+
   | Claude Code               |  HTTP hooks  | Server (packages/server)  |
   |   hooks  -----------------|------------->|  REST + hooks + WebSocket |
   |   MCP (devotchi mcp) -----|--- WS ------>|  SQLite                   |
   | devotchi CLI + dashboard -|--- WS/REST ->|                           |
   +---------------------------+              +-------------+-------------+
                                                            | WebSocket (ws/wss)
                                       +--------------------+-------------------+
                                       |                    |                   |
                                 +-----+------+      +------+------+     +------+------+
                                 | Device     |      | Dashboard   |     | devotchi sim|
                                 | (firmware) |      | (browser)   |     | (headless)  |
                                 +------------+      +-------------+     +-------------+
```

The wire contract between these parts is `docs/protocol.md` (protocol v2). This
document describes how each part is built.

| Part | Location | Language |
|---|---|---|
| Shared contract code | `packages/shared` | TypeScript |
| Server | `packages/server` | TypeScript (Node >= 22.13) |
| Client: CLI, MCP server, dashboard, sim | `packages/client` | TypeScript |
| Firmware | `firmware/` | C (ESP-IDF v6.0, LVGL 9) |
| Hardware and case | `hardware/`, `case/` | KiCad, CAD (reference only) |

---

## 1. Shared (`packages/shared`)

Everything two packages must agree on lives here and is unit-tested:

- `protocol.ts`: message types, the single `Mood` enum, limits, error codes.
- `validate.ts`: one validator per message type and direction (type whitelist
  per role, strict types, UTF-8 byte truncation). The server uses it for every
  inbound frame and for every message it builds; the client uses it before
  sending.
- `text.ts`: UTF-8 safe truncation, ASCII sanitising for device text.
- `tool-format.ts`: `formatToolCall(toolName, toolInput, cwd)` producing
  `{tool, summary, details, risk}` for permission cards and status lines, and
  the risk rule table.
- `pairing.ts`: pairing code alphabet, CSPRNG generator, validator.
- `api.ts`: REST request/response types exactly as in the protocol document.

## 2. Server (`packages/server`)

Koa HTTP server plus a `ws` WebSocket server on one port, with SQLite
persistence. One codebase serves three deployments: embedded in the client
(`devotchi start --local`), self-hosted on a LAN, and hosted multi-user.

### 2.1 Structure

```
src/
  index.ts            CLI entry: reads env, calls createServer(), handles signals
  app.ts              createServer(config) -> { url, port, close() }  (used by tests and --local)
  config.ts           env parsing and validation
  db/                 node:sqlite (DatabaseSync, WAL), migrations by PRAGMA user_version
  http/               error middleware, body limits, rate limiter, auth middleware, request log
  routes/             health, auth, tokens, pairing, devices, room, notify, hooks, account page
  rooms/              room registry: producers, consumers, peers, last status/mood, sessions
  permissions/        pending request manager (ack, heartbeat, hold, dismiss, replay)
  ws/                 upgrade auth, register, per-role message routing, heartbeat
public/account.html   minimal account page (multi-user mode)
```

`createServer(config)` has no global state: the database handle, timers and
room registry are per instance, and every timeout is injectable so tests run
in milliseconds.

The package entry point (`exports` in `package.json`) is `app.ts`: importing
`@devotchi/server` gives `createServer` and never opens a socket. `index.ts`
is only the executable (`node dist/index.js`).

### 2.2 Persistence

SQLite through the built-in `node:sqlite` module (no native build, works in a
plain Node Docker image). WAL journal, `foreign_keys=ON`, synchronous writes.
Existing sql.js database files are ordinary SQLite files and are migrated in
place. Before the first migration of a v1 file the server copies it to
`<file>.v0.bak` next to it and logs the path; the copy is never deleted.

| Table | Purpose |
|---|---|
| `tokens` | `id`, `token_hash` (unique), `room_id`, `label`, `kind` (`cli`/`device`), `user_id` (nullable), `created_at`, `last_used` |
| `devices` | Primary key (`room_id`, `id`); `token_id` (unique), `name`, `model`, `fw`, `adopt_id` (migrated rows only), `created_at`, `last_seen`, `last_hold_at`, `telemetry` (JSON), `telemetry_at` |
| `pairings` | Flow B codes: `code`, `room_id`, `expires_at`, `claimed` |
| `device_pairings` | Flow A: `code`, `secret_hash`, `device_id`, `model`, `fw`, `expires_at`, `state` (`pending`/`confirmed`/`delivered`), `room_id` and `confirmed_at` (set on confirm), `name` |
| `room_settings` | `room_id`, `mode`, `hold_ms`, `share_text` |
| `users`, `sessions` | Multi-user accounts and login sessions |

Migration from the v1 schema: tokens whose label is `device` become kind
`device` and get a `devices` row with a provisional id (`dv-` + the first 6
hex of the token id, name `device`, `adopt_id` set); all other tokens become
kind `cli`. A provisional row takes the device's own id (`dv-` + MAC suffix)
the first time that device registers, so the board that was paired with the
v1 server keeps working and ends up with its real id. `last_used` is written
at most once per minute per token.

A device is identified by the registry row that owns its token, never by what
the client claims (protocol 3.2). Device ids are unique per room; pairing a
device id again in the same room replaces the old row and token.

### 2.3 Permission requests

The pending request manager implements the state machine in protocol section
8.3. The rule that shapes it: **the server never decides for the user.** It
returns `allow` or `deny` only when a human pressed a button on a consumer.
Every other outcome (no device, device did not acknowledge within 3 s, device
stopped sending heartbeats, hold time expired, device deferred, shutdown) is
the neutral hook reply, which leaves Claude Code's own permission prompt in
charge.

Claude Code does not report that a prompt was answered in the terminal, so
the manager also cancels requests when a later hook event of the same session
shows that the call is over (`PostToolUse`, `PostToolUseFailure`,
`PostToolBatch`, `Stop`, `StopFailure`, `UserPromptSubmit`, `SessionEnd`) or
when the hook's HTTP response stream closes before a reply was written.

Whether Claude Code shows its own dialog while the hook is pending is not
documented; the working assumption is that a terminal session does not, which
is why the default hold time is 30 s. The server measures it
(`diagnostics.dialogWhilePending`, protocol 8.4) and `devotchi doctor`
reports the result.

Device liveness is therefore observable: each consumer acknowledges a request
with `holding` and repeats it every 4 s while it shows the request. The server
records the last `holding` per device and exposes it (`lastHoldAt`, `holding`,
`online`, `lastSeen`) in `GET /api/devices`, `GET /api/room`, `devotchi status`,
`devotchi devices` and the dashboard.

### 2.4 Hardening

- Bind address `DEVOTCHI_HOST` (default `0.0.0.0` for the standalone server,
  `127.0.0.1` when embedded by `--local` unless `--lan` is given).
- JSON error middleware (no stack traces, no text/plain 500s); every body and
  query validated; duplicated query parameters rejected.
- Body limits (16 KB API, 2 MB hooks), per-route rate limits (protocol 7.8),
  connection limits (200 sockets per process, 8 + 8 per room), 10 s register
  deadline, unregistered sockets are reaped.
- Room-scoped pending requests with 128-bit ids; strict boolean answers.
- Device tokens are consumer-only. Tokens and devices can be listed and
  revoked.
- Pairing codes from `crypto.randomInt`; claim/confirm throttled; rows pruned.
- Structured one-line JSON logs with level and timestamp; tokens, pairing
  codes and message text are never logged.
- Graceful shutdown: pending hooks get the neutral reply, sockets close with
  1001, the database is closed; no forced 5 s exit in the normal path.
- `uncaughtException`/`unhandledRejection` are logged and exit non-zero; port
  in use and invalid config produce one-line errors.

### 2.5 Multi-user mode

`DEVOTCHI_MODE=multi-user` adds accounts. A user owns one room; API tokens
minted with a login session belong to it; anonymous token minting is closed.
Sessions are opaque random tokens stored hashed (revocable by logout).
Registration is `open`, `invite` (requires `DEVOTCHI_INVITE_CODE`) or `closed`
(`DEVOTCHI_REGISTRATION`). Login is rate limited and takes the same time for
unknown and known emails. The server serves a single static page at `/account`
for register, login, token list/revoke, device list/revoke and confirming a
device pairing code. `devotchi login` performs the same flow from the CLI.

### 2.6 Configuration

| Env var | Default | Description |
|---|---|---|
| `DEVOTCHI_HOST` | `0.0.0.0` | Bind address |
| `DEVOTCHI_PORT` | `8080` | Port |
| `DEVOTCHI_MODE` | `single-user` | `single-user` or `multi-user` |
| `DEVOTCHI_DB_PATH` | `~/.devotchi/devotchi.db` | SQLite file |
| `DEVOTCHI_CORS_ORIGINS` | none | Comma-separated allowed browser origins (default: no cross-origin access) |
| `DEVOTCHI_TRUST_PROXY` | `0` | `1` behind a TLS-terminating reverse proxy (uses `X-Forwarded-*`) |
| `DEVOTCHI_ADMIN_SECRET` | unset | Single-user: when set, required to mint tokens |
| `DEVOTCHI_REGISTRATION` | `open` | Multi-user: `open`, `invite`, `closed` |
| `DEVOTCHI_INVITE_CODE` | unset | Multi-user with `invite` |
| `DEVOTCHI_PERMISSION_HOLD_MS` / `_ACK_MS` / `_ALIVE_MS` | `30000` / `3000` / `12000` | Protocol 8.6 |
| `DEVOTCHI_LOG_LEVEL` | `info` | `debug`, `info`, `warn`, `error` |

`DEVOTCHI_JWT_SECRET` is no longer used; if set, the server logs one warning.

### 2.7 Deployment

- `Dockerfile` (multi-stage, `node:24-slim`, non-root user, `/data` volume for
  the database, `HEALTHCHECK` on `/health`) and `docker-compose.yml` with two
  services: the server and Caddy terminating TLS and proxying HTTP and
  WebSocket to it (`DEVOTCHI_TRUST_PROXY=1`).
- The server itself speaks plain HTTP/WS. TLS (`https`/`wss`) is always
  terminated by a reverse proxy. The firmware verifies `wss` against the
  ESP-IDF certificate bundle, so a hosted deployment needs a publicly trusted
  certificate (Caddy obtains one automatically).
- `docs/deploy.md` describes LAN self-hosting (plain `ws://`), the compose
  stack, backups (copy the SQLite file with the server stopped or use
  `VACUUM INTO`), and upgrading.

## 3. Client (`packages/client`)

One CLI, `devotchi`.

| Command | Purpose |
|---|---|
| `start [--local] [--lan] [--server URL] [--port N] [--no-open]` | Dashboard. `--local` embeds the real server (`createServer`) on 127.0.0.1; `--lan` binds it to all interfaces so a device can reach it |
| `mcp` | MCP server on stdio |
| `setup [--dir D] [--user] [--remove] [--check] [--share-text]` | Install, verify or remove the Claude Code hooks (protocol 8.7) |
| `pair <CODE> [--name N]` / `pair --new` | Confirm a code shown by a device / create a code for the device console |
| `devices [list] \| rename <id> <name> \| identify <id> \| rm <id>` | Device registry |
| `status [--json]` | Server reachability, token, peers, mode, device liveness, last request outcome |
| `doctor [--dir D] [--json]` | Ordered checks with a one-line fix each; non-zero exit on failure. Also reports whether Claude Code was seen showing its dialog while a request was pending (hold time advice) |
| `mode [ask\|desk]` | Show or set the room mode |
| `notify <event> [detail] [--mood M] [--urgent]` | Send a notification from a script |
| `run [--name L] -- <cmd...>` | Run a command, notify the device when it finishes, propagate the exit code |
| `sim [--approve all\|none\|defer\|after=MS] [--name N]` | Headless virtual device speaking the real device protocol; prints one JSON line per event |
| `login` / `logout` | Multi-user account login; drop local credentials |
| `config [get\|set\|path]` | Show or change configuration |

### 3.1 Configuration

`~/.devotchi/config.json` (directory overridable with `DEVOTCHI_CONFIG_DIR`),
mode 0600, written atomically. Credentials are stored **per server URL**:

```json
{
  "version": 2,
  "serverUrl": "http://localhost:8080",
  "webPort": 3456,
  "servers": {
    "http://localhost:8080": { "token": "…", "roomId": "5e140e256d56" }
  }
}
```

A v1 file is migrated on first load. A file that cannot be parsed is an error;
it is never overwritten. `DEVOTCHI_SERVER_URL` and `DEVOTCHI_TOKEN` override
the file. A 401 from the server triggers re-provisioning in single-user mode
and an instruction to run `devotchi login` in multi-user mode.

### 3.2 MCP server

Starts the stdio transport first and provisions lazily, so an unreachable
server never prevents startup. Registers as one of possibly several producers.

| Tool | Arguments | Result |
|---|---|---|
| `show_status` | `msg`, `mood?` | `{ok, delivered, consumers, devices}` |
| `notify` | `event`, `mood`, `detail?`, `urgency?` | same |
| `set_mood` | `mood`, `reason?` | same |
| `get_device_state` | none | `GET /api/room` summary: mode, devices with liveness and battery, pending count |

`delivered` is true only when the message was written to an open socket and the
last `peers` message reported at least one consumer. All tools accept all 11
moods. `show_status` without a `mood` leaves the pet's mood unchanged.
Strings are truncated to the protocol byte limits before sending. The last
status and mood are resent after a reconnect.

### 3.3 Dashboard

`devotchi start` serves the web UI from `dist/web` and proxies to the server
with the CLI's token. The dashboard process does not register as a producer.

- Binds 127.0.0.1 only.
- Static files are resolved and must stay inside the web directory; GET/HEAD
  only.
- Every page load embeds a per-launch random secret; `/api/*` requires it in
  `X-Devotchi-Session` and `/ws` in the `s` query parameter. `Origin` and
  `Host` must be `localhost` or `127.0.0.1` on the dashboard port.
- `/ws` proxies to the server as a consumer of kind `dashboard`; `/api/*`
  proxies the device, room, pairing and config endpoints.

Web UI (`web/src`), framework-free TypeScript, type-checked in the build:

```
ws-client.ts     connect, backoff with jitter, heartbeat
store.ts         pure reducer: protocol messages -> view state (unit-tested)
views/           pet panel, request list, devices panel, event log, settings
main.ts          wiring
```

The request list mirrors the device: a queue of pending requests with tool,
summary, risk colour, details and a deadline-based countdown; a request leaves
the list only on `dismiss`; the tab sends `holding` only while visible; the
document title and a browser notification signal a new request in a background
tab. The devices panel shows online state, last seen, last ack, battery, RSSI
and firmware, with rename, identify and unpair. The dashboard is not a pixel
simulator of the device; `devotchi sim` is the behavioural stand-in for
hardware.

### 3.4 Tests

Vitest in every package (`pnpm test` at the root). Unit tests cover the shared
validators, formatter, risk rules and reducers. Integration tests start
`createServer` on an ephemeral port with a temporary database and drive it
over real HTTP and WebSocket, including a `devotchi sim` device. Tests never
use ports 8080 or 3456 and never touch `~/.devotchi`.

---

## 4. Firmware (`firmware/`)

Plain C on ESP-IDF v6.0 for the ESP32-S3-MINI-1-N4R2 (4 MB flash, 2 MB PSRAM).
LVGL 9 draws the UI. No C++ and no C++ dependencies. Coding rules are in
`firmware/CONVENTIONS.md`.

Managed components: `lvgl/lvgl` 9.x, `espressif/esp_websocket_client`,
`espressif/button`, `espressif/network_provisioning`, `espressif/cjson`
(ESP-IDF v6 no longer ships cJSON in core; it is this managed component).

### 4.1 Board facts

The schematic is authoritative. `hardware/README.md` lists BAT_ADC on GPIO1;
that is wrong, the net is on GPIO2.

| GPIO | Function | Level / notes |
|---|---|---|
| 2 | BAT_ADC (ADC1_CH1) | 100k/100k divider: Vbatt = 2 x Vadc; 12 dB attenuation |
| 4 | BTN_APPROVE | Low = pressed; external 10k pull-up + 100 nF; internal pulls off |
| 5 | BTN_REJECT | same |
| 6 | BTN_ACTION | same |
| 7 | not connected | The fourth switch on the PCB is the MCU reset button on EN. GPIO7 floats and is never configured as an input or wake source |
| 8 / 9 / 10 | DISP_RST / DISP_DC / DISP_CS | |
| 11 / 12 | DISP_MOSI / DISP_SCLK | SPI2, write-only, mode 0, 40 MHz |
| 13 | DISP_BL | High = on, PWM, low-side MOSFET |
| 17 | PWR_STDBY | TP4056 open drain, low = charge complete; internal pull-up required |
| 18 | PWR_CHRG | TP4056 open drain, low = charging; internal pull-up required |
| 19 / 20 | USB D- / D+ | Native USB-Serial-JTAG. Never reconfigured |
| 37 | MOTOR | High = on, PWM, low-side MOSFET from +BATT |
| 38 | LED | High = on, PWM |
| 43 / 44 | UART0 TX / RX | Test points only; unused |

Panel settings are kept exactly as in the firmware that is known to display
correctly: ST7789, 320x240, BGR element order, little-endian RGB565, colour
inversion on, `swap_xy` on, `mirror(x=false, y=true)`, no gap.

Consequences of the board design:

- There is no BOOT button. USB-Serial-JTAG auto-reset is the only way to
  reflash, so the firmware never deep-sleeps or light-sleeps while a USB host
  is connected and never within the boot grace period.
- There is no power switch and no power path: the system runs from the battery
  node, and VBUS is not visible to the MCU. External power is inferred from
  the charger status pins.
- "No battery" and "full on USB" cannot be told apart with certainty.

### 4.2 Components

```
src/
  main/              app_main.c, Kconfig.projbuild ("Devotchi" menu), idf_component.yml
  components/
    board/           board_pins.h: every pin, LEDC channel, SPI host in one header
    core/            pure C, no ESP-IDF includes; compiled on the host for unit tests
                       proto      protocol v2 parse/serialize (cJSON)
                       ws_reasm   WebSocket message reassembly and close-code capture
                       text       ASCII sanitising, UTF-8 safe truncation, wrapping helpers
                       url        server URL parsing and derivation
                       pair_code  pairing code validation
                       reqq       pending request queue
                       pet_model  mood, status, last notification
                       pet_pose   pose table per mood, interpolation
                       btn_fsm    click / long press / wake-swallow logic
                       fb_seq     feedback pattern sequencer
                       battery    filter, LiPo curve, charge state
                       power_fsm  idle stages and sleep decisions
                       backoff    reconnect delays with jitter
    storage/         NVS access (namespace devotchi), settings
    display/         esp_lcd ST7789, backlight, LVGL display port, shadow + snapshot framebuffers
    buttons/         GPIO buttons (espressif/button) -> button events
    feedback/        LED and motor PWM, pattern player
    power/           battery ADC, charger pins, panel sleep, light and deep sleep
    net/             wifi, BLE provisioning, WebSocket client, HTTP pairing, TLS config, network worker
    ui/              LVGL lock, screen manager, screens, widgets, pet view
    app/             event queue, application state machine, glue
    console/         serial debug console
host_test/           CMake project: builds src/components/core with gcc and runs Unity tests
tools/               devotchi_serial.py, mock_server.py, e2e scripts
```

`src/`, `host_test/` and `tools/` are directly under `firmware/`.

Dependency direction: `app` depends on everything; `ui` depends on `core`,
`display`; drivers depend on `board` and `core`; `core` depends on nothing but
the C library and cJSON. Drivers never call into `app` or `ui`; they post
events.

### 4.3 Tasks and ownership

| Task | Stack | Prio | Owns |
|---|---|---|---|
| `app` | 6144 | 5 | All application state. The only consumer of the app event queue (depth 32) |
| `ui` | 8192 | 4 | Runs `lv_timer_handler`. All LVGL and all `esp_lcd` panel calls happen here or under `ui_lock()` |
| `net` | 8192 | 4 | Blocking jobs: HTTP (pair start/poll/claim, health check) and stopping/destroying the WebSocket client |
| websocket client | 8192 | 5 | Socket I/O; reassembles messages and posts them to `app` |
| `pwr` | 3072 | 2 | Battery ADC every 2 s, charger pin levels every 1 s (slower while the screen is off) |
| `console` | 4096 | 3 | Reads command lines, runs handlers |
| system event loop | 4096 | | WiFi, IP and provisioning events, translated into app events |
| esp_timer | | | Button callbacks and feedback steps; they only post events or set PWM |

Rules:

- Every asynchronous input becomes an `app_event_t` posted to the app queue:
  button events, WebSocket frames and link changes, WiFi and provisioning
  events, job results, power samples, console requests, a 1 Hz tick. Only the
  `app` task changes application state, chooses the screen, starts network
  operations and plays feedback.
- The `app` task never blocks on the network. In particular it never calls
  `esp_websocket_client_stop()` or `_destroy()`: both wait without a time
  limit for the client task, which can sit in DNS, TCP connect or a TLS
  handshake. Stopping is a job on the `net` task that reports
  `APP_EV_WS_STOPPED` when it is done.
- There is at most one WebSocket client. The app keeps a small lifecycle:
  `WS_IDLE -> WS_RUNNING -> WS_STOPPING -> WS_IDLE`. A client is created only
  in `WS_IDLE`, and `WS_IDLE` is entered only by `APP_EV_WS_STOPPED` (or at
  boot). The library's auto-reconnect is disabled; every reconnect goes
  through this cycle.
- Received messages are copied into one of six 4 KB buffers from a static
  pool (PSRAM) and parsed in the `app` task. With the pool exhausted a
  message is dropped and counted.
- Sending on the WebSocket uses a 1 s timeout
  (`CONFIG_ESP_WS_CLIENT_SEPARATE_TX_LOCK=y`, so a send does not wait for a
  read in progress). Two consecutive failed sends, or a transport error
  reported by the client, are a link loss; one failed send is retried by the
  next heartbeat.
- Screens never block and never touch the network or storage. A user intent
  from a screen is posted as an app event.
- The SPI panel is driven by one task at a time: `esp_lcd` calls (draw,
  sleep, on/off) are made with `ui_lock()` held, and a flush is complete
  before the flush callback returns (it waits for the transfer-done
  interrupt), so no transfer is in flight while the lock is free.

### 4.4 Application state

```
BOOT -> PROVISIONING            no WiFi credentials, or WiFi setup requested
     -> WIFI_CONNECTING <-----> WIFI_FAILED (retrying with backoff, offers WiFi setup)
     -> PAIRING                 no token: flow A (code on screen), or console `pair`
     -> SERVER_CONNECTING       token present, not registered yet
     -> ONLINE                  register_ack received
ONLINE -> OFFLINE               link lost after having been online; pet stays on screen with a banner
any    -> PAIRING               close 4003, or confirmed revocation after repeated HTTP 401
                                (protocol 9.4): erase token + room only
```

A single HTTP 401 on the upgrade never erases the token. The device checks
`GET /health` for protocol 2, counts consecutive 401s, and gives up the token
only after three of them spread over at least 30 s; a server that does not
report protocol 2 is shown as "Incompatible server" and the token is kept.

Independent of the link state the app keeps:

- the **pet model** (mood, status text, project, last notification), updated
  by messages regardless of the visible screen;
- the **request queue** (up to 4 pending permissions with local deadlines);
- the **power stage** (section 4.8);
- persisted **settings**.

Reconnect delays come from `backoff`: WiFi 1, 2, 4 ... 30 s, WebSocket 1, 2, 4
... 60 s, each with +/-20 % jitter, reset on success.

### 4.5 Screens and buttons

LVGL objects are created once per screen and updated in place. A 20 px status
bar occupies the top of every screen except `splash`, `lowbatt`, `identify`;
screens lay out below it. Toasts and banners live on the top layer.

| Screen id | Content |
|---|---|
| `splash` | Name and firmware version |
| `provisioning` | BLE service name `DEVOTCHI-XXXX` and the proof-of-possession code for this session |
| `wifi` | "Connecting to WiFi", SSID, failure reason and retry countdown |
| `connecting` | "Connecting to server", host, last error and retry countdown |
| `pairing` | Pairing code in two groups, `devotchi pair <CODE>` hint, expiry bar, server host, error line |
| `pet` | Page `pet`: project header, animated face, status line. Page `about`: firmware, device id, IP, RSSI, server, link state, last error, uptime, reset reason |
| `approval` | Request card: tool chip coloured by risk, queue position `1/3`, project, summary, details page, countdown bar, button hints |
| `settings` | Rows listed below |
| `identify` | Device name, full screen |
| `lowbatt` | "Battery empty, plug in USB-C" |

Status bar, left to right: battery glyph with percent (plug glyph and `USB`
when no battery is detected, bolt when charging), mode chip (`DESK`), RSSI bars
(crossed out when WiFi is down), link dot (green registered, amber connecting,
red offline).

A short press is delivered on **release** (click) and is suppressed when the
press became a long press (`CONFIG_DEVOTCHI_LONG_PRESS_MS`, default 1000).

| Screen | APPROVE click | REJECT click | ACTION click | ACTION long | Other |
|---|---|---|---|---|---|
| `pet` | Pat | Poke | Next page | Settings | REJECT long: toggle `desk` mode |
| `approval` | Allow (risk low/med); for `high` shows "Hold to approve" | Deny | Next details page | Send to terminal (`permission_defer`) | APPROVE long: allow (required for `high`) |
| `settings` | Row down | Row up | Change / activate | Exit | |
| `pairing` | | | New code | Settings | |
| `wifi`, `connecting`, `provisioning` | (`wifi` after repeated failures: start WiFi setup) | | | Settings | |

Settings rows: Brightness (25/50/75/100), Screen timeout (15/30/60/120 s),
Deep sleep (10/20/60 min/never), Haptics (off/gentle/full), LED (on/off), Mode
(ask/desk), WiFi setup, Unpair, Factory reset, About. WiFi setup, Unpair and
Factory reset need a second ACTION click on a confirmation row within 5 s.
Unpair erases token and room id only. Factory reset erases the `devotchi`
namespace and calls `esp_wifi_restore()`.

A pending request takes over the screen from any state that can show it
(`pet`, `settings`); requests are answered first-in first-out.

### 4.6 Networking

- **Server URL**: NVS `server_url`, falling back to
  `CONFIG_DEVOTCHI_DEFAULT_SERVER_URL`. Set from the console (`server <url>`)
  or during BLE provisioning (custom endpoint `custom-data`, payload
  `server=<url>`). Shown on the `pairing`, `connecting` and About screens.
- **WebSocket**: `esp_websocket_client` to `<base>/ws` with
  `Authorization: Bearer <token>` (the token is never placed in the URL) and
  a 4096-byte buffer. `core/ws_reasm` rebuilds messages: the library reports
  `payload_offset`/`payload_len` per WebSocket *frame*, so a message is the
  concatenation of a text frame and its continuation frames up to the frame
  with `fin` set; ping/pong frames in between are ignored. The server's close
  code arrives as a data event with opcode 8 (two bytes, big-endian) and is
  attached to the link-down event; the HTTP status of a failed upgrade comes
  from the client's error event. Messages are parsed once with cJSON and
  dispatched on `type`.
- **TLS**: one helper (`tls_cfg`) configures both the WebSocket client and
  the HTTP client: the ESP-IDF certificate bundle by default, or one extra CA
  embedded at build time (`CONFIG_DEVOTCHI_TLS_EXTRA_CA` +
  `CONFIG_DEVOTCHI_TLS_EXTRA_CA_PATH`) for a private server.
- **Pairing**: flow A by default (protocol 9.2), run as HTTP jobs on the `net`
  task; flow B from the console. Errors are shown on the pairing screen.
- **WiFi provisioning**: BLE (NimBLE) through `network_provisioning`, security
  1, service name `DEVOTCHI-` + last two MAC bytes, a random 8-character
  proof-of-possession generated per session and shown on screen. Credentials
  are written to NVS only after the station got an IP address. A failed
  attempt resets the provisioning state machine so the phone can retry.
  While provisioning runs, the provisioning manager owns WiFi connection
  attempts; the app does not schedule retries of its own.
  Bluetooth memory is released when provisioning is not running.
  Re-provisioning (Settings > WiFi setup, console `wifi-setup`) reboots into
  provisioning and keeps the old credentials until new ones work.
- If NVS could not be initialised, WiFi is started with its own NVS use
  turned off, and a WiFi init failure is an application state shown on the
  About page, not an abort.
- Secrets (token, PSK, pairing codes, proof-of-possession) are never logged.

### 4.7 NVS compatibility

Partition `nvs` at 0x9000, size 0x6000, unencrypted: unchanged. The firmware
never erases the partition; if `nvs_flash_init` fails it runs with defaults,
shows the error on the About page and reports `"nvs":"error"` in `state`.

Namespace `devotchi`:

| Key | Type | Status | Meaning |
|---|---|---|---|
| `wifi_ssid` | string | existing, kept | Station SSID |
| `wifi_psk` | string | existing, kept | Station passphrase |
| `token` | string | existing, kept | Device API token |
| `room_id` | string | existing, kept | Room id (informational) |
| `server_url` | string | existing (unused until now) | Base URL, `ws://` or `wss://` |
| `pair_code` | string | existing, legacy | Never read or written again; left in place |
| `bk_token`, `bk_room`, `bk_url` | string | new | Backup of the oldest known pairing (see below and 4.10). `bk_url` holds the raw `server_url` value; an empty string means the key was absent (Kconfig default in effect) |
| `dev_name` | string | new | Device name from the server |
| `s_bright` | u8 | new | Brightness percent, default 75 |
| `s_dim_s` | u16 | new | Screen timeout base T in seconds, default 30 |
| `s_deep_min` | u16 | new | Deep sleep after N minutes on battery, 0 = never, default 20 |
| `s_haptic` | u8 | new | 0 off, 1 gentle, 2 full; default 2 |
| `s_led` | u8 | new | 0/1, default 1 |
| `s_batt` | u8 | new | Battery presence: 0 auto, 1 present, 2 absent; default 0 |
| `prov_req` | u8 | new | 1 = enter provisioning on next boot; cleared when provisioning starts |

Token and room id are written together, `room_id` first and `token` last.
NVS has no multi-key transaction; `room_id` is informational and never used
for authentication, so a reset between the two writes is harmless.

Automatic backup: before the firmware overwrites or erases `token`,
`room_id` **or `server_url`**, and a token exists and `bk_token` is empty, it
copies `token`, `room_id` and the raw `server_url` value to the `bk_*` keys.
`pair-restore` writes them back; when `bk_url` is empty it erases the
`server_url` key so the Kconfig default applies again. `state` reports
`serverSource` (`nvs` or `default`) so a restore can be checked.

esp_wifi keeps its own copy of the station config in its NVS namespace; that
is left as it is.

### 4.8 Idle power stages

`power_fsm` (pure C, host-tested) decides the stage from timestamps and
conditions; the `power` component executes it.

| Stage | Display | Radio / CPU |
|---|---|---|
| `active` | Backlight at the brightness setting | WiFi power save MIN_MODEM. CPU held at full speed |
| `dim` | Backlight 10 % | same |
| `off` | Panel in sleep (SLPIN), backlight 0, LVGL task parked | Link kept alive. CPU may scale down. On battery: WiFi MAX_MODEM and automatic light sleep; on USB: MIN_MODEM, no light sleep |
| deep sleep | Everything off | Wake on any of the three buttons (ext1, any low). Cold boot |

Timeouts, with T = the Screen timeout setting (default 30 s):

| Transition | On battery | On external power |
|---|---|---|
| `active` -> `dim` | T | 2 x T |
| -> `off` | 2 x T | 10 x T |
| -> deep sleep | Deep sleep setting (default 20 min, or never) | `CONFIG_DEVOTCHI_PWR_USB_DEEP_MIN` (default 0 = never) |

Activity:

- **User activity** resets all timers and returns to `active`: any button
  event, an incoming `permission`, a `notify` with urgency >= 1, `identify`,
  console `btn`/`wake`, a change of the *debounced* external-power state, and
  these link transitions only: entering PAIRING or PROVISIONING, the first
  ONLINE after boot, and ONLINE -> OFFLINE (once per outage).
- Reconnect attempts, backoff timers and every other link state change are
  **not** activity. A device whose WiFi or server is unreachable still dims,
  turns its screen off and reaches deep sleep.
- **Agent traffic** (`status`, `mood`, `notify` of any urgency) does not wake
  the screen but restarts the deep-sleep timer, so the device stays reachable
  while an agent is working.
- Telemetry, heartbeats and `peers` are not activity.

External power is debounced: it is present when CHRG or STDBY was seen low
within the last 2 s. A charger without a battery pulses CHRG; the pulses must
not look like plugging and unplugging.

CPU frequency and locks (ESP-IDF power management, DFS 40..160 MHz):

- An `ESP_PM_CPU_FREQ_MAX` lock is held in `active` and `dim`, so rendering,
  JSON parsing and the console always run at full speed and frame times are
  comparable. It is released in `off`.
- An `ESP_PM_NO_LIGHT_SLEEP` lock is held unless the stage is `off`, the
  device is on battery, no USB host is connected and
  `CONFIG_DEVOTCHI_PWR_LIGHT_SLEEP` is set.
- The buttons use the button library's power-save mode: its polling timer
  stops while no button is pressed and a GPIO level interrupt (which is also
  the light-sleep wake source) restarts it. Nothing else configures GPIO
  wake-up for the button pins.
- In `off` the `ui` task waits for a notification instead of running LVGL
  timers, and the `pwr` task samples every 10 s.

Rules:

- A button event in `dim` or `off` (and the press that woke the device from
  deep sleep) only wakes the screen. It is not delivered to the screen, and
  button events are ignored until 300 ms after the wake and until all buttons
  are released.
- While the request queue is not empty the stage never goes beyond `dim`, and
  the device never deep-sleeps.
- No deep sleep and no light sleep while a USB host is connected
  (`usb_serial_jtag_is_connected()`), during provisioning, or within
  `CONFIG_DEVOTCHI_PWR_BOOT_GRACE_S` (default 60) after boot. One exception to
  the boot grace is described under low battery below.
- Before deep sleep the WebSocket stop is requested and awaited for at most
  3 s, the panel is put to sleep, and backlight, motor, display reset and
  chip select are held at their inactive levels.
- Wake from deep sleep skips the splash screen, shows the pet immediately,
  reconnects WiFi using the stored channel and BSSID, and restores state from
  the server's replay after `register_ack`. The wake cause is read with
  `esp_sleep_get_wakeup_causes()`.

Low battery protection (only when discharging and a battery is present):

| Condition | Action |
|---|---|
| Filtered voltage below `CONFIG_DEVOTCHI_BATT_LOW_MV` (3500) | One toast, amber battery glyph, telemetry `alert: battery_low` |
| Below `CONFIG_DEVOTCHI_BATT_CRIT_MV` (3300) for 30 s | `lowbatt` screen for 5 s, final telemetry with `alert: battery_critical`, deep sleep, and a flag in RTC memory |

A button wake from a battery-protection sleep (RTC flag set) takes a burst of
five ADC readings before WiFi starts. If the median is still below the
critical threshold and no external power is present, the device shows
`lowbatt` for 3 s and sleeps again **without** waiting for the boot grace
period; otherwise it clears the flag and boots normally. The USB-host guard
still applies, and external power always cancels it, so a device on a cable
can always be reflashed.

### 4.9 Battery

- ADC1 channel 1, 12 dB, curve-fitting calibration; 32 averaged reads after 4
  discarded; x2 divider; calibration factor `CONFIG_DEVOTCHI_BATT_CAL_PERMILLE`.
- Filter: median of 5 samples, then an exponential filter.
- Percent from a LiPo open-circuit table with linear interpolation.
- Charger pins: levels are sampled once per second. CHRG edges are counted by
  a GPIO any-edge interrupt (capped at 50 per second) into a ten-slot,
  ten-second window, because a charger without a battery can pulse CHRG faster
  than any polling rate would show. External power = CHRG or STDBY seen low
  within the last 2 s.

| State | Condition | Displayed |
|---|---|---|
| `absent` | Setting says absent, or (auto) external power and at least 3 CHRG edges in 10 s | Plug glyph, `USB`, no percent |
| `full` | STDBY low, or CHRG low with at least 4150 mV for 10 min | 100 %, bolt |
| `charging` | CHRG low | Percent that never decreases, bolt |
| `discharging` | No external power | Percent that never increases |

On the bench board (USB, battery disconnected) the charger reports CHRG low
steadily at about 4.1 V, which is indistinguishable from a nearly full battery
on charge unless CHRG is seen toggling. The battery presence setting
(`s_batt`, console `batt present`) overrides the heuristic.

### 4.10 Debug console

The console runs on the native USB-Serial-JTAG port (the board's only
connector) and exists so that automated agents can drive and observe a real
device.

Framing:

- Host to device: one command per line, terminated by `\n` (`\r` ignored), at
  most 1024 bytes. No echo, no prompt, no escape sequences.
- Device to host: every console line starts with `@@`. Log lines never do.
  All output to the port (console lines and log lines) goes through one
  writer under one mutex, a whole line at a time, so a console line never
  appears in the middle of a log line.
- Every command produces zero or more payload lines followed by exactly one
  terminator line:
  - `@@OK <command>`
  - `@@ERR <command> <code> <message>` with code `unknown_command`, `bad_args`,
    `invalid_state`, `busy` or `failed`
- Payload lines: `@@JSON <one JSON object>`, or the screenshot block.
- `@@READY fw=<version> proto=2 reset=<reason>` is printed once when the
  console starts accepting commands after boot. It is a best-effort marker:
  after a reset or a deep-sleep wake the USB port re-enumerates and the line
  is usually gone before a host has reopened it. Tools detect readiness by
  sending `version` until it is answered; `version` and `state` carry the
  reset reason and uptime.
- `@@EVT <one JSON object>` lines are emitted asynchronously on application
  state, screen, link, request queue and power stage changes, for example
  `@@EVT {"evt":"screen","id":"approval"}`.

Screenshot block (`screenshot`): the pixels last flushed to the panel,
320x240, RGB565 little-endian, row-major from the top-left. The flush
callback mirrors every flushed area into a shadow framebuffer. The command
forces a refresh, copies the shadow into a second snapshot buffer while
holding the UI lock, computes the CRC of the snapshot, releases the lock and
only then sends the snapshot. Animations that keep drawing during the
transfer therefore cannot tear the image or invalidate the CRC.

```
@@SCREENSHOT BEGIN w=320 h=240 fmt=RGB565LE enc=base64 bytes=153600 crc32=1a2b3c4d
@@D <76 base64 characters = 57 bytes>
...
@@SCREENSHOT END lines=2695
@@OK screenshot
```

`crc32` is the IEEE CRC-32 of the raw 153600 bytes (equal to Python
`zlib.crc32`), 8 lowercase hex digits. Lines that do not start with `@@D `
inside the block are not part of the image.

Commands:

| Command | Effect |
|---|---|
| `help` | List commands |
| `version` | `@@JSON {"fw","idf","proto","deviceId","model","built","reset","uptimeS"}` |
| `state` | One `@@JSON` line: app state, screen, WiFi, WebSocket, server URL, pairing, mode, request queue, pet, power stage, battery, heap, UI frame times, NVS status, settings. Never contains the token or the passphrase |
| `screen` | `@@JSON {"id","page","texts":[...],"mood","statusBar":{...},"request":{...}}`: the current screen in machine-readable form |
| `screenshot` | Screenshot block |
| `btn <approve\|reject\|action> <press\|long>` | Inject a click or long press through the same path as a hardware button, including the wake rule |
| `wake` | User activity without a button |
| `msg <json>` | Handle a protocol message as if it had been received from the server |
| `pair <CODE>` | Claim a flow B code on the configured server; stores token and room on success |
| `server [<url>]` | Show, or set and persist, the server base URL (`ws://` or `wss://`), then reconnect |
| `wifi [<ssid> <pass>]` | Show WiFi status, or try credentials; they are saved only after an IP address was obtained, otherwise the previous ones are restored |
| `wifi-setup` | Reboot into BLE provisioning (old credentials are kept until new ones work) |
| `reset-pairing` | Erase token and room id only |
| `pair-backup [--force]` / `pair-restore` | Copy token, room id and server URL to / from the backup keys |
| `preview <screen>\|off` | Show a screen with sample data without changing state: `pairing`, `provisioning`, `approval-low`, `approval-high`, `lowbatt`, `identify`, `settings` |
| `set [<key> <value>]` | List or change a setting |
| `fb list` / `fb play <pattern>` / `fb stop` | Feedback patterns; each step is reported as `@@EVT {"evt":"fb",...}` |
| `pet mood <mood>` / `pet pose` / `pet freeze on\|off` | Pet rig control; `freeze` stops idle animation so screenshots are reproducible |
| `batt` / `batt inject <mv> <chrg> <stdby>` / `batt inject off` / `batt present auto\|yes\|no` | Battery readings and overrides |
| `power` / `power stage <active\|dim\|off>` / `power dry-run on\|off` / `power sleep deep <wakeS> --force` | Power stage inspection and control. With dry-run on (default while a USB host is connected) a deep sleep decision is reported instead of executed |
| `log <none\|error\|warn\|info\|debug> [tag]` | Log level |
| `tasks` | Stack high-water marks |
| `reboot` | Restart |
| `factory-reset --yes-erase-wifi-and-pairing` | The only command that erases WiFi credentials. Never used by automation |

Host tool: `firmware/tools/devotchi_serial.py` (Python standard library;
Pillow for PNG) sends commands, waits for the terminator, prints JSON, waits
for `@@EVT` conditions, and saves screenshots as PNG after checking byte
count and CRC (retrying up to three times). It opens the port with plain
`os.open` and `termios` (raw mode, HUPCL cleared) and never changes the modem
lines, because the DTR/RTS sequence a serial library applies on open resets
the chip through the USB-Serial-JTAG bridge. A deliberate reset is a separate
option.

### 4.11 Flash layout

| Name | Type | Offset | Size |
|---|---|---|---|
| `nvs` | data/nvs | 0x9000 | 0x6000 |
| `phy_init` | data/phy | 0xf000 | 0x1000 |
| `factory` | app | 0x10000 | 3 MB |

The table is unchanged. OTA is not implemented: two OTA slots in 4 MB allow at
most about 1.9 MB per image, and the update path, rollback and signing are a
separate project. The application is built for size and kept below 1.9 MB so
that a later move to `otadata + ota_0 + ota_1` (which keeps `nvs` and
`phy_init` where they are and needs one USB flash) stays possible.

Flashing writes the bootloader (0x0), the partition table (0x8000) and the
application (0x10000). It never touches 0x9000..0xFFFF. `erase-flash` and
`--erase-all` are never used.

### 4.12 Host tests

`firmware/host_test` is a plain CMake project that compiles `components/core`
with the host gcc and runs the tests with `ctest`. It takes cJSON from
`managed_components` and Unity from `$IDF_PATH` when they exist, and
otherwise downloads pinned releases of both with CMake `FetchContent`, so it
builds on a fresh clone and in CI without ESP-IDF. Everything with
logic and no hardware lives in `core` so it is covered there: protocol,
WebSocket reassembly, text, URL parsing, request queue, button logic,
feedback sequencing, battery maths, power stages, pet poses, backoff.

---

## 5. End-to-end flows

### 5.1 Permission request

```
Claude Code is about to ask for permission to run "rm -rf dist"
  -> PermissionRequest hook: POST /hook/event
  -> server formats the tool call, creates request R, sends `permission` to all consumers
  -> device wakes its screen, vibrates, shows the card, sends holding{R} and repeats it every 4 s
  -> user presses APPROVE
  -> device sends permission_response{R, granted:true}
  -> server replies allow to the hook and sends dismiss{R, answered, allow} to all consumers
```

If no consumer acknowledges within 3 s, the device stops sending heartbeats,
the device defers, or the hold time (30 s by default) ends, the hook gets
`{}` and Claude Code's own prompt decides. If the call is settled in the
terminal first, the server notices it from the next hook event of that
session and dismisses the request on the device.

### 5.2 Status

```
PostToolUse hook -> POST /hook/event -> status{msg, mood:working, project} -> consumers
MCP show_status  -> producer WebSocket -> status -> consumers
```

### 5.3 First-time setup

1. `devotchi start --local --lan` (or a running server) and `devotchi setup`.
2. Power the device; provision WiFi over BLE with the code on its screen.
3. The device shows a pairing code; run `devotchi pair <CODE>`.

---

## 6. Not built

A phone app, BLE as a message transport, OTA updates, mDNS discovery, a pixel
simulator of the device in the dashboard, activity history and auto-approval
modes are not part of the system. They are tracked in `TODO.md` / `IDEAS.md`.
