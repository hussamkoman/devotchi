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
- Phase 2 (protocol 14, architecture 7), server and client: questions (`AskUserQuestion`), plans (`ExitPlanMode`), the richer decision menu (send to terminal, allow for this session with a narrow derived rule, deny and ask why, deny and stop), `extend`, full text for dashboards, room time zone and device time, switches `DEVOTCHI_QUESTIONS` / `DEVOTCHI_PLANS` / `DEVOTCHI_SESSION_GRANTS`, `doctor` checks for them
- Phase 2 dashboard: permission, question and plan cards, Pets panel (gallery drawn by `renderPetSvg`, upload, send, use, remove), time zone setting
- Pets as data: one `pets/<id>.pet.json` per pet, shared codec and `petpack.py` with golden packs, lint, generated firmware built-ins from `pets/builtin.txt`, server catalog, room uploads, `devotchi pets`, `pnpm run pet-sheet`; Devo (ghost, default), Mochi, Ember built in, Sprout in the catalog
- Phase 2 firmware: design-system UI port (fonts, theme, tabs with hold grammar), permission / question / plan cards and menus (`card_fsm`), quick menu and Settings tree, pet picker and stored pets in the `pets` partition, clock face as the resting face, going-to-sleep screen, low-battery toasts, showcase mode, app size check in every build

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
- [ ] Run the checks in docs/phase2-manual-tests.md on a real Claude Code session (session grants, compound commands, interrupt, plan modes, answers) before a release
- [ ] Investigate the Claude Code Buddy system and whether Devotchi can integrate with it

### Hardware checks only the owner can do (phase 2)

- [ ] Buttons by hand: a high-risk APPROVE click only nudges, a 1.2 s hold allows; the short-long haptic when the next card replaces one; the press that wakes the screen (dim, off, clock, deep sleep) does nothing else
- [ ] Haptic pulse on a new question or plan with haptics on (e2e runs used haptics off because of the USB brownout)
- [ ] Clock face on the panel: day and night backlight levels on battery and USB, night palette and outline pet readable
- [ ] Battery with a real cell: percent, charging and full detection, the 15 % and 8 % toasts, the `lowbatt` sleep and the battery-protection wake
- [ ] Visual review against the renders: clock face without link dot and battery (40, 41: brief or render, pick one), destructive confirm warning icon smaller than 53, going-to-sleep shows the plain sleeping pet instead of the drowsy one (91), value editor bar wider than 52, compact review wells for 3-4 answers and the left-aligned "Send n selected" row (35)

### Open minor issues (phase 2)

- [ ] Firmware: in the decision menu the APPROVE mode is fixed at press-down, so APPROVE pressed on one row, ACTION moving the cursor, then APPROVE released decides the newly focused row (never an allow); cancel the APPROVE press when the cursor moves (`core/card_fsm.c`)
- [ ] Firmware: `pet_install_commit` returns `storage` when the select step fails after a good write and read-back, but leaves the new pack stored (protocol 14.8 says nothing changes on failure)
- [ ] Firmware: a pet fetch requested while Wi-Fi is down has no deadline; further installs answer `busy` until the fetch runs (`app/app_pets.c`)
- [ ] Firmware: `power_fsm` sets `show_clock` from power state only, so with Settings or the showcase open when the screen dims the backlight uses clock levels and the CPU lock is released; pass "home visible" into `power_fsm`
- [ ] Firmware: `clock set <epoch>` without an offset is accepted as offset 0 instead of a usage error (`app/app_clock.c`)
- [ ] Firmware: the connecting, offline and splash screens have no pet; the offline screen has no Retry tab because there is no retry action
- [ ] Firmware: `power_fsm_output_t.low_toast` (the voltage-based toast) is computed but not used by the app; the toasts follow the percent
- [ ] Firmware: `ui_demo_active()` (lock held) and `ui_demo_is_active()` (takes the lock) are easy to confuse
- [ ] Firmware: the caps helper upper-cases Latin-1 letters (é becomes É); the design brief asked to leave non-ASCII letters as they are; owner to decide
- [ ] Pets: `petpack.py check` warns that Ember's spade reaches outside the 220x190 mockup box

## Backlog (not approved or deferred)

One line each; none of these is planned until approved.

- delight-3: result animations on the device (pass/fail reactions beyond the toast)
- delight-5: XP and levels for the pet
- delight-7: battery personality (the pet reacts to low battery)
- delight-8: speech bubble for status text
- delight-9: pixel-accurate web simulator of the device screen
- delight-10: hatching and pet identity
- delight-11: screen transitions
- delight-12: activity timeline
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
