#pragma once

#include "cJSON.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/** Longest line the device writes, including the "@@..." prefix and the newline. */
#define CONSOLE_OUT_MAX   3072
/** Longest command line accepted from the host, without the newline. */
#define CONSOLE_LINE_MAX  1024
#define CONSOLE_MAX_ARGS  8
#define CONSOLE_MAX_CMDS  32

typedef enum {
    CONSOLE_OK,
    CONSOLE_ERR_BAD_ARGS,
    CONSOLE_ERR_INVALID_STATE,
    CONSOLE_ERR_BUSY,
    CONSOLE_ERR_FAILED,
} console_rc_t;

/**
 * Runs in the console task. argv[0] is the command name; raw_args is the
 * untokenised text after it. The console writes the terminator line from the
 * return value, so a handler writes payload lines only.
 */
typedef console_rc_t (*console_handler_t)(int argc, char **argv, const char *raw_args);

typedef struct {
    const char *name;
    const char *help;
    console_handler_t handler;
} console_cmd_t;

/**
 * Installs the USB-Serial-JTAG driver and routes ESP_LOG output through the
 * console writer. reset_reason must outlive the console (a string literal).
 */
esp_err_t console_init(const char *reset_reason);

/** Starts the reader task, which announces itself with one @@READY line. */
esp_err_t console_start(const char *task_name, uint32_t stack_size, uint32_t priority);

/** Adds a command; cmd must stay valid (static storage). */
esp_err_t console_register(const console_cmd_t *cmd);

/** Writes "@@JSON <json>" as a payload line; false when the host did not take it. */
bool console_json(const char *json);

/** Writes "@@JSON <obj>" and deletes obj; false for NULL or when the host did not take it. */
bool console_json_take(cJSON *obj);

/** Writes one raw payload line of the current command (the screenshot block). */
bool console_payload(const char *line);

/** Records the message for the "@@ERR" terminator and returns rc, for `return console_fail(...)`. */
console_rc_t console_fail(console_rc_t rc, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/** Writes "@@EVT <json>" from any task; dropped when the host is not reading. */
void console_evt(const char *json);
