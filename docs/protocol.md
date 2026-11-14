# Devotchi Protocol v2

This document is the contract between the server (`packages/server`), the PC
client (`packages/client`, including the dashboard and `devotchi sim`) and the
device firmware (`firmware/`). Where an implementation disagrees with this
document, the implementation is wrong.

Protocol version: **2**. Section 12 lists every change from v1 and which
implementation has to change.

---

## 1. Roles and topology

```
Claude Code --(HTTP hooks)--> server <--(WebSocket)--> consumers (device, dashboard, sim)
MCP / CLI ----(WebSocket producer)--^
```

- A **room** is the unit of routing. Every API token belongs to exactly one room.
- A **producer** is a PC-side process that emits pet state (`devotchi mcp`).
  A room may have several producers at once.
- A **consumer** displays state and answers permission requests. Consumer kinds:
  `device` (firmware), `dashboard` (browser through the dashboard proxy), `sim`
  (`devotchi sim`).
- The server never forwards consumer messages to producers. Producers receive
  only `register_ack`, `ping`, `peers` and `error`.

## 2. Common rules

- Transport is JSON objects in WebSocket **text** frames, one object per frame,
  UTF-8. Binary frames are answered with `error INVALID_MESSAGE` and ignored.
- Every message has a string `type`. Receivers MUST ignore unknown `type`
  values and unknown fields (forward compatibility). The firmware must not log
  them above DEBUG.
- **Sizes are UTF-8 bytes**, never characters or UTF-16 units.
  - Every frame the server sends is at most **2048 bytes**.
  - The server accepts inbound frames up to 2048 bytes. A frame of 2049..8192
    bytes is dropped and answered with `error MESSAGE_TOO_LARGE`; the socket
    stays open. A frame above 8192 bytes closes the socket with code 1009.
  - The firmware receive path reassembles a message that arrives in several
    reads or in several WebSocket continuation frames, up to 4096 bytes, and
    drops (and counts) anything larger.
- **String limits.** When a producer or hook supplies a string longer than its
  limit, the server truncates it on a code point boundary and appends nothing.
  It does not reject the message. Wrong types or missing required fields are
  rejected with `error INVALID_MESSAGE` and the message is dropped.
- **Text for devices.** The device fonts are ASCII only. The server restricts
  the strings it *generates* for `permission` (`tool`, `summary`, `details`,
  `project`) to printable ASCII (0x20..0x7E) plus `\n`, replacing anything else
  with `?`. Producer text (`status.msg`, `notify.detail`, `mood.reason`) is
  forwarded as UTF-8; the firmware replaces every non-ASCII code point with one
  `?` before display.
- **Timestamps.** `timestamp` is an ISO 8601 UTC string set by the server on
  every message it emits or forwards (a producer-supplied value is overwritten).
  Consumer-to-server messages carry no timestamp; devices have no trusted clock.
- **Request ids** are 32 lowercase hex characters (128 random bits).
- **Booleans are strict.** `true`/`false` only; any other JSON type is invalid.

## 3. WebSocket transport

### 3.1 URL and authentication

```
ws(s)://<host>[:<port>]/ws
```

`/ws` is the canonical path. The server accepts an upgrade on any path (v1
firmware used `/`); new clients must use `/ws`.

The API token is presented at upgrade time, in order of preference:

1. `Authorization: Bearer <token>` request header (firmware, CLI, MCP, sim, the
   dashboard proxy).
2. `?token=<token>` query parameter (legacy; kept for v1 hook URLs and tools
   that cannot set headers).

A missing or invalid token gets `HTTP/1.1 401 Unauthorized` with body
`{"error":"Invalid token","code":"unauthorized"}`. A single 401 is **not** a
revocation for the firmware; section 9.4 defines when a device gives up its
token.

### 3.2 register

The first frame a client sends, within 10 seconds of the upgrade:

```json
{"type":"register","role":"consumer","proto":2,
 "client":{"kind":"device","deviceId":"dv-e356e4","model":"devotchi-v1","version":"0.2.0"}}
```

```json
{"type":"register","role":"producer","proto":2,
 "client":{"kind":"mcp","version":"0.2.0","project":"devotchi"}}
```

| Field | Type | Required | Notes |
|---|---|---|---|
| `role` | `"producer"` \| `"consumer"` | yes | |
| `proto` | integer | yes | Must be `2`. Absent means v1. |
| `client.kind` | string | yes | Consumers: `device`, `dashboard`, `sim`. Producers: `mcp`, `cli`. |
| `client.deviceId` | string | device, sim | Must match `^dv-[0-9a-f]{6}$` (last 3 MAC bytes) or `^sim-[a-z0-9-]{1,12}$`. See "Device identity" below for how it is used. |
| `client.model` | string | no | Max 24 bytes. `devotchi-v1` for the first board. |
| `client.version` | string | no | Max 24 bytes. Software version of the client. For a device this is its firmware version; the server stores it as `Device.fw`. There is no `client.fw` field. |
| `client.project` | string | no | Producers only: basename of the process cwd, max 32 bytes. |

Rules:

- The room comes from the token. v1 fields `roomId` and `token` in `register`
  are ignored.
- A token of kind `device` (section 7.1) may only register as `consumer` with
  `client.kind` `device` or `sim`. `client.kind` `device` requires a `device`
  token. A `cli` token may register as `producer` (`mcp`, `cli`) or as
  `consumer` with kind `dashboard` or `sim`. A violation gets
  `error NOT_ALLOWED` and close 1008.
- A missing or malformed `client.deviceId` where one is required gets
  `error INVALID_MESSAGE` and close 1008.
- `proto` other than 2 (or absent) gets `error UNSUPPORTED_PROTO` and close 4002.
- No `register` within 10 s, or any other first message, gets
  `error INVALID_MESSAGE` and close 1008.
- A second `register` on a registered socket gets `error INVALID_MESSAGE` and
  is ignored.
- Limits: 8 producers and 8 consumers per room. Excess registrations get
  `error ROOM_FULL` and close 1008.

**Device identity.** The server, not the client, decides what a consumer is
called in `dismiss.by`, in `holders`, in telemetry storage and in the device
registry:

| Socket | Identity used by the server |
|---|---|
| `device` token (kind `device` or `sim`) | The id of the registry row that owns the token (section 7.5). `client.deviceId` is ignored, with one exception: a registry row created by the v1 database migration adopts a valid `client.deviceId` on its first `register`, if no other device in the room has that id |
| `cli` token, kind `sim` | `client.deviceId` as sent. Not in the registry, not listed by `GET /api/devices`; its telemetry is kept in memory only |
| `cli` token, kind `dashboard` | The literal `dashboard` |

### 3.3 register_ack

```json
{"type":"register_ack","role":"consumer","roomId":"5e140e256d56","proto":2,
 "serverVersion":"0.2.0","serverTime":1794668400000,"mode":"ask",
 "peers":{"producers":1,"consumers":2,"devices":1},
 "device":{"id":"dv-e356e4","name":"desk"}}
```

