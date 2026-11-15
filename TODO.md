# TODO

## Done

- Protocol v2 (`docs/protocol.md`): shared types and one validator, server, client and firmware speak only v2
- Server: rooms by token, SQLite (node:sqlite, migrations with a v0 backup), strict validation, rate/body/connection limits
- Pending-request manager: delivery ack, 4 s heartbeat, hold time (default 30 s, 10..300 s), dismiss reasons, replay, cancellation by later hook events; never auto-allows or auto-denies
- Device registry with server-decided identity, telemetry and liveness; pairing flow A (device shows a code) and flow B (`pair --new`)
- Room mode (ask / desk), room settings, `dialogWhilePending` diagnostics
- Multi-user mode with accounts and opaque sessions; Dockerfile, Docker Compose with Caddy TLS, deploy notes (`docs/deploy.md`)
- CLI: start (incl. `--local --lan`), setup (nine hooks), pair, devices, status, doctor, mode, hold, notify, run, sim, login/logout, config
- MCP server with lazy provisioning and `get_device_state`
- Dashboard: loopback only, per-launch session secret, Host/Origin checks, path containment; web UI with request queue, holding, reconnect, devices panel with liveness and battery
- Installable client tarball (bundled with esbuild, workspace packages inlined); CI workflow incl. firmware host tests without ESP-IDF

## Open

- [ ] Switch the owner's own setup to the v2 server (start it on the real database, re-run `devotchi setup`; the board keeps its pairing)
- [ ] Power: the board browns out on USB power without a battery (backlight 75 % + WiFi TX or motor); re-test with a cell fitted, check the supply path if it persists
- [ ] Battery presence: with no cell the TP4056 CHRG pin toggles at about 800 Hz; confirm detection with a cell, consider a pull or capacitor on CHRG in the next board revision
- [ ] Check for ghost APPROVE presses on GPIO4 (three unexplained presses during a test on 2026-11-15 around 00:50)
- [ ] Hardware checks only the owner can do: buttons and wake from deep sleep, vibration and LED feel, BLE provisioning end to end, battery behaviour with a real cell, light sleep and current draw
- [ ] Observe in real use whether Claude Code's terminal prompt is usable while a hook is pending (`devotchi doctor` reports `dialogWhilePending`) and tune the default hold time
- [ ] Deploy a hosted multi-user server (files are ready; no real deploy yet)
- [ ] Decide whether to publish `devotchi` to npm (the package is `private: true`; the tarball installs)
- [ ] Run the CI workflow on GitHub once (written and syntax-checked only)
- [ ] Case: back cover, USB opening, print front and back
- [ ] Investigate the Claude Code Buddy system and whether Devotchi can integrate with it

## Phase 2 (planned)

- Answer Claude Code questions (AskUserQuestion) on the device with the buttons
- Richer, safe decisions on permission requests beyond approve/reject
- Clock face with a sleeping pet when idle
- Pet system: a signature pet plus selectable pet packs (on-device picker; more packs loaded from the dashboard)
- Demo/showcase mode: trigger moods, animations, a sample question and a sample permission request by hand
- Professional UI redesign: design system and mockups first (docs/design/), then the LVGL port

## Backlog (not approved or deferred)

One line each; none of these is planned until approved.

- delight-3: result animations on the device (pass/fail reactions beyond the toast)
- delight-4: clock face while idle
- delight-5: XP and levels for the pet
- delight-7: battery personality (the pet reacts to low battery)
- delight-8: speech bubble for status text
- delight-9: pixel-accurate web simulator of the device screen
- delight-10: hatching and pet identity
- delight-11: screen transitions
- delight-12: activity timeline
- workflow-5: always-allow / deny-with-reason from the device
- workflow-7: multi-session board (all sessions and projects at once)
- workflow-8: `ask_user` tool (free-form question to the device)
- workflow-9: work journal
- workflow-12: quick replies from the device
- ops-9: SoftAP setup portal as an alternative to BLE provisioning
- ops-10: OTA updates (needs the two-slot partition table; see docs/architecture.md 4.11)
- ops-11: mDNS discovery of the server
- ops-12: crash-loop safe mode
- QR code on the pairing screen
- On-screen confetti and shake reactions
- Per-mood complex animation loops
- `show_progress` MCP tool
- Branch detection via the SessionStart hook
- Session registry messages
- Subagent counters
- Remote `device_config` (settings pushed from the server)
- GitHub webhook adapter
- `doctor --device`: serial comparison and probe of a connected board
- `updateRequired` gating of old firmware
- Wake on charger plug
- Canvas simulator conformance fixtures
- auto_safe mode: rejected permanently, do not build (Devotchi never decides on its own)
