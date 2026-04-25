#pragma once

#include <cstdint>

namespace devotchi::runtime {

/**
 * Wraps one FreeRTOS task. Subclass and override run() with the task body;
 * not copyable — identity is tied to the underlying handle.
 */
class Task {
public:
    struct Spec {
        const char* name;
        uint32_t    stack_bytes;
        unsigned    priority;     // 0 = lowest, configMAX_PRIORITIES - 1 = highest
    };

    explicit Task(Spec spec);
    virtual ~Task();

    // Non-copyable, non-movable — task identity is tied to its FreeRTOS handle.
    Task(const Task&)            = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&&)                 = delete;
    Task& operator=(Task&&)      = delete;

    /** Creates the task. Idempotent — no-op if already running. */
    void spawn();

    /** Asks the task to exit. The body must check should_stop() to honour it. */
    void stop();

    bool is_running() const;

protected:
    /** Task body — provided by the subclass. */
    virtual void run() = 0;

    bool should_stop() const { return stop_requested_; }

private:
    static void entry_trampoline(void* arg);

    Spec  spec_;
    void* handle_         = nullptr;   // FreeRTOS TaskHandle_t — opaque here
    bool  stop_requested_ = false;
};

}  // namespace devotchi::runtime