| Field | Type | Notes |
|---|---|---|
| `role` | string | Echo of the accepted role. |
| `roomId` | string | 12 hex characters. |
| `proto` | integer | Always 2. A client that sees another value disconnects and reports "server version mismatch". |
| `serverVersion` | string | Server package version. |
| `serverTime` | integer | Epoch milliseconds, display-only (the device may set its clock from it). |
| `mode` | `"ask"` \| `"desk"` | Current room mode (section 6). |
| `peers` | object | Same shape as the `peers` message. |
| `device` | object | Only for `device` tokens: registry id and display name. |

Immediately after `register_ack` to a **consumer**, the server replays room
state as ordinary messages, in this order:

1. the last `status`, without its `mood` field, if newer than 30 minutes;
2. `{"type":"mood","mood":<room mood>}` (section 4.5), if the room mood was
   set less than 30 minutes ago;
3. every pending `permission` with `timeout` set to the remaining hold time.
   A request with less than 1000 ms remaining is not replayed.

### 3.4 Heartbeat

- The server sends `{"type":"ping"}` to every registered socket every 30 s.
- A client answers each `ping` with `{"type":"pong"}` immediately.
- **Any** inbound frame counts as liveness. The server terminates a socket from
  which it received no frame for 70 s.
- A client may send `ping`; the server answers `pong`.
- A client should treat 70 s without any server frame as a dead link and
  reconnect.
- WebSocket-level ping/pong frames are allowed and answered but are not what
  the server's liveness check counts.

### 3.5 Rate limits on a socket

- Producer content messages (`status`, `notify`, `mood`): 60 per rolling minute
  per socket. Excess messages are dropped with `error RATE_LIMITED`.
- System messages are not counted against that budget but are bounded
  individually. `holding`: a token bucket of 8 that refills at 2 per second
  (a burst of acks for several new requests must never be dropped).
  `telemetry` 1/s, `set_mode` 1/s. Excess is dropped with
  `error RATE_LIMITED`.

### 3.6 Close codes

| Code | Sent by | Meaning | Client action |
|---|---|---|---|
| 1000 | either | Normal close | Reconnect if it still wants the link |
| 1001 | server | Server shutting down | Reconnect with backoff |
| 1008 | server | Protocol violation (bad or late `register`, `NOT_ALLOWED`, `ROOM_FULL`) | Reconnect with backoff, report |
| 1009 | server | Frame larger than 8192 bytes | Reconnect with backoff |
| 4002 | server | Unsupported protocol version | Report "update required", retry no faster than every 5 min |
| 4003 | server | Token revoked or device unpaired | Do not reconnect. Device erases token and room id only and returns to pairing (section 9.4) |

Code 4001 ("replaced by new producer") no longer exists.

### 3.7 Who may send what

| `type` | producer -> server | consumer -> server | server -> producer | server -> consumer |
|---|---|---|---|---|
| `register` | yes | yes | | |
| `register_ack`, `ping`, `error`, `peers` | | | yes | yes |
| `pong` | yes | yes | yes | yes |
| `ping` | yes | yes | | |
| `status`, `notify`, `mood` | yes | | | yes |
| `permission`, `dismiss`, `mode`, `identify` | | | | yes |
| `holding`, `permission_response`, `permission_defer`, `set_mode`, `telemetry` | | yes | | |

Anything outside this table gets `error NOT_ALLOWED` and is dropped. In
particular a producer cannot send `permission`, `register_ack` or `dismiss`,
and a consumer cannot send `status`.

---

## 4. Messages

### 4.1 `error` (server -> client)

```json
{"type":"error","code":"INVALID_MESSAGE","message":"status.msg must be a string"}
{"type":"error","code":"UNKNOWN_REQUEST","message":"not pending","id":"9f2c4e6a8b0d1f3a5c7e9b1d3f5a7c9e"}
```

`id` is optional. The server sets it when the rejected message was a
`permission_response` or `permission_defer` that carried a well-formed id
(`UNKNOWN_REQUEST`, or `INVALID_MESSAGE` for a non-boolean `granted`), so a
consumer with several queued requests knows which one was refused. Clients
must work without it.

| Code | Meaning |
|---|---|
| `INVALID_MESSAGE` | Not JSON, no `type`, wrong field type, missing required field, binary frame |
| `MESSAGE_TOO_LARGE` | Frame of 2049..8192 bytes |
| `RATE_LIMITED` | Section 3.5 |
| `NOT_ALLOWED` | Role or token kind may not send this type |
| `UNKNOWN_REQUEST` | `permission_response`/`permission_defer` for an id that is not pending in this room |
| `UNSUPPORTED_PROTO` | `register.proto` is not 2; followed by close 4002 |
| `ROOM_FULL` | Section 3.2; followed by close 1008 |

`INVALID_PAIRING_CODE` and `NO_PRODUCER` no longer exist.

### 4.2 `peers` (server -> every client in the room)

Sent whenever a client registers or disconnects.

```json
{"type":"peers","producers":1,"consumers":2,"devices":1}
```

`devices` counts registered consumers of kind `device` or `sim`.

### 4.3 `status` (producer or hook -> consumers)

```json
{"type":"status","msg":"Edit src/app.c","mood":"working","sid":"3fa9c1",
 "project":"devotchi","timestamp":"2026-11-14T14:30:00.000Z"}
```

| Field | Type | Required | Limit | Notes |
|---|---|---|---|---|
| `msg` | string | yes | 200 bytes | What the agent is doing |
| `mood` | Mood | no | | Absent means **keep the current mood**. There is no default (v1 defaulted to `working`) |
| `sid` | string | no | 6 hex | Short session id (section 8.2) |
| `project` | string | no | 32 bytes | Session project label |

The server remembers the last `status` per room for replay. A `status` that
carries a `mood` also sets the room mood (section 4.5).

### 4.4 `notify` (producer, hook or REST -> consumers)

```json
{"type":"notify","event":"tests_failed","mood":"sad","detail":"3 of 47 failing",
 "urgency":2,"source":"ci","project":"devotchi","timestamp":"2026-11-14T14:30:00.000Z"}
```

| Field | Type | Required | Limit | Notes |
|---|---|---|---|---|
| `event` | string | yes | 40 bytes, `[a-z0-9_]+` | See section 5.2 |
| `mood` | Mood | yes | | Mood the pet settles into after the reaction |
| `detail` | string | no | 300 bytes | |
| `urgency` | integer 0..3 | no | | Default 1. 0 silent (mood only), 1 toast, 2 toast + haptic, 3 sticky banner + haptic until a button is pressed |
| `source` | string | no | 32 bytes | Origin label for non-agent notifications |
| `sid`, `project` | string | no | as `status` | |

### 4.5 `mood` (producer -> consumers)

```json
{"type":"mood","mood":"anxious","reason":"Long build running","timestamp":"..."}
```

`mood` required (Mood); `reason` optional, 200 bytes.

