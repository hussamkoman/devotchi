# Firmware Coding Conventions

## Layout

```
firmware/
├── CMakeLists.txt              project file
├── sdkconfig.defaults          IDF framework knobs (committed)
├── sdkconfig                   generated (gitignored)
├── partitions.csv              flash partition table
├── dependencies.lock           component manager lockfile (committed)
├── .clangd                     LSP config
└── src/
    ├── main/                   App + entry point
    └── components/<name>/
        ├── CMakeLists.txt
        ├── include/            public headers
        ├── *.cpp               implementation
        └── *Impl.hpp           private headers (sibling to .cpp)
```

Headers in `include/` are visible to other components; anything else is private.

---

## Naming

| Thing             | Convention                          | Example                          |
|-------------------|-------------------------------------|----------------------------------|
| Files             | `PascalCase.{hpp,cpp}` matching primary class | `Wifi.hpp`, `BlinkTask.cpp` |
| Classes / structs | `PascalCase`                        | `WsClient`, `ButtonEvent`        |
| Methods           | `snake_case`                        | `set_token`, `blink_loop`        |
| Members           | `snake_case_` (trailing underscore) | `storage_`, `period_ms_`         |
| Constants         | `UPPER_SNAKE`                       | `WIDTH`, `DEBOUNCE_MS`           |
| Namespaces        | `lower_snake`                       | `devotchi::ws_client`            |
| Enum values       | `PascalCase` (always `enum class`)  | `Mood::Happy`                    |

Each component lives under `devotchi::<component_name>`. `using namespace` is
forbidden in headers and discouraged in `.cpp` outside narrow scopes
(`std::chrono_literals` is fine).

---

## Header / source split

Declarations in `.hpp`, bodies in `.cpp`.

Allowed in headers:
- Class declarations.
- Trivial one-expression accessors.
- `constexpr` constants and functions.
- `inline constexpr` globals.
- Templates.

Forbidden in headers:
- Multi-line method bodies.
- Heavy IDF includes (e.g. `esp_wifi.h`) — let the `.cpp` pull them in.
- Free function definitions without `inline`.

---

## Includes

In each `.cpp`:

```cpp
#include "MyClass.hpp"          // own header first

#include "Config.hpp"           // project headers, alphabetical
#include "OtherComponent.hpp"

#include "driver/gpio.h"        // ESP-IDF / system, alphabetical
#include "esp_log.h"
#include "freertos/FreeRTOS.h"  // IWYU pragma: keep — required first by task.h
#include "freertos/task.h"

#include <cstdint>              // C++ stdlib last
```

Own header first ensures it pulls everything it needs — no relying on transitive
includes. Reference other components by bare header name; CMake handles paths.

When clangd flags an include as unused but it's load-bearing (typically
`freertos/FreeRTOS.h` before `task.h` / `queue.h`), suppress with
`// IWYU pragma: keep — <reason>`.

---

## Comments

Default to none. Names carry intent; comments rot, names don't.

Write a comment for:
- Hardware or IDF quirks — link the upstream issue.
- Invariants the type system can't express (`// caller must hold gpio_mutex_`).
- A "wrong-looking" choice that's actually correct.
- `TODO:` with a concrete trigger for removal.

Never write a comment that:
- Restates the name.
- Encodes session state — "now", "for now", "will be", "used to", "temporary",
  "previously", "added for ticket X". This belongs in commits and tickets.
- Explains *what* the code does instead of *why*.

If something really is throwaway, encode it in the type name (`Sample`, `Test`
suffix) and pair with a `TODO:` that names its removal trigger.

---

## Doc comments

`/** ... */` for heads above types and public methods. `// ...` for inline notes.

One-liner — opening, content, closing on one line:

```cpp
/** Spawns the FreeRTOS task. Idempotent — no-op if already running. */
void spawn();
```

Multi-line — wrap at ~80 cols, blank lines as ` *`, present tense:

```cpp
/**
 * Owns one FreeRTOS task tied to a single hardware-backed responsibility.
 * Subclass and override run(); spawn() creates the underlying task and stop()
 * requests cooperative shutdown. Not copyable or movable.
 */
class Task {
```

Default to one sentence. A second sentence only for a non-obvious gotcha
(threading, lifetime). If you need three, the type does too much.

Don't use `@param`, `@returns`, `@throws`, `@author`, `@date`, `@version`.
Prose, not Doxygen ceremony. Git owns history.

---

## Components

A component owns distinct hardware or state and exposes a public API. It's not
a file-grouping tool. No "utility" or "helpers" components — keep helpers next
to their consumer until two consumers exist.

```cmake
idf_component_register(
    SRCS "Wifi.cpp"
    INCLUDE_DIRS "include"
    REQUIRES        nvs_flash esp_wifi   # appears in public headers
    PRIV_REQUIRES   esp_event            # only in .cpp
)
```

Be honest about the split — lying breaks the build for whoever depends on you.

If you have private headers, set `PRIV_INCLUDE_DIRS "."`.

---

## Tasks

Active Class pattern. The work is a method on the component; a `Task` subclass
spawns the FreeRTOS task and calls into the component.

