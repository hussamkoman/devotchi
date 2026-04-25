# Firmware Coding Conventions

These rules exist so anyone (including future-you) can read, modify, and extend
this firmware without surprises. They are opinionated. Follow them unless there's
a specific, documented reason not to.

---

## Project layout

```
firmware/
├── CMakeLists.txt              project file — points at src/
├── sdkconfig.defaults          IDF framework knobs (committed)
├── sdkconfig                   generated from defaults (gitignored)
├── partitions.csv              flash partition table
├── dependencies.lock           component manager lockfile (committed)
├── .clangd                     LSP config — tells clangd about ESP-IDF flags
├── .gitignore
├── CONVENTIONS.md              this file
└── src/
    ├── main/                   App + entry point (the "main" component)
    └── components/             one folder per component
        └── <name>/
            ├── CMakeLists.txt
            ├── include/        public headers — visible to other components
            └── *.cpp           private implementation
```

### Why `src/`

Keeps code separated from build artifacts and project-level config files. The
root `CMakeLists.txt` adds `src/main/` and `src/components/` to
`EXTRA_COMPONENT_DIRS` so ESP-IDF finds them.

### Why `include/`

ESP-IDF's `INCLUDE_DIRS` setting controls which headers are visible to other
components. Files in `include/` are part of the **public API**. Files at the
component root or in `src/` (if added) are private. This is enforced by CMake,
not just convention — putting headers in the wrong place silently breaks
encapsulation.

---

## Naming

| Thing | Convention | Example |
|---|---|---|
| Files | `PascalCase.{hpp,cpp}` matching primary class | `Wifi.hpp`, `BlinkTask.cpp` |
| Classes / structs | `PascalCase` | `WsClient`, `ButtonEvent` |
| Methods / functions | `snake_case` | `set_token`, `blink_loop` |
| Member variables | `snake_case_` (trailing underscore) | `storage_`, `blink_period_ms_` |
| Constants / constexpr | `UPPER_SNAKE` | `WIDTH`, `DEBOUNCE_MS` |
| Namespaces | `lower_snake` | `devotchi::ws_client` |
| Enum values | `PascalCase` (in `enum class`) | `Mood::Happy`, `State::Connected` |
| Macros | `UPPER_SNAKE`, prefer `constexpr` instead | (avoid macros except for `TAG` / `ESP_LOG*`) |

The `_` suffix on member variables disambiguates them from parameters and locals
without needing `this->` everywhere.

### Namespaces

Every component lives under `devotchi::<component_name>`. Nested types (e.g.,
`Buttons::ButtonEvent`) are fine when the type only makes sense in context.

```cpp
namespace devotchi::feedback {
    class Feedback { ... };
    class BlinkTask : public runtime::Task { ... };
}
```

Use `using namespace` only inside `.cpp` files, never in headers, and only for
small, well-known namespaces (`std::chrono_literals` is OK; `using namespace std;`
is not).

---

## Header / source split

**Rule:** class declarations in `.hpp`, method bodies in `.cpp`. No exceptions
beyond what the language requires.

```cpp
// Wifi.hpp
class Wifi {
public:
    void init();
    void start();
private:
    State state_ = State::Disconnected;
};

// Wifi.cpp
void Wifi::init() { /* body */ }
void Wifi::start() { /* body */ }
```

### Allowed in headers

- Class definitions (the `class { ... };` block).
- Trivial accessors only when they are one expression (`int value() const { return n_; }`).
- `constexpr` constants and `constexpr` functions.
- `inline` global constants (`inline constexpr`).
- Templates — required by the language, no choice.

### Forbidden in headers

- Multi-line method bodies.
- Logic that pulls heavy dependencies (e.g., never `#include "esp_wifi.h"` in a
  public header — let the `.cpp` include it).
- Definitions of non-template free functions without `inline`.

If a header is large enough that you want to "just put a body here for
convenience," instead split that work out into a helper or move it to the `.cpp`.