**Room mood.** The server keeps one current mood per room: the `mood` of the
last message it sent to consumers that carried one (`mood`, `notify`, or a
`status` with a `mood`), with the time it was set. It is what replay sends
(section 3.3) and what the hook dispatcher consults (section 8.5). Consumers
apply the same rule: the pet's mood is the mood of the last message that
carried one.

### 4.6 `permission` (server -> consumers)

Created only by the server from a `PermissionRequest` hook (section 8.3).

```json
{"type":"permission","id":"9f2c4e6a8b0d1f3a5c7e9b1d3f5a7c9e","tool":"Bash",
 "summary":"rm -rf dist","details":"Remove build output\n$ rm -rf dist",
 "risk":"high","sid":"3fa9c1","project":"devotchi","timeout":30000,
 "timestamp":"2026-11-14T14:30:00.000Z"}
```

| Field | Type | Required | Limit | Notes |
|---|---|---|---|---|
| `id` | string | yes | 32 hex | |
| `tool` | string | yes | 40 bytes | Display name of the tool (`Bash`, `Edit`, `github: create_issue`) |
| `summary` | string | yes | 200 bytes | One-line human summary |
| `details` | string | no | 600 bytes | Preformatted lines separated by `\n` |
| `risk` | `"low"` \| `"med"` \| `"high"` | yes | | A hint only. It never decides anything |
| `sid`, `project` | string | no | | |
| `timeout` | integer | yes | | **Remaining** hold time in ms at the moment of sending, always >= 1000. A consumer computes its local deadline as receipt time + `timeout` |

The v1 field `action` no longer exists. The serialized frame is at most 2048
bytes; the server shortens `details` further if JSON escaping would exceed it.

### 4.7 `holding` (consumer -> server): delivery ack and liveness heartbeat

```json
{"type":"holding","ids":["9f2c4e6a8b0d1f3a5c7e9b1d3f5a7c9e"]}
```

- `ids`: 1..8 request ids this consumer currently presents to a human.
- A consumer sends `holding` when it has accepted a `permission` and made it
  visible (the first `holding` naming an id is the delivery ack), and then
  **every 4 seconds** while it still holds at least one request, listing all
  of them.
- The ack is sent **100 ms after the newest unacknowledged `permission`
  arrived** (a debounce, restarted by each new arrival, never later than
  500 ms after the first unacknowledged one), so that several requests
  arriving together are acknowledged by one frame naming all of them.
- "Visible" means: the device has the request on screen or in its on-device
  queue with the screen awake; the dashboard tab is in the foreground
  (`document.visibilityState === "visible"`); a sim always.
- A consumer that cannot present a request (queue full, hidden tab) does not
  name it. It may still answer it.
- Unknown ids are ignored silently.

### 4.8 `permission_response` (consumer -> server)

```json
{"type":"permission_response","id":"9f2c4e6a8b0d1f3a5c7e9b1d3f5a7c9e","granted":true}
```

`granted` must be a JSON boolean. Anything else is `INVALID_MESSAGE` and the
request stays pending. Accepted only from a consumer in the request's room; the
first valid response wins; a response for an id that is not pending gets
`error UNKNOWN_REQUEST` with the `id` (section 4.1).

### 4.9 `permission_defer` (consumer -> server)

```json
{"type":"permission_defer","id":"9f2c4e6a8b0d1f3a5c7e9b1d3f5a7c9e"}
```

"Do not decide here; hand this request to the terminal now." The server
resolves the hook with the neutral reply and dismisses the request everywhere
with reason `released`.

### 4.10 `dismiss` (server -> consumers)

Sent to **every** consumer in the room whenever a request stops being pending,
for any reason.

```json
{"type":"dismiss","id":"9f2c4e6a8b0d1f3a5c7e9b1d3f5a7c9e","reason":"answered",
 "outcome":"allow","by":"dv-e356e4"}
```

| `reason` | Meaning | Extra fields |
|---|---|---|
| `answered` | A consumer answered | `outcome`: `allow` \| `deny`; `by`: device id or `dashboard` |
| `cancelled` | Claude Code no longer waits: the hook request was aborted, the tool ran or failed, the tool batch or the turn ended, a new prompt was submitted, or the session ended (section 8.3). The terminal handled it | |
| `timeout` | The hold time expired with a live consumer and no answer. The terminal prompt is in charge | |
| `released` | Handed to the terminal: `permission_defer`, no live holder left, or server shutdown | |

A consumer removes the request and **never sends a response for a dismissed
id**. The consumer that answered also receives the `dismiss` and treats it as
the delivery confirmation of its answer.

### 4.11 `mode` (server -> consumers) and `set_mode` (consumer -> server)

```json
{"type":"mode","mode":"desk"}
{"type":"set_mode","mode":"ask"}
```

`set_mode` persists the room mode and the server broadcasts `mode` to all
consumers (including the sender). See section 6.

### 4.12 `telemetry` (device or sim -> server)

```json
{"type":"telemetry","battery":{"state":"discharging","pct":82,"mv":3987},
 "power":"battery","rssi":-58,"uptimeS":5321,"heapFree":148392,"heapMin":101224,
 "resetReason":"poweron","fw":"0.2.0","screen":"pet","stage":"active",
 "alert":"battery_low"}
```

| Field | Type | Required | Notes |
|---|---|---|---|
| `battery.state` | `"charging"` \| `"full"` \| `"discharging"` \| `"absent"` | yes | |
| `battery.pct` | integer 0..100 or `null` | yes | `null` when `absent` |
| `battery.mv` | integer | yes | Filtered battery millivolts |
| `power` | `"usb"` \| `"battery"` | yes | External power present or not |
| `rssi` | integer (dBm) | yes | |
| `uptimeS` | integer | yes | |
| `heapFree`, `heapMin` | integer | yes | Internal heap, bytes |
| `resetReason` | string | yes | `poweron`, `sw`, `panic`, `wdt`, `brownout`, `deepsleep`, `usb`, `other` |
| `fw` | string | yes | |
| `screen` | string | no | Current screen id |
| `stage` | `"active"` \| `"dim"` \| `"off"` | no | Power stage |
| `alert` | `"battery_low"` \| `"battery_critical"` | no | Present only while the condition holds |

Sent once after `register_ack`, then every 60 s, and within 2 s of a battery
state change, a 5 % step, or an alert change. At most 300 bytes. The server
stores the latest sample per device and never forwards it over WebSocket.

### 4.13 `identify` (server -> one device)

```json
{"type":"identify","durationMs":3000}
```

The device shows its name full-screen and plays the `identify` feedback pattern
for `durationMs` (1000..10000), then returns to what it showed before.

---

## 5. Enums

### 5.1 Mood

One enum for `status`, `notify` and `mood` (v1 `StatusMood` and `PetMood` are
merged):

```
happy  sad  excited  anxious  sleeping  angry  proud  idle  focused  working  thinking
```

The server rejects any other value from a producer with `INVALID_MESSAGE`.
Consumers map an unknown mood to `idle`.

