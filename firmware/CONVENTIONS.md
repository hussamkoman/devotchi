# Firmware Coding Conventions

The firmware is plain C, built by ESP-IDF v6.0 (`gnu17`). No C++ sources, no
C++ dependencies. LVGL 9 is the graphics library. Architecture, pin map, NVS
keys and the debug console are specified in `docs/architecture.md`; the wire
protocol in `docs/protocol.md`.

## Layout

```
firmware/
├── CMakeLists.txt              project file (sets PROJECT_VER)
├── sdkconfig.defaults          IDF framework knobs (committed)
├── sdkconfig                   generated (gitignored)
├── partitions.csv              flash partition table (nvs stays at 0x9000)
├── dependencies.lock           component manager lockfile (committed)
├── src/
│   ├── main/
│   │   ├── app_main.c          entry point: init order, nothing else
│   │   ├── Kconfig.projbuild   the "Devotchi" menu
│   │   └── idf_component.yml   managed dependencies
│   └── components/<name>/
│       ├── CMakeLists.txt
│       ├── include/<name>.h    public API (more headers only when needed)
│       ├── <name>.c            implementation
│       └── <name>_priv.h       private header shared by the component's .c files
├── host_test/                  host unit tests for src/components/core
└── tools/                      Python host tools (serial console, mock server, e2e)
```

Headers in `include/` are the component's public API. Everything else is
private to the component.

## Naming

| Thing | Convention | Example |
|---|---|---|
| Files | `snake_case.c` / `.h` | `ws_client.c`, `power_fsm.h` |
| Public functions | `<module>_<verb>[_<object>]` | `storage_get_token()`, `ui_show_screen()` |
| Static functions | `snake_case`, no module prefix | `parse_permission()` |
| Types | `<module>_<name>_t` | `proto_msg_t`, `app_event_t` |
| Enum constants | `<MODULE>_<NAME>` | `MOOD_HAPPY`, `APP_EV_BUTTON` |
| Macros, constants | `UPPER_SNAKE` with module prefix | `PROTO_MAX_FRAME` |
| File-scope variables | `s_` prefix | `s_queue`, `s_state` |
| Kconfig symbols | `DEVOTCHI_<AREA>_<NAME>` | `CONFIG_DEVOTCHI_DISPLAY_SPI_HZ` |
| Log tag | the module name, one per `.c` | `static const char *TAG = "ws";` |

The module prefix is the file or component name (`wifi_`, `ws_`, `proto_`,
`ui_`, `app_`). Nothing public is unprefixed.

## Headers and includes

- Every header has `#pragma once` and compiles on its own.
- A public header includes only what its declarations need. Heavy IDF headers
  (`esp_wifi.h`, `lvgl.h` outside `ui`/`display`) stay in the `.c` file.
- Include order in a `.c` file: own header, project headers, ESP-IDF and
  third-party headers, C standard headers. Alphabetical within a group.
  `freertos/FreeRTOS.h` comes before other FreeRTOS headers.
- No function bodies in headers except `static inline` one-liners.

## Component structure

A component owns one piece of hardware or one responsibility and exposes a
small API. No `utils` or `helpers` components; a helper lives next to its only
user until a second user exists.

```cmake
idf_component_register(
    SRCS "wifi.c" "prov.c" "ws.c" "pairing.c" "net_worker.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES        core                     # appears in public headers
    PRIV_REQUIRES   esp_wifi esp_event ...   # used only in .c files
)
```

Dependency direction is one-way: `app` -> `ui`, drivers, `net`, `storage` ->
`core`, `board`. A driver never includes `app` or `ui` headers; it reports by
posting an event (see Tasks).

### `core` is pure C

`src/components/core` contains every module with logic and no hardware:
protocol, WebSocket message reassembly, text, URL parsing, request queue, pet
model and poses, button logic, feedback sequencing, battery maths, power
stages, backoff.

- It includes only the C standard library and `cJSON.h`. No `esp_*.h`, no
  FreeRTOS, no LVGL, no logging macros.