---

## Includes

### Order

Within each `.cpp`:

```cpp
#include "MyClass.hpp"          // 1. own header first

#include "Config.hpp"           // 2. other project headers, alphabetical
#include "OtherComponent.hpp"

#include "driver/gpio.h"        // 3. ESP-IDF / system headers, alphabetical
#include "esp_log.h"
#include "freertos/FreeRTOS.h"  // IWYU pragma: keep — required first by task.h
#include "freertos/task.h"

#include <cstdint>              // 4. C++ standard library last
#include <string>
```

Putting your own header first ensures it includes everything it needs — if it
silently relies on a transitive include, the build breaks immediately.

### Relative paths

Always include other components' public headers by their bare name:

```cpp
#include "Wifi.hpp"             // ✓
#include "../wifi/include/Wifi.hpp"   // ✗ never
```

The CMake `INCLUDE_DIRS` mechanism makes the bare name work.

### IWYU pragmas

When clangd flags an include as "unused" but it's actually required (most
common: `freertos/FreeRTOS.h` needed before `task.h`), add:

```cpp
#include "freertos/FreeRTOS.h"  // IWYU pragma: keep — required first by task.h
```

Always include the *reason* in the comment.

---

## Comments

Default to writing **no comments**. Comments rot, names don't. Add a comment
only when the *why* is non-obvious to a cold reader — not the *what*.

### The cold-reader test

Before writing a comment, ask: would this still read as true and useful to
someone opening this file 6 months from now, with zero memory of why I'm
writing it today?

If the comment encodes *current session state* — what just changed, what we
plan to do next, what was here before, what's "for now" — it fails the test.
Don't write it. That information belongs in the commit message and the issue
tracker, not in the code.

### Banned phrases (any tense)

Comments must not contain:

- **Time-relative**: "now", "currently", "as of writing", "at the moment".
- **Future-reference**: "will be", "going to", "soon", "eventually", "later".
- **Past-reference**: "previously", "used to", "old", "removed for now".
- **Provisional**: "temporary", "for now", "placeholder", "stopgap", "smoke test".
- **Self-referential**: "added for ticket X", "this fixes Y", "see commit Z".

If something really is throwaway, encode it in the **name** (e.g., `_TODO`,
`Test`, `Sample` suffix), and add a `TODO:` with concrete removal criteria.

### Write a comment when

- A workaround for a hardware/IDF quirk — link to the upstream issue.
- A non-obvious invariant the type system can't express
  (`// caller must hold gpio_mutex_`).
- Why a "wrong-looking" choice is correct
  (`// stack fixed at 2 KB — measured high-water under -O0`).
- A `TODO:` with a **concrete trigger** for action
  (`// TODO: switch to PWM once esp_driver_ledc is in REQUIRES`).
- A class- or component-level doc explaining what it owns and the design
  contract (kept short — a few lines).

### Don't write a comment for

- What the code does — let names do that work.
- Re-stating a function or class name in prose.
- The reason for the commit you're about to make.
- Section dividers in tiny files. (For files > 200 lines, a `── Foo ──` band
  is fine.)

### Tone

- One-liners: sentence fragments OK, lowercase OK, no trailing period.
- Multi-line: full sentences, full punctuation.
- No emojis. No ASCII art unless it genuinely clarifies (state diagram, memory
  layout, signal timing).

### Doc comments — format

Two comment styles, each with one purpose. Don't mix them.

| Style | Used for |
|---|---|
| `/** ... */` | **Head comments** — above types, public methods, files. Declarative. |
| `// ...`     | **Inline** — single-line notes, IWYU pragmas, TODOs, local invariants. |

Doxygen-shaped, but we don't generate Doxygen output. The `/** */` is purely
to make heads visually distinct from inline noise.

#### Layout

One-liner — opening, content, closing on the same line:

```cpp
/** Spawns the FreeRTOS task. Idempotent — no-op if already running. */
void spawn();
```

Multi-line — opening and closing on their own lines, each content line
prefixed with ` * `, blank lines as ` *`:

```cpp
/**
 * Owns one FreeRTOS task tied to a single hardware-backed responsibility.
 *
 * Subclass and override run() with the task body. Construct with a Spec
 * (name + stack + priority); spawn() creates the underlying task, stop()
 * requests cooperative shutdown.
 *
 * Not copyable or movable — task identity is bound to its handle.
 */
class Task {
```

Wrap content lines at ~80 columns. Use full sentences with periods. Tense is
present indicative ("spawns", "owns", "returns") — not future ("will spawn").

### Doc comments — content rules

Only write a head comment when the name + signature don't already say it.
When you do, keep it short and direct.

**Default shape**: one sentence saying what the thing does. Add a second
sentence only if there's a non-obvious gotcha worth flagging — typically
threading, lifetime, or a thread-safety caveat.

That's it. No checklist, no labelled sections. If you need more than two
sentences to describe one type, the type is doing too much.

**Always omit:**

- Restating the thing's own name in prose.
- Implementation details — those go inline in the `.cpp` if anywhere.
- `@param`, `@returns`, `@throws` — write prose.
- `@author`, `@date`, `@version`, change history — git owns this.
- Session state ("recently added", "temporary", "TODO is being worked on").

### Examples

```cpp
/** Battery reading in millivolts. */
struct BatterySample { uint16_t millivolts; };
```

```cpp
/**
 * Persistent settings backed by NVS. Call init() once before any getter
 * or setter; getters/setters are thread-safe.
 */
class Storage {
public:
    void init();
    std::optional<WifiCredentials> wifi_creds() const;
    void set_wifi_creds(const WifiCredentials& creds);
};
```

```cpp
/**
 * Drives the status LED and vibration motor. Synchronous; not thread-safe.
 * For background behaviour, wrap with a Task subclass (see BlinkSmokeTest).
 */
class Feedback { /* ... */ };
```

```cpp
/** Creates the FreeRTOS task. Idempotent — no-op if already running. */
void spawn();
```

### Anti-examples

```cpp
/** This is the Feedback class. */                       // restates the name
/** Initializes the feedback component. */               // restates init()'s name
/**
 * @brief    Sets LED brightness
 * @param pct  Brightness percent (0-100)
 * @returns void
 */                                                       // Doxygen ceremony for nothing
/** Method to set brightness. We changed it from bool. */ // session/history

### Bad / good examples

```cpp
// BAD — session state
void led_set(uint8_t pct);              // 0-100 — digital for now, will be PWM

// GOOD — just the contract
void led_set(uint8_t pct);              // 0-100
```

```cpp
// BAD — explains what, not why
// Spawns a task that runs Feedback::blink_loop. Temporary smoke test —
// will go away once we wire real notification patterns.
class BlinkTask : public runtime::Task { ... };

// GOOD — name encodes intent, no rotting prose
class BlinkSmokeTest : public runtime::Task { ... };
// TODO: remove once Notifier handles LED feedback
```

```cpp
// BAD — self-evident
i++;   // increment counter

