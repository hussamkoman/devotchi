# Devotchi firmware

Plain C firmware for the Devotchi board (ESP32-S3, 2.0" ST7789 320x240 TFT,
three buttons, LED, vibration motor, single-cell battery with a charger).
ESP-IDF v6.0, LVGL 9, cJSON. The device is a protocol v2 consumer
(`docs/protocol.md`); the firmware design is in `docs/architecture.md`
section 4 and the coding rules in [`CONVENTIONS.md`](CONVENTIONS.md).

```
firmware/
  src/main/            app_main, Kconfig.projbuild (menu "Devotchi")
  src/components/      board, core (pure C, host tested), storage, display, ui,
                       feedback, buttons, power, net, debug_console, app
  host_test/           Unity tests for core, built with the host compiler
  tools/               serial console client, mock server, board test scripts
  sdkconfig.defaults   every ESP-IDF option the project changes
  partitions.csv       nvs 24 KB, phy_init, one 3 MB factory app
```

## Build and flash

```bash
. ~/.espressif/tools/activate_idf_v6.0.sh
cd firmware
python "$IDF_PATH/tools/idf.py" build
python "$IDF_PATH/tools/idf.py" -p /dev/ttyACM0 flash
```

`flash` writes the bootloader (0x0), the partition table (0x8000) and the
app (0x10000) only. **Never erase the flash** (`erase-flash`, `erase_region`
over 0x9000..0xFFFF, esptool `--erase-all`): the NVS partition holds the WiFi
credentials and the pairing, and the board has no BOOT button, so USB is the
only way back in.

The partition table has a `pets` partition at the end of the flash
(0x3F2000, 0xE000; architecture 7.3). A normal `flash` writes the new table
at 0x8000 and nothing else moves: `nvs` and `phy_init` keep their offsets, the
pairing and WiFi survive. The firmware may erase `pets` by label when it
cannot be opened (losing downloaded pets, nothing else); nothing ever erases
`nvs`. A board still on the old table runs with the built-in pets only.

After editing `sdkconfig.defaults`, delete `sdkconfig` and rebuild. For a
variant build, keep the normal tree untouched and use a scratch directory:

```bash
python "$IDF_PATH/tools/idf.py" -B /tmp/x/build -D SDKCONFIG=/tmp/x/sdkconfig \
  -D "SDKCONFIG_DEFAULTS=$PWD/sdkconfig.defaults;/tmp/x/extra.defaults" build
```

The app image is 1 667 760 bytes. Every build checks it against
`DEVOTCHI_APP_MAX_BYTES` = 1 966 080 bytes (0x1E0000, `CMakeLists.txt`):
`tools/check_app_size.cmake` runs after the binary is generated, prints
`devotchi.bin: N bytes of 1966080` and fails the build above the limit, so a
later OTA layout with 0x1F0000 slots stays possible (architecture 7.3).

## Fonts and UI assets

The UI follows `docs/design/design-system.md`. Fonts are generated from the
TTFs in `docs/design/fonts` and committed under `src/components/ui/fonts`;
the build never runs the generator:

```bash
PYTHON=~/.espressif/tools/python/v6.0/venv/bin/python tools/gen_fonts.sh   # needs Node (npx) and ImageMagick
```

| Font | Source | Size / line | bpp | Glyphs | Flash |
|---|---|---|---|---|---|
| `ui_font_display64` | Figtree Bold | 64 / 64 | 2 | `0-9 : -` space, 23 pairing letters | 15 085 B |
| `ui_font_title24` | Figtree Bold | 24 / 28 | 4 | ASCII | 10 130 B |
| `ui_font_body18` | Figtree Medium (+ JetBrains Mono for ✓) | 18 / 22 | 4 | device charset | 13 751 B |
| `ui_font_small14` | Figtree Medium (+ JetBrains Mono for ✓) | 14 / 18 | 4 | device charset | 9 724 B |
| `ui_font_mono14` | JetBrains Mono Medium | 14 / 18 | 4 | device charset | 8 924 B |
| `ui_font_icons` | `ICON` paths of `docs/design/tools/build.py` | 14 (hold glyphs 16) | 4 | 34 | 2 763 B |