- Time is always a parameter (`uint32_t now_ms`), never read from a clock.
  Randomness is a parameter or an injected function.
- No heap allocation except inside cJSON calls in `proto`. Callers provide
  the output structs.
- Every `core` module has a test file in `host_test/`.

If a function can be written in `core`, it belongs in `core`.

## Singletons and handles

The device has exactly one of each peripheral, so components are **static
singletons**: file-scope `static` state, `<module>_init()` called once from
`app_main`, no handle parameter.

```c
esp_err_t display_init(void);
void      display_set_brightness(uint8_t percent);
```

Use an **opaque handle** only where several instances exist at once or the
type is pure data logic that tests instantiate freely (everything in `core`):

```c
typedef struct reqq reqq_t;                    /* opaque when the layout is private */
typedef struct { ... } battery_filter_t;       /* plain struct when callers own storage */
void battery_filter_init(battery_filter_t *f);
```

`core` types are caller-allocated plain structs so they need no heap and can
live on a test's stack.

## Error handling

- Functions that can fail return `esp_err_t` (or `bool` in `core`, where
  ESP-IDF types are not available) and deliver results through out
  parameters.
- Init path, no sensible recovery: `ESP_ERROR_CHECK(...)`. Storage and WiFi
  are not in that class: a failed `nvs_flash_init` degrades to defaults, WiFi
  is then initialised with `nvs_enable = 0`, and a failed WiFi init is an
  application state shown to the user. The device must reach the console in
  every case.
- Runtime: check, log once at the place that can explain the failure, and
  return the error. Use `ESP_RETURN_ON_ERROR` / `ESP_GOTO_ON_ERROR` from
  `esp_check.h` for cleanup paths.
- Never `abort()` on network, storage, or peer input. A malformed frame is
  dropped and counted.
- NVS: never call `nvs_flash_erase()`. Keys are erased only by the explicit
  user actions (Unpair and `reset-pairing`: token and room id; Factory reset:
  the `devotchi` namespace). An init failure degrades to defaults. NVS has no
  multi-key transactions: when two keys belong together, write the one that
  matters last and do not rely on atomicity.
- Validate everything that crosses a boundary: every `snprintf` result, every
  string copy (`strlcpy`), every length from the network. Fixed buffers have a
  named size constant next to the type that owns them.

## Tasks, queues, events

- One owner per piece of state. The `app` task owns application state and is
  the only reader of the app event queue. Other contexts (WebSocket task,
  system event loop, esp_timer callbacks, console, `pwr`, `net`) translate
  what happened into an `app_event_t` and post it:

  ```c
  bool app_post(const app_event_t *ev);          /* from tasks, never blocks > 10 ms */
  bool app_post_from_isr(const app_event_t *ev); /* from ISRs */
  ```

- Events are small fixed-size structs passed by value. Large payloads travel
  as a pointer to a buffer from a static pool with a clear owner hand-off (the
  receiver releases it).
- Callbacks from ESP-IDF (event handlers, timer callbacks, the WebSocket
  event handler, button callbacks) do no work beyond copying data and posting
  an event. No blocking calls, no LVGL, no NVS in callbacks.
- Task name, stack size and priority are constants in one table in
  `app/include/app_tasks.h`. Create tasks with `xTaskCreate`; check the
  result. Stack sizes are justified by `uxTaskGetStackHighWaterMark` (console
  command `tasks`), not guessed upward.
- Every loop blocks on a queue, a notification or a delay. No busy waiting.
- Anything that can block for long (HTTP, TLS handshake) runs on the `net`
  task as a job; the result comes back as an event.
- Shared data read from another task goes through a function that copies a
  snapshot under a mutex (`app_get_snapshot`). No bare shared globals; no
  `volatile` flags as synchronisation.
- Bounded waits everywhere: no `portMAX_DELAY` on sends, queue posts or
  mutexes outside the task's own main receive.