// GOOD — encodes a real constraint
buf_pos_ = (buf_pos_ + 1) % BUF_SIZE;   // ring buffer wrap; assumes power-of-two BUF_SIZE
```

---

## Components

A component is a directory under `src/components/` with its own
`CMakeLists.txt`. Use components to express **dependency boundaries**, not just
file grouping.

### When to make a new component

- It owns a distinct piece of state or hardware.
- It has a clear public API others should depend on.
- You can plausibly imagine reusing it across projects.

### When NOT to

- "Utility" or "helpers" — these turn into god-buckets. Keep helpers next to
  their consumer until two consumers exist.
- Tiny single-file logic with no consumers — leave it in the consuming
  component.

### REQUIRES vs PRIV_REQUIRES

```cmake
idf_component_register(
    SRCS "Wifi.cpp"
    INCLUDE_DIRS "include"
    REQUIRES        nvs_flash esp_wifi   # in public headers — others need them
    PRIV_REQUIRES   esp_event            # only in .cpp — others don't need them
)
```

Be honest about which includes appear in your `include/` headers and which only
appear in `.cpp`. Lying to CMake here breaks the build for whoever pulls you in.

### Public/private headers

```
component/
├── include/Foo.hpp     ← public — others can #include "Foo.hpp"
├── Foo.cpp
└── FooImpl.hpp         ← private — sibling to .cpp, invisible outside
```

If you ever have private headers, set `PRIV_INCLUDE_DIRS "."` in CMakeLists.

---

## Tasks (background work)

We use the **Active Class** pattern: the work is a method on the component;
the FreeRTOS task is a separate `Task` subclass that calls it.

### Component side — pure logic

```cpp
class Feedback {
public:
    void init();
    void blink_loop(uint32_t period_ms);   // task body — also callable directly for tests
};
```

The component knows nothing about `xTaskCreate`. It only references FreeRTOS for
yield primitives (`vTaskDelay`, queues, semaphores).

### Spawner — Task subclass in the same component file

```cpp
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

```cpp
BlinkTask::BlinkTask(Feedback& target, uint32_t period_ms)
    : Task(config::tasks::FEEDBACK_BLINK), target_(target), period_ms_(period_ms) {}

void BlinkTask::run() {
    target_.blink_loop(period_ms_);
}
```

### Task config — one place

Every task's name, stack size, and priority lives in `config::tasks` in
`Config.hpp`. The `Task` base class reads from it via the `Spec` ctor argument.
Don't hardcode these in the component.

```cpp
namespace tasks {
    inline constexpr Spec FEEDBACK_BLINK = { "feedback_blink", 2048, 2 };
}
```

### App holds and spawns

```cpp
// App.hpp — tasks declared after the components they reference.
feedback::Feedback   feedback_;
feedback::BlinkTask  blink_task_{feedback_, 500};

// App.cpp
feedback_.init();
// ... after all init's ...
blink_task_.spawn();
```

### Task body conventions

- Always `vTaskDelay` somewhere in your loop, even if just `vTaskDelay(1)`.
  Without it, lower-priority tasks (including IDLE) starve and the watchdog
  trips.
- Check `should_stop()` if your task needs cooperative shutdown.
- Reasonable default stack: 2 KB. Bump only when actual high-water-mark
  measurements demand it.

---

## Configuration

Two layers — keep them separate:

| What | Where | Example |
|---|---|---|
| ESP-IDF framework knobs | `sdkconfig.defaults` | `CONFIG_FREERTOS_HZ=1000` |
| Application constants | `Config.hpp` (`devotchi::config::*`) | `DEBOUNCE_MS = 30` |

`sdkconfig.defaults` is for things ESP-IDF needs to know at build time
(framework features on/off, partition layout, RTOS tick rate). Your app's pin
map, server URLs, timing constants — everything else — goes in `Config.hpp` as
`constexpr` in the appropriate sub-namespace.

When you tweak `sdkconfig.defaults`, delete the generated `sdkconfig` and run
`idf.py reconfigure` so the defaults take effect.

---

## Logging

Every component has a single tag at the top of its `.cpp`:

```cpp
namespace {
constexpr const char* TAG = "Feedback";
}
```

Use the appropriate level:

| Level | Use for |
|---|---|
| `ESP_LOGE(TAG, ...)` | Error — operation failed, recoverable but worth flagging |
| `ESP_LOGW(TAG, ...)` | Warning — unexpected but handled |
| `ESP_LOGI(TAG, ...)` | Info — major lifecycle events (boot, connected, paired) |
| `ESP_LOGD(TAG, ...)` | Debug — verbose, off in release |
| `ESP_LOGV(TAG, ...)` | Verbose — every event, only when actively debugging |