### 5.2 Notify events

Any `[a-z0-9_]+` string is valid. Consumers choose the reaction by event name
first, then by mood:

| Event | Reaction class |
|---|---|
| `tests_passed`, `build_succeeded`, `deploy_complete`, `*_succeeded`, `*_passed` | positive |
| `tests_failed`, `build_failed`, `deploy_failed`, `error`, `*_failed` | negative |
| `task_complete` | complete |
| `task_paused` | none (toast only) |
| `needs_input` | attention |
| anything else | positive if mood is `happy`/`excited`/`proud`, negative if `sad`/`angry`, otherwise none |

---

## 6. Room mode and settings

| Mode | `PermissionRequest` handling |
|---|---|
| `ask` (default) | Sent to consumers as described in section 8.3 |
| `desk` | The server replies with the neutral hook reply immediately and sends nothing. The user is at the keyboard |

Room settings (persisted per room; REST in section 7.6):

| Setting | Type | Default | Range |
|---|---|---|---|
| `mode` | `ask` \| `desk` | `ask` | |
| `holdMs` | integer | server default (`DEVOTCHI_PERMISSION_HOLD_MS`, 30000) | 10000..300000 |
| `shareText` | boolean | `false` | When true, `Stop` notifications include the first line of the assistant's last message |

There is deliberately no auto-approve mode and no timeout policy: a request
that nobody answers is never allowed and never denied by Devotchi.

---

## 7. REST API

JSON in, JSON out. Request bodies require `Content-Type: application/json`.
Every error has the shape:

```json
{"error":"Human readable message","code":"machine_code"}
```

| HTTP | `code` |
|---|---|
| 400 | `invalid_request` |
| 401 | `unauthorized` |
| 403 | `forbidden` |
| 404 | `not_found` |
| 409 | `conflict` |
| 410 | `expired` or `already_used` |
| 413 | `payload_too_large` |
| 429 | `rate_limited` (with `Retry-After` seconds) |
| 500 | `internal` |

Body limits: 16 KB on `/api/*`, 2 MB on `/hook/*` (hook bodies contain file
contents).

### 7.1 Credentials

- **API token**: UUID v4 string (36 chars). Stored server-side as a SHA-256
  hash. Kinds:
  - `cli`: full access to its room (REST, hooks, producer and consumer WebSocket).
  - `device`: consumer WebSocket only. Rejected with 403 on every REST and hook
    route.
- **Session token** (multi-user mode only): `dvs_` + 43 base64url characters,
  valid 30 days, obtained from login. Accepted on account routes and wherever a
  `cli` token is accepted for REST.
- Both are sent as `Authorization: Bearer <value>`. Hook and WebSocket routes
  also accept `?token=` for API tokens.

In the tables below, auth `cli` means a `cli` API token (or a session token in
multi-user mode); `session` means a session token only.

### 7.2 Health and identity

| Method | Path | Auth | Success |
|---|---|---|---|
| GET | `/health` (alias `/`) | none | 200 `{"status":"ok","version":"0.2.0","proto":2,"mode":"single-user","uptimeS":123}` |
| GET | `/api/me` | cli | 200 `{"kind":"token","roomId":"5e140e256d56","tokenId":"b2a7b905","label":"cli","serverMode":"single-user"}`; with a session: `{"kind":"session","userId":"...","email":"...","roomId":"...","serverMode":"multi-user"}` |

### 7.3 Tokens and accounts

| Method | Path | Auth | Body | Success | Errors |
|---|---|---|---|---|---|
| POST | `/api/auth/token` | single-user: none (or `X-Devotchi-Admin: <secret>` when `DEVOTCHI_ADMIN_SECRET` is set); multi-user: session | `{"label"?: string<=64}` | 201 `{"token":"<uuid>","id":"<8 hex>","roomId":"<12 hex>"}` | 401, 429 |
| GET | `/api/tokens` | cli | | 200 `{"tokens":[{"id","label","kind","createdAt","lastUsed"}]}` | |
| DELETE | `/api/tokens/:id` | cli | | 204; open sockets of that token are closed with 4003 | 404 |
| POST | `/api/auth/register` | none (multi-user only) | `{"email","password","inviteCode"?}` | 201 `{"userId":"<16 hex>"}` | 400, 403 (registration closed or bad invite), 409, 429 |
| POST | `/api/auth/login` | none (multi-user only) | `{"email","password"}` | 200 `{"sessionToken":"dvs_...","userId":"...","expiresAt":"<ISO>"}` | 401, 429 |
| POST | `/api/auth/logout` | session | | 204 | 401 |

- Single-user mode: each anonymous `POST /api/auth/token` creates a new room.
  Multi-user mode: each user owns one room; minted tokens belong to it.
- Email is trimmed and lower-cased and must match `^[^@\s]+@[^@\s]+\.[^@\s]+$`.
  Password is 10..72 bytes.
- `register`, `login` and the account routes return 404 in single-user mode.

### 7.4 Pairing

See section 9 for the flows.

| Method | Path | Auth | Body | Success | Errors |
|---|---|---|---|---|---|
| POST | `/api/device/pair/start` | none | `{"deviceId":"dv-e356e4","model":"devotchi-v1","fw":"0.2.0"}` | 201 `{"code":"K7M2QX","deviceSecret":"<32 hex>","expiresInS":300,"pollIntervalMs":2000}` | 400, 429 |
| POST | `/api/device/pair/poll` | none | `{"deviceSecret":"<32 hex>"}` | 202 `{"status":"pending","expiresInS":212}` or 200 `{"token":"<uuid>","roomId":"<12 hex>","deviceId":"dv-e356e4","name":"desk"}` | 404 unknown secret, 410 `expired`, 429 |
| POST | `/api/pair/confirm` | cli | `{"code":"K7M2QX","name"?: string 1..24}` | 200 `{"deviceId":"dv-e356e4","name":"desk"}` | 404 unknown code, 410 `expired`/`already_used`, 429 |
| POST | `/api/pair` | cli | `{}` | 201 `{"code":"FBMNDP","expiresAt":"<ISO>","expiresInS":300}` | 401, 403 |
| POST | `/api/pair/claim` | none | `{"code":"FBMNDP","deviceId"?,"model"?,"fw"?}` | 201 `{"token":"<uuid>","roomId":"<12 hex>","deviceId":"dv-e356e4","name":"device"}` | 400, 404 unknown, 410 `expired`/`already_used`, 429 |

- The pairing code field is spelled `code` in every request and response
  (v1 `/api/pair` returned `pairingCode`).
- `fw` in these bodies and in `Device` is the device firmware version.
- Codes are case-insensitive on input, generated with a CSPRNG, single use,
  valid 300 s. Expired and used rows are pruned.
- `deviceId` in `start` and `claim` is validated against the formats in
  section 3.2 (400 otherwise). `claim` without `deviceId` creates a device
  with id `dv-` + 6 random hex.
- Both flows create a `device` token and a row in the device registry of the
  room that confirmed (flow A) or created the code (flow B).