Text fonts total 57 614 bytes of the 100 kB budget (60 377 with the icons).
The device charset is ASCII, U+00A0-U+00FF and `– — ‘ ’ “ ” • … ← ↑ → ↓ ✓ × −`;
Figtree has no check mark, so body and small take U+2713 from JetBrains Mono.
Digits are tabular in every Figtree font (`tools/patch_font.cjs` gives 0-9 one
advance and takes them out of kerning). The lock-up of the splash screen is
two A8 masks (`ui_img_lockup.c`, 5 842 B, `tools/gen_lockup.py`). The OFL
licence texts sit next to the generated files. `display` and `title` are for
firmware-owned strings only; dynamic text uses `body`, `small` or `mono`.

## Pet packs

Pets are data (architecture 7.2): `pets/<id>.pet.json` is the source,
`tools/petpack.py` (standard library only) turns it into the binary pack.
After editing a source, regenerate the golden pack, its CRC and the
compiled-in array together; the host test `test_pet_pack` fails when they
disagree:

```bash
python3 -I tools/petpack.py encode ../pets/devo.pet.json ../pets/golden/devo.pet
python3 -I -c "import zlib,sys;d=open(sys.argv[1],'rb').read();print('%08x'%zlib.crc32(d[:-4]))" \
  ../pets/golden/devo.pet > ../pets/golden/devo.crc32
python3 -I tools/petpack.py c-array ../pets/devo.pet.json src/components/core/pet_builtin_devo.c
python3 -I tools/petpack.py check ../pets/devo.pet.json ../pets/golden/devo.pet
```

Console: `pet list`, `pet use <id>`, `pet rm <id>`, `pet install <id> <rev>
<size> <crc32> [select]` (the same checks as a server's `pet_cmd`; the pack is
fetched from the configured server), `pet react <name>`, `pet aux`,
`pet scale <n>`, `pet outline on|off`, `pet mood`, `pet pose`, `pet freeze`.
`state` reports `pet` (selected, shown, source) and `pets` (partition,
slots, free, stored).

## Debug console

The USB-Serial-JTAG port (`/dev/ttyACM0`) carries the ESP-IDF log and a line
console. A command's payload (`@@JSON <object>`, or the
`@@SCREENSHOT BEGIN` / `@@D` / `@@SCREENSHOT END` lines of a screenshot) is
followed by one terminator line, `@@OK <cmd>` or `@@ERR <cmd> <code>
<message>`. `@@READY` marks a finished boot, and asynchronous `@@EVT <json>`
lines report what the device does (link, screen, request, btn, power, rx/tx
and more). `help` lists every command with its arguments.

Use `tools/devotchi_serial.py` (standard library; Pillow for screenshots), run
with the ESP-IDF Python:

```bash
PY=~/.espressif/tools/python/v6.0/venv/bin/python
$PY -I tools/devotchi_serial.py state                # application state as JSON
$PY -I tools/devotchi_serial.py cmd screen           # screen id and visible texts
$PY -I tools/devotchi_serial.py cmd btn approve press   # also: long (1.3 s), hold <ms>
$PY -I tools/devotchi_serial.py screenshot /tmp/s.png
$PY -I tools/devotchi_serial.py monitor              # log and EVT lines
```

It opens the port with `os.open` and `termios` instead of pyserial on
purpose: a serial library toggles DTR and RTS on open, and that sequence
passes through the state that resets the chip through the USB-Serial-JTAG
bridge. With this client the board keeps running; `--reset` pulses the lines
when a reset is wanted.

Secrets (token, WiFi passphrase, proof of possession) never appear in the log,
in a console reply or in the `screen` texts.

`ui demo <name> [variant]` shows any screen or widget state with constant
sample data without touching the app's state (`ui demo off` hands the screen
back and redraws it); `ui demo help` lists the names. `ui tabs` prints the
button tabs, which `screen` also reports (`tabs`).