Default level is INFO (`CONFIG_LOG_DEFAULT_LEVEL_INFO=y`). Don't use `printf`
directly — `ESP_LOG*` is filterable per tag and adds timestamps.

---

## Error handling

ESP-IDF uses `esp_err_t` return codes. For init code where there's no useful
recovery, wrap calls in `ESP_ERROR_CHECK(...)` — it logs + aborts on failure.
For runtime code that can recover, check the return value and handle.

```cpp
// Init — abort on failure, no recovery possible
ESP_ERROR_CHECK(gpio_config(&io));

// Runtime — handle gracefully
esp_err_t err = nvs_set_str(handle, "token", value);
if (err != ESP_OK) {
    ESP_LOGW(TAG, "failed to save token: %s", esp_err_to_name(err));
    return;
}
```

We have C++ exceptions enabled (`CONFIG_COMPILER_CXX_EXCEPTIONS=y`) but don't
use them for control flow — only for genuine programmer errors (e.g., a
`std::variant` access that should never happen).

---

## Memory

- **Stack-allocate by default.** `App` and all components live on the main task's
  stack. No `new` / `make_unique` unless you have a specific reason.
- **PSRAM** is available (2 MB) for large allocations like display framebuffers
  via `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`. Use only when needed.
- **No globals with non-trivial constructors** — they run before
  `app_main` and can fail silently. Stick to `constexpr` globals.

---

## Embedded C++ specifics

The opinionated bits — what subset of C++ we use, and what we deliberately
avoid because we're on a microcontroller.

### Language features — use

- **`constexpr` everywhere it's legal.** Compile-time evaluation, no runtime
  cost, type-safe. Pin maps, sizes, timing constants — all `constexpr`.
- **`enum class`** always; never plain `enum`. Avoids implicit int conversions
  and global namespace pollution.
- **`override` and `final`** on every overridden virtual. The compiler catches
  signature drift; without `override` a typo silently creates a new method.
- **`[[nodiscard]]`** on functions returning errors or non-trivial values the
  caller must look at (`esp_err_t` wrappers, `try_lock`, parsers).
- **`explicit`** on every single-argument constructor. Implicit conversions
  cause action-at-a-distance bugs.
- **`noexcept`** on functions that genuinely don't throw. Required for move
  ops to be picked by stdlib containers; reduces code size.
- **`std::optional`, `std::variant`, `std::string_view`** — all stack-friendly,
  zero-allocation. Use them.
- **Strong types** for IDs (e.g., `enum class TaskHandle : uint32_t`), not
  bare `int`s.

### Language features — avoid

- **`new` / `delete`** (and friends like `make_unique` for normal flow). For
  one-shot init allocations that need PSRAM, use
  `heap_caps_malloc(MALLOC_CAP_SPIRAM)` explicitly so the location is visible
  at the call site.
- **RTTI** (`dynamic_cast`, `typeid`). Off in our build. Use `enum class` tags
  + `std::variant` instead.
- **Exceptions for control flow.** They're enabled (`CONFIG_COMPILER_CXX_EXCEPTIONS=y`)
  but reserve them for genuine "should never happen" — `std::variant` misuse,
  out-of-bounds in a `std::array` etc. Hardware/network failures use `esp_err_t`.
- **STL containers that allocate (`std::vector`, `std::string`, `std::map`)** in
  hot paths or ISRs. Fine in init code. For long-lived storage, prefer
  `std::array<T, N>` with a known bound.
- **`std::shared_ptr`** in hot paths — atomic refcount is expensive on MCUs.
- **`using namespace`** in headers. In `.cpp` only when scoped tightly
  (function-local, or a small known namespace like `std::chrono_literals`).
- **Multiple inheritance** except for purely-abstract interface mixins.

### Hardware-aware patterns