- **Duplicate device id.** The registry key is (room, device id). If the
  confirming or claiming room already has a device with that id, the old row
  and its token are deleted, its sockets are closed with 4003, and the new
  pairing takes its place (the same physical device was paired again). A
  device with the same id in **another** room is never touched: a
  self-asserted `deviceId` cannot affect a room whose owner did not confirm.
- Delivery of the flow A token. `confirm` mints the token and keeps the raw
  value in memory only. The device must fetch it with `poll` within 60 s of
  the confirm; after the first 200 the same body stays retrievable with the
  same `deviceSecret` for another 60 s (the response may have been lost),
  then 410.
- Undelivered flow A pairings. If the token was not fetched within 60 s of
  the confirm, or the server restarted before delivery, `poll` returns 410
  `expired` and the server deletes the device row and its token. The device
  starts again with `start`.

### 7.5 Devices

| Method | Path | Auth | Body | Success | Errors |
|---|---|---|---|---|---|
| GET | `/api/devices` | cli | | 200 `{"devices":[Device]}` | |
| PATCH | `/api/devices/:id` | cli | `{"name": string 1..24}` | 200 `Device` | 400, 404 |
| DELETE | `/api/devices/:id` | cli | | 204. Revokes the device token; its socket is closed with 4003 | 404 |
| POST | `/api/devices/:id/identify` | cli | `{"durationMs"?: 1000..10000}` | 202 `{"sent":true}` | 404, 409 device offline |

A device id of another room is 404. Device ids are unique per room, not
globally (section 7.4); `:id` always refers to the caller's room.

`Device`:

```json
{"id":"dv-e356e4","name":"desk","model":"devotchi-v1","fw":"0.2.0",
 "createdAt":"2026-11-14T13:18:51.000Z",
 "online":true,
 "lastSeen":"2026-11-14T14:30:02.000Z",
 "lastHoldAt":"2026-11-14T14:29:40.000Z",
 "holding":0,
 "telemetry":{"battery":{"state":"absent","pct":null,"mv":4116},"power":"usb","rssi":-58,
              "uptimeS":5321,"heapFree":148392,"heapMin":101224,"resetReason":"usb",
              "fw":"0.2.0","screen":"pet","stage":"active"},
 "telemetryAt":"2026-11-14T14:29:58.000Z"}
```

| Field | Meaning (liveness) |
|---|---|
| `online` | The device has a registered WebSocket right now |
| `lastSeen` | Time of the last frame of any kind from the device (`null` if never) |
| `lastHoldAt` | Time of the last `holding` frame, i.e. the last request ack or heartbeat (`null` if never) |
| `holding` | Number of pending requests the device currently holds |
| `telemetry`, `telemetryAt` | Last telemetry sample (`null` if none) |

All times are ISO 8601 UTC with `Z`.

### 7.6 Room

| Method | Path | Auth | Body | Success |
|---|---|---|---|---|
| GET | `/api/room` | cli | | 200 `Room` |
| PUT | `/api/room/settings` | cli | any subset of `{"mode","holdMs","shareText"}` | 200 `{"mode":"ask","holdMs":30000,"shareText":false}`; a `mode` change is broadcast as `mode` |

`Room`:

```json
{"roomId":"5e140e256d56",
 "settings":{"mode":"ask","holdMs":30000,"shareText":false},
 "peers":{"producers":1,"consumers":2,"devices":1},
 "pending":[{"id":"9f2c...","tool":"Bash","summary":"rm -rf dist","risk":"high",
             "project":"devotchi","sid":"3fa9c1","ageMs":4100,"remainingMs":25900,
             "holders":["dv-e356e4"]}],
 "lastRequest":{"at":"2026-11-14T14:29:40.000Z","tool":"Bash","outcome":"allow",
                "detail":"answered","by":"dv-e356e4"},
 "diagnostics":{"dialogWhilePending":{"count":0,"lastSeenAt":null}},
 "devices":[Device]}
```

`holders` and `by` use the identities defined in section 3.2 (registry id,
`sim-...`, or `dashboard`).

`diagnostics.dialogWhilePending` counts evidence that Claude Code showed its
own permission dialog while a Devotchi request of the same session was still
pending (section 8.4). It is kept in memory per room and starts at 0 after a
server restart.

`lastRequest.outcome` is `allow`, `deny` or `neutral`. `lastRequest.detail` is
one of `answered`, `no_consumer`, `no_ack`, `holder_lost`, `deferred`,
`timeout`, `cancelled`, `desk`, `skipped_tool`, `room_busy`, `shutdown`.
`lastRequest` is `null` until the first request.

### 7.7 Notifications from scripts

| Method | Path | Auth | Body | Success |
|---|---|---|---|---|
| POST | `/api/notify` | cli | `{"event","mood"?,"detail"?,"source"?,"urgency"?}` | 202 `{"delivered":2}` (number of consumers it was sent to) |

Fields follow `notify` (section 4.4). When `mood` is absent it defaults by
event class (section 5.2): positive `happy`, negative `sad`, otherwise `idle`.
`source` is also copied into `project`. 30 requests per minute per token.

### 7.8 Rate limits

Sliding one-minute windows, in memory, keyed by client IP (the socket address,
or `X-Forwarded-For` when `DEVOTCHI_TRUST_PROXY=1`) unless noted.

| Route | Limit |
|---|---|
| `POST /api/auth/token` | 10/min |
| `POST /api/auth/register` | 5/min |
| `POST /api/auth/login` | 10/min per IP; 5 failures per 15 min per email |
| `POST /api/pair/claim` | 10/min; 20 failures per hour |
| `POST /api/device/pair/start` | 6/min |
| `POST /api/device/pair/poll` | 60/min |
| `POST /api/pair/confirm` | 10/min per token and 10/min per IP; 100 failed codes per hour per IP, then 429 |
| `POST /api/notify` | 30/min per token |
| `/hook/*` | 600/min per token |

---

## 8. Claude Code hook API

### 8.1 Endpoint

```
POST /hook/event
Authorization: Bearer <cli token>        (or ?token=<cli token>)
X-Devotchi-Hook: 1
Content-Type: application/json
<Claude Code hook input JSON>
```

The server dispatches on `hook_event_name`. v1 routes remain as aliases for
bodies without `hook_event_name`: `/hook/permission` = `PermissionRequest`,
`/hook/status` = `PostToolUse`, `/hook/stop` = `Stop`.

`X-Devotchi-Hook` is optional for the server and is never checked. It exists
only so that `devotchi setup` can recognise the hooks it installed (section
8.7); v1 hooks do not send it.

Every hook route answers `200` with a JSON object. Except for a decided
`PermissionRequest`, the body is `{}` and is sent **immediately** (the server
never does slow work before replying). Unknown events get `{}`. Authentication
failures are 401; Claude Code treats any non-2xx as a non-blocking error.

### 8.2 Sessions

From the common hook fields the server derives, for every event:

- `sid` = first 6 hex characters of SHA-256(`session_id`).
- `project` = basename of `cwd`, sanitised to ASCII, max 32 bytes.

- `agent` = the hook input's `agent_id` (present inside subagents and
  teammates), or none for the main conversation. It is used only to scope
  request cancellation (section 8.3) and is never sent to consumers.

`sid` and `project` are attached to the messages the server emits. Per
`(room, sid)` the server keeps the time of the last `UserPromptSubmit`, a
consecutive tool failure counter and a "terminal dialog may be open" flag
(section 8.4); a session is forgotten on `SessionEnd` or after 30 minutes of
silence.

### 8.3 `PermissionRequest`

Input fields used: `tool_name`, `tool_input`, `session_id`, `cwd`, `agent_id`.

The server replies with the **neutral reply** `{}` immediately, without
contacting consumers, when any of these holds:

| Condition | `lastRequest.detail` |
|---|---|
| Room mode is `desk` | `desk` |
| No consumer is registered in the room | `no_consumer` |
| `tool_name` is `AskUserQuestion` or `ExitPlanMode` (a bare allow is not sufficient for them) | `skipped_tool` |
| The room already has 8 pending requests | `room_busy` |

Otherwise it creates a request, sends `permission` (with `timeout` = hold
time) to every consumer in the room, and holds the HTTP response:

```
            +--> any consumer sends holding{id} within ACK --> HELD
 CREATED ---+
            +--> nobody acks within ACK ------------------> neutral  (no_ack)

 HELD: resolved by the first of
   permission_response granted=true   -> allow
   permission_response granted=false  -> deny
   permission_defer                   -> neutral  (deferred)
   every holder lost                  -> neutral  (holder_lost)
   hold time expired                  -> neutral  (timeout)
   hook HTTP client disconnected      -> no reply (cancelled)
   a cancelling hook event (table below) -> neutral (cancelled)
   server shutdown                    -> neutral  (shutdown)
```

The same resolutions apply in CREATED (before any ack), except "every holder
lost".

Cancelling hook events. Claude Code does not tell a hook that its request was
answered in the terminal, so the server infers it from later events of the
same session (`sid`):

| Event | Requests cancelled |
|---|---|
| `PostToolUse`, `PostToolUseFailure` | The oldest pending request with the same `tool_name` and the same `tool_input` (compared by SHA-256 of canonical JSON) |
| `PostToolBatch` | Every pending request with the same `agent` (the batch that contained them has resolved) |
| `Stop`, `StopFailure`, `UserPromptSubmit` | Every pending request of the main conversation (no `agent`) |
| `SessionEnd` | Every pending request of the session |

A terminal **deny** produces no `PostToolUse`/`PostToolUseFailure`; it is
caught by `PostToolBatch`, `Stop` or the next `UserPromptSubmit` at the
latest. "Hook HTTP client disconnected" means the response stream closed
before the server finished writing a reply; the end of the request body is
not a disconnect.

Timing (server configuration, section 8.6):

| Name | Default | Meaning |
|---|---|---|
| ACK | 3000 ms | Time for a first `holding` naming the request |
| ALIVE | 12000 ms | A holder is lost when it sent no `holding` naming the request for this long, or its socket closed |
| HOLD | 30000 ms (room `holdMs`) | Maximum time a request stays pending, measured from creation |

A consumer becomes a holder with its first `holding` for the id and stays one
while its heartbeats keep arriving. A valid `permission_response` is accepted
during CREATED as well as HELD, from any consumer in the room, holder or not.

Every resolution sends `dismiss` to all consumers (section 4.10) with reason
`answered` (allow/deny), `released` (`no_ack`, `deferred`, `holder_lost`,
`shutdown`), `timeout`, or `cancelled`.

Hook replies:

```json
{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"allow"}}}
```
```json
{"hookSpecificOutput":{"hookEventName":"PermissionRequest",
 "decision":{"behavior":"deny","message":"Denied on Devotchi"}}}
```
```json
{}
```

**Devotchi never produces `deny` or `allow` on its own.** `allow` and `deny`
come only from an explicit human answer on a consumer. Every other outcome is
`{}`, which leaves Claude Code's own permission flow in charge.

### 8.4 What Claude Code does with these replies

Checked against the Claude Code hooks reference
(`code.claude.com/docs/en/hooks`, 2026-11-14):

| Fact | Status |
|---|---|
| A 2xx reply with an empty body or `{}` carries no decision; the permission flow proceeds unchanged (the normal dialog in an interactive session) | Documented |
| A hook that reaches its `timeout` is cancelled and renders no decision | Documented |
| A non-2xx reply or a connection failure is a non-blocking error; the permission flow proceeds | Documented |
| In sessions that cannot show a prompt (background subagents in non-interactive mode, `-p` runs without a permission host), Claude Code itself denies a call no hook decided | Documented. That denial is Claude Code's, not Devotchi's |
| In sessions hosted through the Agent SDK `canUseTool` callback (desktop app, VS Code extension) or `--permission-prompt-tool`, hooks run alongside the host and whichever decides first applies | Documented |
| `AskUserQuestion` and `ExitPlanMode` need `updatedInput` on allow; a bare allow is ignored and the prompt is still shown | Documented |
| The `PermissionRequest` hook runs "when Claude Code is about to ask you for permission"; on allow the transcript shows the hook's decision "where the dialog would have appeared" | Documented |
| A terminal deny fires neither `PostToolUse` nor `PostToolUseFailure` | Documented |
| `Notification` with `permission_prompt` fires about 6 s after a permission dialog **appeared** (terminal) or after Claude asked (SDK hosts) | Documented |
| Whether a plain **terminal** session shows its dialog while an http `PermissionRequest` hook is still pending | **Not documented, not tested** |
| Whether Claude Code aborts the pending HTTP request when the user answers elsewhere first | **Not documented, not tested** |
| Whether `PostToolBatch` fires for a batch in which a call was denied | **Not documented, not tested** |

**Working assumption: in a plain terminal session the permission dialog is
NOT usable while the hook is pending.** The wording above ("about to ask",
"where the dialog would have appeared", and a race being documented only for
SDK hosts) points that way. Consequences:

- The hold time is how long a person at the keyboard waits when a live device
  shows the request and nobody answers it. The default is therefore short,
  **30 s**, and configurable per room (`holdMs`, 10..300 s) and per server
  (`DEVOTCHI_PERMISSION_HOLD_MS`).
- The common cases do not wait at all: `desk` mode (0 s), no device or a
  sleeping device (0 s), a device that does not acknowledge (3 s),
  "send to terminal" on the device (`permission_defer`).
- When the hold time ends the hook gets `{}`, the terminal dialog appears, and
  about 6 s later the `permission_prompt` notification makes the device say
  "Permission needed in terminal" (section 8.5).

The design is also correct if the assumption is wrong (SDK hosts, or a future
Claude Code): the user answers in the terminal, the server learns it from the
aborted request or a cancelling event (section 8.3) and dismisses the request
with `cancelled`; a late answer from the device gets `UNKNOWN_REQUEST` and
changes nothing.