Useful commands: `state`, `screen`, `ui ...`, `screenshot`, `tasks` (stack high-water
marks), `version`, `log <level> [tag]`, `server [<url>]`, `pair <CODE>`,
`wifi [<ssid> <pass>]`, `wifi-setup`, `set [<key> <value>]`, `power ...`,
`batt ...`, `fb ...`, `pet ...`, `preview <screen>`, `msg <json>` (handle a
protocol message locally), `wake`, `reboot`.

## Host tests

The `core` component is pure C and is tested on the PC:

```bash
# Without ESP-IDF: cJSON and Unity are downloaded
cmake -S firmware/host_test -B build-host -DDEVOTCHI_HOST_TEST_FETCH=ON
# With ESP-IDF installed (IDF_PATH set): its Unity and the managed cJSON are used
cmake -S firmware/host_test -B build-host
cmake --build build-host && ctest --test-dir build-host
```

AddressSanitizer and UBSan are enabled when the host compiler has them.

## Mock server and board tests

`tools/mock_server.py` is a protocol v2 server for tests (WebSocket, pairing,
`/health`), controllable over stdin or `POST /_ctl` (send, send-fragmented,
send-text, send-binary, send-continuation, flood, close, drop, stall,
reject-token, confirm, tokens, status, and for pet packs pet-add, pet-cmd,
pet-mode). It serves `pets/golden/*.pet` at `/api/pets/<id>/pack`
(`--pets DIR`, `--pet-truncate`). It never logs tokens.

```bash
python3 tools/mock_server.py --port 18090 [--tls CERT KEY] [--proto 1] [--confirm-after S]
```

Board scripts (ESP-IDF Python, `-I`). Each one records the board's server,
pairing and test-relevant settings first, makes a backup with `pair-backup`
(an existing backup is kept), and restores everything in a `finally` block.
They use ports 18080..18099 only.

| Script | What it checks |
|---|---|
| `e2e_link.py` | WiFi, connect, register, reconnect, close codes, 401 rule, pairing flows |
| `e2e_flow.py` | Approval cards, answers, queue, dismiss, toasts, offline banner (`--tls` for wss) |
| `e2e_pet.py` | Pet moods, status text, telemetry, settings page |
| `e2e_pets.py` | Pet packs: install, select, replace, remove, every `pets` error, resets during an install, the picker |
| `e2e_power.py` | Power stages, wake, swallowed wake press, battery decisions (dry run) |
| `e2e_soak.py` | Malformed frames, flood, stuck link, 30 min soak, stacks, heap, secrets (`--tls`) |
| `e2e_server.py` | The whole approval path against the real server from `packages/server` |

On USB power without a battery the board can brown out when the backlight is
bright while WiFi reconnects, so the scripts run at brightness 25 with haptics
off and put the original values back.

## Kconfig options (menu "Devotchi")

| Option | Default | Meaning |
|---|---|---|
| `DEVOTCHI_DEFAULT_SERVER_URL` | `ws://10.0.0.120:8080` | Server used until one is set by console, provisioning or settings |
| `DEVOTCHI_DISPLAY_SPI_HZ` | 40000000 | Panel SPI clock |
| `DEVOTCHI_DIM_BRIGHTNESS_PCT` | 10 | Backlight in the dim stage |
| `DEVOTCHI_PWR_USB_DEEP_MIN` | 0 | Deep sleep on external power after N minutes (0 = never) |
| `DEVOTCHI_PWR_BOOT_GRACE_S` | 60 | No sleep of any kind this long after boot |
| `DEVOTCHI_PWR_LIGHT_SLEEP` | y | Automatic light sleep on battery while the screen is off |
| `DEVOTCHI_PWR_TEST_SECONDS` | n | Debug: the deep-sleep setting counts seconds |
| `DEVOTCHI_BTN_POWER_SAVE` | y | Button library power-save mode (it owns the GPIO wake-up) |
| `DEVOTCHI_BATT_LOW_MV` / `_CRIT_MV` | 3500 / 3300 | Battery warning and protection thresholds |
| `DEVOTCHI_BATT_CAL_PERMILLE` | 1000 | Battery voltage calibration factor |
| `DEVOTCHI_MOTOR_MAX_DUTY_PCT` | 70 | Vibration motor duty ceiling |
| `DEVOTCHI_WS_RX_BUFFER` | 4096 | Largest reassembled WebSocket message |
| `DEVOTCHI_SHADOW_FB` | y | PSRAM shadow framebuffer for `screenshot` |
| `DEVOTCHI_TLS_EXTRA_CA` / `_PATH` | n / "" | Trust one CA embedded at build time (a file named `extra_ca.pem`) instead of the certificate bundle, for wss and https |
| `DEVOTCHI_PROV_NAME_PREFIX` | `DEVOTCHI` | BLE provisioning service name prefix |
| `DEVOTCHI_DEBUG_NVS_FAIL` | n | Debug: storage behaves as if NVS failed to initialise (never touches NVS) |