```cpp
class Feedback {
public:
    void blink_loop(uint32_t period_ms);   // task body, also directly callable
};

class BlinkTask : public runtime::Task {
public:
    BlinkTask(Feedback& target, uint32_t period_ms);
protected:
    void run() override;
private:
    Feedback& target_;
    uint32_t  period_ms_;
};
```

The component knows nothing about `xTaskCreate`. The spawner knows nothing
about hardware.

Task name / stack / priority lives in `config::tasks`, never hardcoded:

```cpp
namespace tasks {
    inline constexpr Spec FEEDBACK_BLINK = { "feedback_blink", 2048, 2 };
}
```

Inside the loop:
- Always `vTaskDelay` somewhere — without it the watchdog trips.
- Check `should_stop()` if the task supports cooperative shutdown.
- Default stack 2 KB; bump only when `uxTaskGetStackHighWaterMark()` says so.

---

## Configuration

| What                       | Where                | Example                  |
|----------------------------|----------------------|--------------------------|
| ESP-IDF framework knobs    | `sdkconfig.defaults` | `CONFIG_FREERTOS_HZ=1000`|
| App constants (pins, etc.) | `Config.hpp`         | `DEBOUNCE_MS = 30`       |

Pin maps, server URLs, timing constants — all `constexpr` in
`devotchi::config::*`.

After editing `sdkconfig.defaults`, delete `sdkconfig` and run
`idf.py reconfigure`.

---

## Logging

One tag per `.cpp`:

```cpp
namespace {
constexpr const char* TAG = "Feedback";
}
```

| Level | Use for                                                           |
|-------|-------------------------------------------------------------------|
| `E`   | Error — operation failed                                          |
| `W`   | Warning — unexpected but handled                                  |
| `I`   | Info — major lifecycle events (boot, connected, paired)           |
| `D`   | Debug — verbose, off in release                                   |
| `V`   | Verbose — every event                                             |

Default level is INFO. Never `printf` directly.

---

## Error handling

```cpp
// Init — no useful recovery
ESP_ERROR_CHECK(gpio_config(&io));

// Runtime — handle
esp_err_t err = nvs_set_str(handle, "token", value);
if (err != ESP_OK) {
    ESP_LOGW(TAG, "failed to save token: %s", esp_err_to_name(err));
    return;
}
```

C++ exceptions are enabled but reserved for "should never happen" cases
(`std::variant` misuse, out-of-bounds). Hardware and network failures use
`esp_err_t`.

---

## Memory

- Stack-allocate by default. No `new` / `make_unique` without a reason.
- PSRAM via `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)` for large allocations
  (display framebuffers, etc.).
- No globals with non-trivial constructors — they run before `app_main` and
  fail silently. `constexpr` only.

---

## Embedded C++ subset

Use:
- `constexpr` wherever legal.
- `enum class` always; never plain `enum`.
- `override` and `final` on every overridden virtual.
- `[[nodiscard]]` on functions returning errors or values the caller must check.
- `explicit` on every single-argument constructor.
- `noexcept` on functions that genuinely don't throw.
- `std::optional`, `std::variant`, `std::string_view`.
- Strong types for IDs (e.g. `enum class TaskHandle : uint32_t`).

Avoid:
- `new` / `delete` and `make_unique` for normal flow.
- RTTI (`dynamic_cast`, `typeid`) — off in our build.
- Exceptions for control flow.
- Allocating STL containers (`vector`, `string`, `map`) in hot paths or ISRs.
- `std::shared_ptr` in hot paths.
- Multiple inheritance, except for purely-abstract interface mixins.

---

## Hardware-aware patterns

- `volatile` for direct MMIO. (Driver APIs wrap most of it; if you ever poke
  registers, document why.)
- `alignas(N)` on DMA buffers. ESP32-S3 needs 4-byte minimum; some peripherals
  need more.
- ISR safety: only `*FromISR` variants. No `printf`, no `vTaskDelay`, no
  blocking. Defer real work to a task via a queue.
- `portYIELD_FROM_ISR()` after waking a higher-priority task.

---

## Initialization

- Constructors do nothing fallible. Default-construct cheaply, then call
  `init()` for hardware setup.
- Initialize members in declaration order.

---

## Resource ownership

- Rule of 0 if the class owns no resource — let the compiler default
  copy/move/dtor.
- Rule of 5 if it does — explicitly `= delete` whichever ops aren't safe. Half-
  defined classes are a bug source. See `runtime::Task` for an example.

---

## Style

- Always braces, even single-line `if` / `for` / `while`.
- West const (`const T*`, not `T const*`).
- `nullptr`, never `NULL` or `0`.
- Numeric literals past 4 digits use `'` (`1'000'000`).
- `0x` for bitmasks, `'\n'` for newlines.

---

## Commits

Short subject under 72 chars, conventional prefix:

```
feat: scaffold ESP-IDF firmware with C++ component layout
fix: correct sdkconfig flash size for N4R2 module
refactor: extract Task base class for FreeRTOS spawning
```

No body unless needed. No `Co-authored-by`, no AI attribution, no emojis.