**Self-diagnosis.** The server finds out which case applies. When a
`Notification` with `notification_type` `permission_prompt` arrives for a
session that has a pending Devotchi request **and** whose "terminal dialog may
be open" flag is clear, a dialog was on screen while the hook was pending. The
server increments `diagnostics.dialogWhilePending.count` and sets `lastSeenAt`
(`GET /api/room`, section 7.6). The flag is set when a `PermissionRequest` of
the session gets a neutral reply (a dialog for that call may now be open) and
cleared by the next `PostToolUse`, `PostToolUseFailure`, `PostToolBatch`,
`UserPromptSubmit`, `Stop` or `StopFailure` of the session. `devotchi doctor`
reports the counter and, when it is non-zero, suggests raising `holdMs`. The
counter is evidence, not proof: a sandbox network prompt (which runs no
`PermissionRequest` hook) can also trigger it.

### 8.5 Other events

| `hook_event_name` | Server action |
|---|---|
| `UserPromptSubmit` | Record turn start. Emit `status {msg:"Thinking...", mood:"thinking"}`. The prompt text is never forwarded. Cancels requests (8.3) |
| `PostToolUse` | Reset the session failure counter. Emit `status {msg:<tool summary>}`, coalesced to at most 2 per second per room (the latest wins). The status carries `mood:"working"` only when the room mood (section 4.5) is `thinking`, `idle` or `sleeping`; otherwise it carries no mood, so a mood set by the agent or by a notification stays. Cancels a matching request (8.3) |
| `PostToolUseFailure` | Emit `status {msg:"<tool> failed"}` with no mood. On the 3rd consecutive failure in a session emit `notify {event:"error", mood:"angry", detail:"3 tool failures in a row", urgency:2}` and reset the counter. Cancels a matching request (8.3) |
| `PostToolBatch` | No message. Cancels requests (8.3) |
| `Notification`, `notification_type` `idle_prompt` or `agent_needs_input` | `notify {event:"needs_input", mood:"anxious", detail:"Claude is waiting", urgency:3}` |
| `Notification`, `notification_type` `permission_prompt` | If the session has a pending request in the room: no message; counts for the diagnosis in 8.4. Otherwise `notify {event:"needs_input", mood:"anxious", detail:"Permission needed in terminal", urgency:2}` |
| `Notification`, other types | Nothing |
| `Stop` with empty or absent `background_tasks` | `notify {event:"task_complete", mood:"sleeping", detail:<D>, urgency:2}`. Cancels requests (8.3) |
| `Stop` with non-empty `background_tasks` | `notify {event:"task_paused", mood:"idle", detail:"<N> background tasks", urgency:1}`. Cancels requests (8.3) |
| `StopFailure` | `notify {event:"error", mood:"sad", detail:<error, 120 bytes>, urgency:2}`. Cancels requests (8.3) |
| `SessionEnd` | Forget the session; cancel its pending requests |

Devotchi's own MCP tools: for `PostToolUse` and `PostToolUseFailure` whose
`tool_name` matches `^mcp__devotchi__`, the server emits **no status** (the
tool call itself just set the status or mood; a status describing that call
would overwrite it). Cancellation and the failure counter still apply.

`<D>` for `Stop`: `"Done (4m12s)"` using the time since the session's last
`UserPromptSubmit`, or `"Done"` when unknown. When room setting `shareText` is
true and `last_assistant_message` is present, `<D>` is the first line of that
message cut to 120 bytes, followed by the duration in parentheses.

The v1 behaviour of sending `notify(idle)` followed immediately by
`mood(sleeping)` on `Stop` is gone: one `notify` with mood `sleeping`.

`<tool summary>` is the `summary` produced by the shared tool formatter
(`formatToolCall` in `packages/shared`), the same one used for `permission`.

### 8.6 Server configuration relevant to hooks

| Env var | Default | Meaning |
|---|---|---|
| `DEVOTCHI_PERMISSION_HOLD_MS` | `30000` | Default room `holdMs` (10000..300000) |
| `DEVOTCHI_PERMISSION_ACK_MS` | `3000` | ACK |
| `DEVOTCHI_PERMISSION_ALIVE_MS` | `12000` | ALIVE |

### 8.7 Hooks installed by `devotchi setup`

All are `type: "http"`, URL `<server>/hook/event`, with headers
`Authorization: Bearer <token>` and `X-Devotchi-Hook: 1`.

| Event | Matcher | `timeout` (s) |
|---|---|---|
| `PermissionRequest` | none | 310 |
| `PostToolUse` | none | 5 |
| `PostToolUseFailure` | none | 5 |
| `PostToolBatch` | none | 5 |
| `Notification` | `idle_prompt\|agent_needs_input\|permission_prompt` | 5 |
| `UserPromptSubmit` | none | 5 |
| `Stop` | none | 5 |
| `StopFailure` | none | 5 |
| `SessionEnd` | none | 2 |

That is nine hook entries. The `PermissionRequest` timeout of 310 s covers
the largest `holdMs` (300 s) plus margin.

The `X-Devotchi-Hook` header marks a hook as owned by Devotchi; `setup` only
ever replaces or removes hooks carrying it (plus v1 hooks whose URL path is
exactly `/hook/permission`, `/hook/status` or `/hook/stop` with a `token`
query).

---

## 9. Pairing

### 9.1 Code format

- 6 characters from the 31-symbol alphabet
  `ABCDEFGHJKMNPQRSTUVWXYZ23456789` (no `0 O I L 1`).
- Displayed as two groups (`K7M 2QX`); the space is not part of the code.
- Input is upper-cased by the server.

### 9.2 Flow A: the device shows a code (default)

```
device                              server                         PC
  | POST /api/device/pair/start       |                              |
  |---------------------------------->|                              |
  | 201 {code, deviceSecret, ...}     |                              |
  |<----------------------------------|                              |
  | shows "K7M 2QX"                   |    devotchi pair K7M2QX      |
  |                                   |<-----------------------------|
  |                                   | POST /api/pair/confirm       |
  | POST /api/device/pair/poll (2 s)  | 200 {deviceId, name}         |
  |---------------------------------->|----------------------------->|
  | 200 {token, roomId, deviceId,name}|                              |
  |<----------------------------------|                              |
  | stores token + room, connects /ws |                              |
```

When `poll` returns 410 the device requests a fresh code with `start`.

### 9.3 Flow B: the PC creates a code (serial console and tests)

`POST /api/pair` on the PC (`devotchi pair --new`), then the device calls
`POST /api/pair/claim` with that code (firmware console command
`pair <CODE>`).

### 9.4 Unpairing and revocation

- `DELETE /api/devices/:id` (dashboard, `devotchi devices rm`) revokes the
  token; the socket closes with 4003.
- **Close code 4003** is an authenticated statement by the server. The device
  erases its token and room id **only** (WiFi credentials and server URL
  stay) and starts flow A, showing "Pairing revoked".