- A library call that waits without a time limit is treated like a blocking
  network call and runs as a job on the `net` task. The known case:
  `esp_websocket_client_stop()` and `_destroy()` wait for the client task with
  `portMAX_DELAY`. The `app` task requests the stop and continues when
  `APP_EV_WS_STOPPED` arrives; a new client is created only after that.
- Read library event structs by their documented meaning, not by their field
  names: `esp_websocket_event_data_t.payload_len`/`payload_offset` describe
  one WebSocket frame, not one message, and the close code is delivered as a
  data event with opcode 8.

## LVGL

- LVGL is not thread-safe. All `lv_*` calls happen in the `ui` task or between
  `ui_lock()` and `ui_unlock()` (a recursive mutex). The public `ui_*`
  functions take the lock themselves; code outside the `ui` component never
  calls `lv_*` directly.
- LVGL callbacks (events, timers, animations) run in the `ui` task with the
  lock held. They must not block, and must not call into `net`, `storage` or
  `feedback`; they post an app event.
- Create a screen's objects once. Update labels and styles in place and only
  when the value changed (compare with the cached value first), so an idle
  screen causes no redraw.
- No `lv_obj_clean()` plus rebuild to change content.
- Colours, fonts, spacing, radii and motion come from `ui/ui_theme.h`
  (design-system 2-4 and 8; `UI_C_*`, `UI_FONT_*`, `UI_PAD`, `UI_R_*`,
  `UI_MS_*`) and the shared styles of `ui_theme.c`. No literal colours in
  screen or widget files (`grep -n lv_color_hex src/components/ui/screen_*.c`
  stays empty).
- Fonts are generated by `tools/gen_fonts.sh` (lv_font_conv 1.5.3, Figtree and
  JetBrains Mono, both OFL) and committed; the build never runs it. `display`
  (64 px, digits and pairing letters) and `title` (24 px, ASCII) only show
  strings the firmware owns. **Dynamic text** (project, device name, status
  line, question, plan, anything from the server or the user) uses `body`,
  `small` or `mono`, which carry the device charset (ASCII, Latin-1 and the
  typographic marks of protocol 14.1). Icons are glyphs of `ui_font_icons`
  (`ui_icons.h`), not images.
- Every text a label shows goes through `ui_label_set()`, which applies
  `text_sanitize_device()` and updates the label only when the text changed.
  `caps` labels (small, letter-spaced) use `ui_label_set_caps()`; letter
  spacing applies to the whole label, so caps text gets its own label.
- Screens are built from the widgets in `ui/widgets/` (status bar, button
  tabs, toast, list, code well, countdown, chip). A screen sets its button
  tabs and status-bar title with `ui_screen_set_tabs()` and
  `ui_screen_set_title()`; the screen manager applies them when the screen is
  loaded. A button without a tab does nothing on that screen, and a tab names
  the outcome of the press.
- Every screen and widget state has a `ui demo <name> [variant]` entry with
  constant sample data; its texts appear in `screen` (`texts`, `tabs`).
- Two internal DMA draw buffers of 40 lines. The flush callback waits
  (bounded) for the transfer it started last, starts the new one and calls
  `lv_display_flush_ready()` at once, so LVGL renders the next strip into the
  other buffer while this one is on the wire; a buffer is never rendered into
  while its transfer runs. The `ui` task calls `display_flush_wait()` before it
  releases the lock. With one buffer (the second failed to allocate) the
  callback waits for its own transfer. LVGL never spins in its own
  `while (flushing)` loop.
- LVGL's heap (`CONFIG_LV_MEM_SIZE_KILOBYTES`) is allocated in PSRAM
  (`LV_MEM_POOL_ALLOC` in `CMakeLists.txt`).
- `esp_lcd` handles are not thread-safe. Every panel call
  (`esp_lcd_panel_draw_bitmap`, `_disp_sleep`, `_disp_on_off`) is made by the
  `ui` task or with `ui_lock()` held. Because the `ui` task waits for the last
  transfer before it unlocks, no transfer is in flight while the lock is free.
  `esp_lcd_panel_disp_sleep()` on the ST7789 already delays 100 ms; do not
  add the datasheet delay a second time.