- **`volatile`** for memory-mapped I/O — never optimize away a hardware read.
  ESP-IDF wraps most MMIO in driver APIs; if you ever poke registers directly,
  use `volatile` and document why.
- **`alignas(N)`** on buffers used by DMA. ESP32-S3 needs 4-byte alignment for
  most DMA, larger for some peripherals. Wrong alignment silently corrupts.
- **ISR safety**: in an ISR, only call `*FromISR` variants
  (`xQueueSendFromISR`, `xSemaphoreGiveFromISR`). Never `printf`, never
  `vTaskDelay`, never blocking calls. Keep ISRs short — defer real work to a
  task via a queue.
- **`portYIELD_FROM_ISR()`** if a `*FromISR` call wakes a higher-priority task.

### Stack discipline

- **No recursion.** Worst-case depth is unknowable; stacks are small (2-8 KB).
- **No large local arrays.** > 256 bytes on the stack is suspect; > 1 KB is
  almost certainly wrong. Move to PSRAM or `static` if persistent.
- **Default task stack: 2 KB.** Bump only when high-water-mark measurements
  via `uxTaskGetStackHighWaterMark()` justify it.
- **Watch for hidden allocations**: `std::function`, `std::string`, formatting
  with `printf`-style — these can blow stacks under deep call chains.

### Initialization

- **Constructors do nothing that can fail.** Default-construct cheaply, then
  call `init()` for hardware setup. Reasoning: we don't use exceptions for
  control flow, so ctors can't report errors; ordering also matters and is
  hard to enforce in member-init lists.
- **Initialize members in declaration order** (compiler enforces; respect it
  in the init list).
- **No globals with constructors that touch hardware** — they run before
  `app_main`, before `nvs_init`, before WiFi. Stick to `constexpr` for
  globals.

### Resource ownership — Rule of 0 / Rule of 5

- **Rule of 0**: if your class doesn't own a resource, write no copy/move/dtor
  ops. Let the compiler default them.
- **Rule of 5**: if your class *does* own a resource that can't be safely
  copied (e.g., a FreeRTOS handle, a file descriptor), explicitly `= delete`
  the copy ops, and either implement or `= delete` the move ops. Half-defined
  classes are a bug source.
- See `runtime::Task` for an example: `= delete` on all four copy/move ops
  because task identity is tied to a single FreeRTOS handle.

### `const` discipline

- **Member functions** that don't mutate state: mark `const`. The compiler
  will tell you when you got it wrong.
- **Parameters by value** that you don't modify: don't add `const`
  (it's pointless on a value param signature).
- **Parameters by reference** to data you don't modify: `const T&` always.
- **Local variables** that don't change: `const auto x = ...` — small habit,
  catches a class of bugs.

### Style conventions

- **Always braces**, even single-line `if`/`else`/`for`/`while`. Cheap, removes
  a class of bugs.
- **East const or west const?** West const (`const T*`, not `T const*`). Pick
  one, stay consistent.
- **`nullptr`** never `NULL` or `0` for pointers.
- **`constexpr` numeric literals**: use `1'000'000` over `1000000` past 4
  digits. Use `0x` for bitmasks, `'\n'` for newlines.

---

## Commit messages

Short subject under 72 chars, conventional prefix:

```
feat: scaffold ESP-IDF firmware with C++ component layout
fix: correct sdkconfig flash size for N4R2 module
refactor: extract Task base class for FreeRTOS spawning
docs: add CONVENTIONS.md
chore: bump esp_websocket_client to 1.5
```

No body unless the change really needs it. No "Co-authored-by", no AI
attribution, no emojis.

---

## When in doubt

Read existing code in this repo. If the answer isn't clear from the code,
prefer the simpler option, the option with fewer dependencies, and the option
that puts heavy includes in `.cpp` rather than `.hpp`.

If you want to deviate from these rules, do it in a separate commit and put the
reason in the commit message.