- **HTTP 401 during the WebSocket upgrade** can also come from something that
  is not a protocol v2 server (a v1 server, which only reads `?token=`; a
  reverse proxy; a captive portal). The device erases its token only when
  **all** of these hold:
  1. `GET /health` on the same server answers 200 with `"proto":2`
     (checked after each 401);
  2. three consecutive connection attempts ended with 401 (any other outcome
     resets the count);
  3. at least 30 s passed between the first and the last of them.

  Until then it keeps retrying with backoff and shows "Server rejected token,
  retrying". When `/health` does not report protocol 2 it shows "Incompatible
  server", keeps the token and retries no faster than every 60 s.
- The device never puts its token in a URL.
- "Unpair" in the device Settings erases the same two values locally; the
  server keeps the registry row until it is deleted from the PC.

---

## 10. Device server URL

The device stores one base URL: `ws://host[:port]` or `wss://host[:port]`
(no path, no query, at most 127 bytes). From it the firmware derives:

| Use | URL |
|---|---|
| WebSocket | `<base>/ws` |
| REST | `http://host[:port]/api/...` for `ws`, `https://host[:port]/api/...` for `wss` |

Default ports: 80 for `ws`, 443 for `wss`. `wss`/`https` are verified against
the ESP-IDF certificate bundle.

---

## 11. Limits summary

| Item | Limit |
|---|---|
| Frame server -> client | 2048 bytes |
| Frame client -> server | 2048 bytes accepted; 2049..8192 `MESSAGE_TOO_LARGE`; above closes 1009 |
| Firmware reassembly buffer | 4096 bytes |
| `status.msg`, `permission.summary`, `mood.reason` | 200 bytes |
| `notify.detail` | 300 bytes |
| `permission.details` | 600 bytes |
| `permission.tool`, `notify.event` | 40 bytes |
| `project`, `notify.source` | 32 bytes |
| Device name | 1..24 bytes |
| Pending requests per room | 8 |
| On-device request queue | 4 |
| Producers / consumers per room | 8 / 8 |
| Register deadline | 10 s |
| Heartbeat | `ping` every 30 s; dead after 70 s of silence |
| `holding` | Ack debounce 100 ms (max 500 ms); heartbeat every 4 s; server bucket 8, refill 2/s |
| Permission timers | ACK 3 s, ALIVE 12 s, HOLD 30 s default (10..300 s) |
| `permission.timeout` | >= 1000 ms; requests with less remaining are not replayed |

---

## 12. Changes from v1 and who must change

| Change | Server | Client / MCP | Web UI | Firmware |
|---|---|---|---|---|
| `register.proto` and `register.client`; `roomId`/`token` in `register` ignored | accept, validate | send | proxy sends `kind: dashboard` | send |
| `register_ack`: `pairingCode` and `connectedPeers` removed; `proto`, `serverVersion`, `serverTime`, `mode`, `peers`, `device` added | change | read `peers` | change | parse by `type`, read `mode` |
| Token in `Authorization: Bearer` on upgrade and hooks | accept both | send header | n/a | send header |
| Canonical WebSocket path `/ws` | keep any-path | unchanged | unchanged | change from `/` |
| Several producers per room; close code 4001 removed | change | remove 4001 special case; `devotchi start` no longer registers a producer | n/a | n/a |
| One `Mood` enum for all messages | validate | accept all 11 in every tool | change | already parses all 11 |
| `status.mood` absent means "keep the current mood" (v1: `working`); room mood replayed as a `mood` message | change hook statuses and replay | `show_status` sends `mood` only when given | change | change |
| `register.client.version` is the only version field (no `client.fw`) | read | send | proxy sends | send firmware version as `version` |
| `POST /api/pair` returns `code` (v1: `pairingCode`) | change | change | n/a | n/a |
| Device identity comes from the registry; ids unique per room | change | n/a | n/a | n/a |
| `error.id` on `UNKNOWN_REQUEST`/`INVALID_MESSAGE` for a response | send | n/a | use when present | use when present |
| `holding` ack debounced 100 ms; server bucket 8 + 2/s | change | sim: change | change | new |
| HTTP 401 on upgrade is not an immediate revocation (9.4) | n/a | n/a | n/a | change |
| `approval` and `approval_response` removed | reject as unknown | delete types | delete handling | delete handling |
| `permission.action` removed; `tool`, `summary`, `risk`, `sid`, `project` added; `details` preformatted, 600 bytes; `id` is 32 hex | change | n/a | change | change |
| `permission.timeout` is the remaining hold time | change | n/a | deadline-based countdown | deadline-based countdown |
| `holding`, `permission_defer` | new | n/a | new | new |
| `permission_response.granted` strictly boolean, room-scoped; `timestamp` not required | change | n/a | drop `timestamp` | unchanged shape |
| `dismiss` on every resolution, with `reason`/`outcome`/`by`; consumers never answer a dismissed id | change | n/a | change | change (v1 sent a rejection) |
| Unanswered request resolves neutral, never deny; ack, alive and hold timers (hold default 30 s) | change | `setup` unchanged timeout 310 | n/a | n/a |
| Requests cancelled by `PostToolUse*`, `PostToolBatch`, `Stop`, `StopFailure`, `UserPromptSubmit`, `SessionEnd`, HTTP abort | new | `setup` installs `PostToolBatch` | n/a | n/a |
| Consumer messages are never forwarded to producers; `NO_PRODUCER` removed | change | delete handler | n/a | n/a |
| `peers`, `mode`/`set_mode`, `telemetry`, `identify` | new | read `peers` | new | new |
| Heartbeat: any frame counts, 70 s limit (v1 documented 10 s) | change | unchanged | unchanged | parse `ping` by `type`, not by substring |
| Sizes in UTF-8 bytes; server truncates instead of closing with 1009 | change | truncate before sending | n/a | 4096-byte reassembly |
| `/hook/event` dispatcher; v1 hook routes are aliases | new | `setup` rewrites hooks | n/a | n/a |
| `Stop` emits one `notify` (`task_complete`, mood `sleeping`) | change | n/a | n/a | n/a |
| Pairing flow A (`/api/device/pair/*`, `/api/pair/confirm`) | new | `devotchi pair <CODE>` | "Add device" | new pairing screen |
| `/api/pair/claim` accepts device metadata and returns `deviceId`, `name` | change | n/a | n/a | read optional fields |
| `GET /api/devices` returns the device registry only (v1 listed every token) | change | new commands | new panel | n/a |
| Error bodies gain `code`; health moves to `/health` (`/` stays as alias) | change | read `code` | n/a | read HTTP status only |

## 13. Not part of this protocol

- BLE as a message transport. BLE is used only for WiFi provisioning
  (`docs/architecture.md`). The v1 "BLE Protocol" section is withdrawn.
- A phone app, OTA firmware update, mDNS discovery, activity history,
  questions with selectable answers, and progress messages are backlog items
  and have no wire representation yet.