## Memory

- Prefer static storage with fixed capacity (queues, pools, the request
  queue, message buffers). Size constants are named and live next to the type.
- Stack: keep frames small; no buffers above 256 bytes on a task stack. JSON
  documents, URLs and frames use static or pool buffers.
- Heap: allowed at init (allocate once, never free) and inside libraries
  (cJSON, LVGL, mbedTLS, IDF). Application code does not `malloc` per message.
  Every `cJSON_Parse` has exactly one `cJSON_Delete` on every path.
- PSRAM (`heap_caps_malloc(n, MALLOC_CAP_SPIRAM)`) for large, non-DMA,
  non-ISR data: the shadow and snapshot framebuffers, the receive frame pool.
  Internal RAM (`MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA`) for the LVGL draw
  buffer handed to the SPI driver.
- Library allocations go to PSRAM where the library allows it, to keep
  internal RAM for WiFi, stacks and DMA: mbedTLS
  (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`), NimBLE
  (`CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL`), WiFi/LWIP
  (`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`), and cJSON through
  `cJSON_InitHooks()` set once in `app_main` (`core` itself stays free of
  ESP-IDF calls).
- Check every allocation. An init-time failure is fatal
  (`ESP_ERROR_CHECK`/explicit abort with a log line); a runtime failure is an
  error return.
- Secrets (token, WiFi passphrase, proof-of-possession) are never logged and
  never printed by the console.

## Power management

- `CONFIG_PM_ENABLE` is on; dynamic frequency scaling is configured by the
  `power` component (`esp_pm_configure`), not by `CONFIG_PM_DFS_INIT_AUTO`.
- Without a lock the CPU runs at the crystal frequency (40 MHz). Work that
  must be fast holds an `ESP_PM_CPU_FREQ_MAX` lock. The `power` component
  owns two locks and is the only code that takes them: `cpu` (held in stages
  `active` and `dim`) and `no_light_sleep` (held unless light sleep is
  allowed). Measure frame times and latencies with the `cpu` lock held.
- LEDC timers use `LEDC_USE_XTAL_CLK` so PWM frequency does not move with DFS.
- Nothing may sleep, light or deep, while `usb_serial_jtag_is_connected()`:
  the board has no BOOT button and USB is the only way to reflash it.
- Use the non-deprecated sleep API: `esp_sleep_get_wakeup_causes()` (a bit
  mask), `esp_sleep_enable_ext1_wakeup_io()`,
  `esp_sleep_get_ext1_wakeup_status()`.

## Configuration

| What | Where |
|---|---|
| ESP-IDF and component options | `sdkconfig.defaults` |
| Pins, LEDC timers/channels, SPI host, panel geometry | `board/include/board_pins.h` (the only place a GPIO number appears) |
| Tunable behaviour: default server URL, timeouts, thresholds, feature switches | `main/Kconfig.projbuild`, menu "Devotchi", read as `CONFIG_DEVOTCHI_*` |
| User settings changed at runtime | NVS through `storage`, with the Kconfig value as the default |
| Internal constants nobody should tune | `#define` in the owning module |

A value belongs in Kconfig when someone building for a different desk, server
or battery would reasonably change it. After editing `sdkconfig.defaults`,
delete `sdkconfig` and rebuild.

## Logging

- `ESP_LOGx(TAG, ...)` only; one `TAG` per `.c` file. No `printf` outside the
  `console` component, which owns the lines starting with `@@`.
- The debug console lives in the component directory `debug_console` (files
  `console.c`, `console.h`, prefix `console_`): a project component named
  `console` would replace ESP-IDF's own `console` component, which
  `protocomm` needs. It is the only writer to the USB port. Log output is
  routed through it (`esp_log_set_vprintf`) and every line, log or console,
  is written whole under one mutex.
- Levels: `E` operation failed; `W` unexpected but handled; `I` lifecycle
  events (boot, WiFi up, registered, paired, sleep); `D` per-message and
  periodic detail; `V` raw data.
- Nothing periodic at `I`. A steady-state device logs nothing.
- `core` does not log; it returns results and the caller logs.

## Comments

Default to none. Names carry intent.

Write a comment for:
- A hardware or ESP-IDF quirk, with a link or the datasheet page.
- An invariant the compiler cannot check (`/* caller holds ui_lock */`).
- A choice that looks wrong and is correct.
- `TODO:` with the concrete condition for removing it.

Never write a comment that:
- Restates the name or the code.
- Describes history or the current state of the work: "now", "for now",
  "temporary", "used to", "previously", "new", "added for X". That belongs in
  commits.
- Explains what the code does instead of why.

Doc comments: `/** ... */` above public declarations in headers, one sentence,
present tense; a second sentence only for a threading or lifetime rule. No
`@param`/`@return` boilerplate.

```c
/** Posts an event to the app task; returns false if the queue stayed full for 10 ms. */
bool app_post(const app_event_t *ev);
```

## Style

- 4 spaces, no tabs; braces on every block; K&R brace placement.
- `const` on everything that can be; pointer star next to the name
  (`const char *name`).
- Fixed-width integer types for data with a defined size; `size_t` for sizes;
  `bool` for flags.
- `enum` for sets of states; `switch` over an enum has no `default` when all
  values are handled, so the compiler reports a missing case.
- Designated initialisers for config structs; zero-initialise with `= {0}`.
- `static` on every function and variable not in a public header.
- No function-like macros where a `static inline` function works.
- Builds are warning-free. Do not silence a warning with a cast without a
  comment explaining why it is safe.
- ISR code uses only `*FromISR` APIs and `app_post_from_isr`.

## Host unit tests

```bash
. ~/.espressif/tools/activate_idf_v6.0.sh
cmake -S firmware/host_test -B /tmp/devotchi-host-test
cmake --build /tmp/devotchi-host-test
ctest --test-dir /tmp/devotchi-host-test --output-on-failure
```

- The host project compiles `src/components/core/*.c` with the host gcc
  (`-std=gnu17 -Wall -Wextra -Werror`, AddressSanitizer and UBSan on).
  Where the sanitizer runtimes (`libasan`, `libubsan`) are not installed,
  CMake prints a warning and builds with UBSan in trap mode only.
- cJSON comes from `managed_components/espressif__cjson/cJSON` and Unity from
  `$IDF_PATH/components/unity/unity/src` when those directories exist.
  Otherwise `host_test/CMakeLists.txt` fetches pinned releases with
  `FetchContent` (cJSON `v1.7.19`, ThrowTheSwitch Unity `v2.6.1`), so the
  tests build on a fresh clone and in CI with only gcc and cmake. The ESP-IDF
  activation line above is optional. `CJSON_NESTING_LIMIT=1000` is defined as
  in the IDF build.
- One test file per module: `host_test/test_<module>.c`.
- Tests are table-driven where inputs repeat, cover every branch of the state
  machines (`power_fsm`, `btn_fsm`, `battery`, `reqq`), and include malformed
  and oversized input for `proto`, `url` and `text`.
- A change to `core` is not complete until its host tests pass.

## Building and flashing

```bash
. ~/.espressif/tools/activate_idf_v6.0.sh
cd firmware
python "$IDF_PATH/tools/idf.py" build
python "$IDF_PATH/tools/idf.py" -p /dev/ttyACM0 flash
```

Never run `erase-flash`, `erase_region` over 0x9000..0xFFFF, or esptool with
`--erase-all`. The NVS partition holds WiFi credentials and the pairing.

## Commits

Short subject under 72 characters with a conventional prefix (`feat:`, `fix:`,
`docs:`, `chore:`, `refactor:`). No body unless needed. No `Co-authored-by`,
no AI attribution.