`wss://` and `https://` servers are verified against the ESP-IDF certificate
bundle. A server with a private CA needs a build with `DEVOTCHI_TLS_EXTRA_CA`,
which then trusts that CA only. Never
commit a private key; keep test certificates under `/tmp`.

## First setup (for a person)

1. **WiFi.** A board without WiFi credentials starts BLE provisioning
   (Settings -> WiFi setup or the console `wifi-setup` start it again; the old
   credentials stay until new ones work). The screen shows the device name
   `DEVOTCHI-xxxx` and a proof-of-possession code. In the Espressif
   **ESP BLE Provisioning** app (Android/iOS) choose BLE, pick the device,
   enter the proof of possession and select the network.
   Optional: send custom data `server=<url>` (for example
   `server=wss://devotchi.example.com`) to set the server URL in the same
   step; the device answers `OK` or `ERR bad url`.
2. **Server.** Without a custom server the Kconfig default is used. The
   console `server <url>` changes it at any time.
3. **Pairing.** Once online without a token, the device shows a six-letter
   code (flow A). Confirm it on the PC with `devotchi pair <CODE>`. From the
   console the other direction works too: `devotchi pair --new` on the PC,
   then `pair <CODE>` on the board (flow B).

Removing the device on the PC (`devotchi devices rm`) closes the socket with
4003; the device forgets its token and room id (WiFi and server stay) and
shows a new code.

## Pairing backup

The first pairing a device stores is copied to the NVS keys `bk_token`,
`bk_room` and `bk_url` (namespace `devotchi`). `pair-backup` makes that copy
on demand and keeps an existing backup unless given `--force`;
`pair-restore` writes it back (token, room id and server URL, so a URL that
came from the default stays the default) and reconnects. `reset-pairing`
erases the token and room id only. The test scripts rely on this: they pair
with a test server and finish with `pair-restore`.

`factory-reset --yes-erase-wifi-and-pairing` erases everything including the
backup; the test scripts never use it.

## What automation verified and what needs a person

Verified on the board by the scripts above: protocol v2 link behaviour
against the mock and the real server (register, ping/pong, reconnect,
close codes, 401 rule, holding ack within 500 ms and every 4 s, strict
answers, dismiss handling, three concurrent requests, holder loss on reboot
with no replay), both pairing flows, wss and https with a private CA, a
30-minute soak with server restarts, a 200-frame flood, malformed frames,
button latency while the link is stuck, stack and heap margins, a boot with
failed NVS, power stage timing on USB and the battery decisions through
injected readings and dry runs.

Needs the owner with the hardware:

- Panel colours and orientation (confirmed once on the plain-C firmware) and
  how the screens look in person.
- LED brightness and the vibration motor feel for each pattern.
- The real buttons, including the button library's power-save path (GPIO
  wake-up from light sleep) and waking from deep sleep with each button; the
  waking press must be swallowed.
- BLE provisioning end to end with the phone app, including `server=<url>`.
- Everything with a real battery: percentages, charge and full detection,
  low and critical thresholds, protection sleep, the calibration factor.
- Automatic light sleep on battery and current draw in each stage.
