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

After editing `sdkconfig.defaults`, delete `sdkconfig` and rebuild. For a
variant build, keep the normal tree untouched and use a scratch directory:

```bash
python "$IDF_PATH/tools/idf.py" -B /tmp/x/build -D SDKCONFIG=/tmp/x/sdkconfig \
  -D "SDKCONFIG_DEFAULTS=$PWD/sdkconfig.defaults;/tmp/x/extra.defaults" build
```

The app image is about 1.6 MB of the 3 MB partition.

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
$PY -I tools/devotchi_serial.py cmd btn approve press
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

Useful commands: `state`, `screen`, `screenshot`, `tasks` (stack high-water
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
reject-token, confirm, tokens, status). It never logs tokens.

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
| `DEVOTCHI_LONG_PRESS_MS` | 1000 | Long-press threshold |
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
