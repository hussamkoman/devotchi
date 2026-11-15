#include "console.h"

#include "cmdline.h"

#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "console";

#define USB_TX_BUFFER          4096
#define USB_RX_BUFFER          2048
#define LOG_LINE_MAX           384
#define ERR_MSG_MAX            96
#define CMD_NAME_MAX           32
#define READ_CHUNK             64

#define LOG_TIMEOUT_MS         20
#define EVT_TIMEOUT_MS         50
#define PAYLOAD_TIMEOUT_MS     500
/* Longer than a payload line: after a failed dump the TX buffer is still full and must drain first. */
#define TERMINATOR_TIMEOUT_MS  2000
#define REBOOT_FLUSH_MS        200

static const char *const RC_CODES[] = {
    [CONSOLE_OK] = "ok",
    [CONSOLE_ERR_BAD_ARGS] = "bad_args",
    [CONSOLE_ERR_INVALID_STATE] = "invalid_state",
    [CONSOLE_ERR_BUSY] = "busy",
    [CONSOLE_ERR_FAILED] = "failed",
};

/* Guards the USB port, s_out, s_log and the log accumulation state. */
static SemaphoreHandle_t s_write_lock;
static char s_out[CONSOLE_OUT_MAX];
static char s_log[LOG_LINE_MAX];
static size_t s_log_len;
static bool s_log_discard;

static const char *s_reset_reason = "other";
/* Commands are appended from init code while the console task may already be reading the table. */
static const console_cmd_t *s_cmds[CONSOLE_MAX_CMDS];
static atomic_size_t s_cmd_count;

/* Console task only. */
static char s_line[CONSOLE_LINE_MAX + 1];
static char s_args[CONSOLE_LINE_MAX + 1];
static char s_err_msg[ERR_MSG_MAX];
static bool s_payload_failed;
static atomic_uint s_evt_dropped;
static bool s_reboot_requested;

/*
 * The single place that writes to the port: one whole line per call. Without
 * a host nothing waits, so the device never stalls on its console.
 * Caller holds s_write_lock.
 */
static bool write_line(const char *buf, size_t len, uint32_t timeout_ms)
{
    const TickType_t ticks = usb_serial_jtag_is_connected() ? pdMS_TO_TICKS(timeout_ms) : 0;
    return usb_serial_jtag_write_bytes(buf, len, ticks) == (int)len;
}

static bool write_prefixed(const char *prefix, const char *body, uint32_t timeout_ms)
{
    const size_t prefix_len = strlen(prefix);
    const size_t body_len = strlen(body);
    if (prefix_len + body_len + 1 > sizeof(s_out)) {
        return false;
    }
    /* Another writer may be waiting for the host; the wait for it is bounded like the write itself. */
    if (xSemaphoreTake(s_write_lock, pdMS_TO_TICKS(2 * timeout_ms)) != pdTRUE) {
        return false;
    }
    memcpy(s_out, prefix, prefix_len);
    memcpy(s_out + prefix_len, body, body_len);
    s_out[prefix_len + body_len] = '\n';
    const bool ok = write_line(s_out, prefix_len + body_len + 1, timeout_ms);
    xSemaphoreGive(s_write_lock);
    return ok;
}

/*
 * ESP_LOG sink. A log line can arrive in several calls; it is collected and
 * written whole. Lines longer than the buffer are cut.
 */
static int log_vprintf(const char *fmt, va_list args)
{
    if (xPortInIsrContext() || xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return 0;
    }
    if (xSemaphoreTake(s_write_lock, pdMS_TO_TICKS(LOG_TIMEOUT_MS)) != pdTRUE) {
        return 0;
    }

    const size_t space = sizeof(s_log) - s_log_len;
    const int n = vsnprintf(s_log + s_log_len, space, fmt, args);
    if (n > 0) {
        const bool cut = (size_t)n >= space;
        bool tail_follows = false;
        s_log_len += cut ? space - 1 : (size_t)n;
        if (cut) {
            const size_t fmt_len = strlen(fmt);
            tail_follows = fmt_len == 0 || fmt[fmt_len - 1] != '\n';
            s_log[s_log_len - 1] = '\n';
        }
        if (s_log_discard) {
            /* Tail of a line that was cut: drop everything up to its newline. */
            const char *nl = memchr(s_log, '\n', s_log_len);
            const size_t drop = nl != NULL ? (size_t)(nl - s_log) + 1 : s_log_len;
            memmove(s_log, s_log + drop, s_log_len - drop);
            s_log_len -= drop;
            s_log_discard = (nl == NULL);
        }
        if (tail_follows) {
            s_log_discard = true;
        }

        size_t whole = s_log_len;
        while (whole > 0 && s_log[whole - 1] != '\n') {
            whole--;
        }
        if (whole > 0) {
            write_line(s_log, whole, LOG_TIMEOUT_MS);
            memmove(s_log, s_log + whole, s_log_len - whole);
            s_log_len -= whole;
        }
    }
    xSemaphoreGive(s_write_lock);
    return n;
}

bool console_payload(const char *line)
{
    const bool ok = write_prefixed("", line, PAYLOAD_TIMEOUT_MS);
    if (!ok) {
        s_payload_failed = true;
    }
    return ok;
}

bool console_json(const char *json)
{
    const bool ok = write_prefixed("@@JSON ", json, PAYLOAD_TIMEOUT_MS);
    if (!ok) {
        s_payload_failed = true;
    }
    return ok;
}

bool console_json_take(cJSON *obj)
{
    static const char PREFIX[] = "@@JSON ";
    const size_t prefix_len = sizeof(PREFIX) - 1;
    bool ok = false;

    if (obj != NULL && xSemaphoreTake(s_write_lock, pdMS_TO_TICKS(2 * PAYLOAD_TIMEOUT_MS)) == pdTRUE) {
        memcpy(s_out, PREFIX, prefix_len);
        /* One byte is kept back for the newline. */
        if (cJSON_PrintPreallocated(obj, s_out + prefix_len, (int)(sizeof(s_out) - prefix_len - 1), false)) {
            const size_t len = strlen(s_out);
            s_out[len] = '\n';
            ok = write_line(s_out, len + 1, PAYLOAD_TIMEOUT_MS);
        }
        xSemaphoreGive(s_write_lock);
    }
    cJSON_Delete(obj);
    if (!ok) {
        s_payload_failed = true;
    }
    return ok;
}

console_rc_t console_fail(console_rc_t rc, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_err_msg, sizeof(s_err_msg), fmt, args);
    va_end(args);
    return rc;
}

void console_evt(const char *json)
{
    if (!write_prefixed("@@EVT ", json, EVT_TIMEOUT_MS)) {
        atomic_fetch_add(&s_evt_dropped, 1);
    }
}

uint32_t console_evt_dropped(void)
{
    return atomic_load(&s_evt_dropped);
}

esp_err_t console_register(const console_cmd_t *cmd)
{
    ESP_RETURN_ON_FALSE(cmd != NULL && cmd->name != NULL && cmd->handler != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "command");
    ESP_RETURN_ON_FALSE(strlen(cmd->name) < CMD_NAME_MAX, ESP_ERR_INVALID_ARG, TAG, "command name");
    const size_t count = atomic_load(&s_cmd_count);
    ESP_RETURN_ON_FALSE(count < CONSOLE_MAX_CMDS, ESP_ERR_NO_MEM, TAG, "command table full");
    s_cmds[count] = cmd;
    atomic_store(&s_cmd_count, count + 1);
    return ESP_OK;
}

static const console_cmd_t *find_cmd(const char *name)
{
    const size_t count = atomic_load(&s_cmd_count);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(s_cmds[i]->name, name) == 0) {
            return s_cmds[i];
        }
    }
    return NULL;
}

static void write_terminator(const char *name, console_rc_t rc, const char *msg)
{
    char line[CMD_NAME_MAX + ERR_MSG_MAX + 32];
    if (rc == CONSOLE_OK) {
        snprintf(line, sizeof(line), "@@OK %s", name);
    } else {
        snprintf(line, sizeof(line), "@@ERR %s %s %s", name, RC_CODES[rc], msg);
    }
    write_prefixed("", line, TERMINATOR_TIMEOUT_MS);
}

/* The first word of the line, reduced to characters that are safe to echo in a terminator. */
static void first_word(const char *line, char name[CMD_NAME_MAX])
{
    size_t n = 0;
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    while (*line != '\0' && *line != ' ' && *line != '\t' && n + 1 < CMD_NAME_MAX) {
        const char c = *line++;
        name[n++] = (c > 0x20 && c < 0x7F) ? c : '?';
    }
    name[n] = '\0';
}

static void dispatch(const char *line)
{
    char name[CMD_NAME_MAX];
    first_word(line, name);
    if (name[0] == '\0') {
        return;
    }

    char *argv[CONSOLE_MAX_ARGS];
    strlcpy(s_args, line, sizeof(s_args));
    const int argc = cmdline_split(s_args, argv, CONSOLE_MAX_ARGS);
    if (argc < 1) {
        write_terminator(name, CONSOLE_ERR_BAD_ARGS, "unbalanced quote or too many arguments");
        return;
    }
    const console_cmd_t *cmd = find_cmd(argv[0]);
    if (cmd == NULL) {
        /* unknown_command is a code of its own, outside console_rc_t, because no handler can return it. */
        char reply[CMD_NAME_MAX + 48];
        snprintf(reply, sizeof(reply), "@@ERR %s unknown_command try help", name);
        write_prefixed("", reply, TERMINATOR_TIMEOUT_MS);
        return;
    }

    s_payload_failed = false;
    s_err_msg[0] = '\0';
    console_rc_t rc = cmd->handler(argc, argv, cmdline_rest(line));
    if (s_payload_failed && rc == CONSOLE_OK) {
        rc = console_fail(CONSOLE_ERR_FAILED, "host_not_reading");
    }
    write_terminator(cmd->name, rc, s_err_msg[0] != '\0' ? s_err_msg : "error");

    if (s_reboot_requested) {
        usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(REBOOT_FLUSH_MS));
        esp_restart();
    }
}

static void console_task(void *arg)
{
    (void)arg;
    size_t len = 0;
    bool overflow = false;

    char ready[96];
    snprintf(ready, sizeof(ready), "@@READY fw=%s proto=2 reset=%s", esp_app_get_description()->version,
             s_reset_reason);
    write_prefixed("", ready, EVT_TIMEOUT_MS);

    for (;;) {
        uint8_t chunk[READ_CHUNK];
        const int n = usb_serial_jtag_read_bytes(chunk, sizeof(chunk), portMAX_DELAY);
        for (int i = 0; i < n; i++) {
            const char c = (char)chunk[i];
            if (c == '\r') {
                continue;
            }
            if (c != '\n') {
                if (len < CONSOLE_LINE_MAX) {
                    s_line[len++] = c;
                } else {
                    overflow = true;
                }
                continue;
            }
            s_line[len] = '\0';
            if (overflow) {
                char name[CMD_NAME_MAX];
                first_word(s_line, name);
                write_terminator(name[0] != '\0' ? name : "?", CONSOLE_ERR_BAD_ARGS, "line too long");
            } else {
                dispatch(s_line);
            }
            len = 0;
            overflow = false;
        }
    }
}

static console_rc_t cmd_help(int argc, char **argv, const char *raw_args)
{
    (void)argc;
    (void)argv;
    (void)raw_args;
    cJSON *root = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(root, "commands");
    const size_t count = atomic_load(&s_cmd_count);
    for (size_t i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", s_cmds[i]->name);
        cJSON_AddStringToObject(item, "help", s_cmds[i]->help != NULL ? s_cmds[i]->help : "");
        cJSON_AddItemToArray(list, item);
    }
    return console_json_take(root) ? CONSOLE_OK : console_fail(CONSOLE_ERR_FAILED, "output");
}

static const char *const LOG_LEVELS[] = {
    [ESP_LOG_NONE] = "none",   [ESP_LOG_ERROR] = "error", [ESP_LOG_WARN] = "warn",
    [ESP_LOG_INFO] = "info",   [ESP_LOG_DEBUG] = "debug", [ESP_LOG_VERBOSE] = "verbose",
};

static console_rc_t cmd_log(int argc, char **argv, const char *raw_args)
{
    (void)raw_args;
    if (argc == 1) {
        char json[48];
        snprintf(json, sizeof(json), "{\"level\":\"%s\"}", LOG_LEVELS[esp_log_level_get("*")]);
        console_json(json);
        return CONSOLE_OK;
    }
    if (argc > 3) {
        return console_fail(CONSOLE_ERR_BAD_ARGS, "usage: log <none|error|warn|info|debug> [tag]");
    }
    for (size_t level = ESP_LOG_NONE; level <= ESP_LOG_DEBUG; level++) {
        if (strcmp(argv[1], LOG_LEVELS[level]) == 0) {
            esp_log_level_set(argc == 3 ? argv[2] : "*", (esp_log_level_t)level);
            return CONSOLE_OK;
        }
    }
    return console_fail(CONSOLE_ERR_BAD_ARGS, "unknown level");
}

void console_reboot_after_command(void)
{
    s_reboot_requested = true;
}

static console_rc_t cmd_reboot(int argc, char **argv, const char *raw_args)
{
    (void)argc;
    (void)argv;
    (void)raw_args;
    console_reboot_after_command();
    return CONSOLE_OK;
}

static const console_cmd_t CMD_HELP = {"help", "list commands", cmd_help};
static const console_cmd_t CMD_LOG = {"log", "log [<none|error|warn|info|debug> [tag]]: show or set the log level", cmd_log};
static const console_cmd_t CMD_REBOOT = {"reboot", "restart the device", cmd_reboot};

esp_err_t console_init(const char *reset_reason)
{
    s_reset_reason = reset_reason;
    s_write_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_write_lock != NULL, ESP_ERR_NO_MEM, TAG, "mutex");

    /* The 256-byte defaults lose data on a screenshot and on long command lines. */
    usb_serial_jtag_driver_config_t usb_cfg = {
        .tx_buffer_size = USB_TX_BUFFER,
        .rx_buffer_size = USB_RX_BUFFER,
    };
    ESP_RETURN_ON_ERROR(usb_serial_jtag_driver_install(&usb_cfg), TAG, "usb driver");
    esp_log_set_vprintf(log_vprintf);

    ESP_RETURN_ON_ERROR(console_register(&CMD_HELP), TAG, "help");
    ESP_RETURN_ON_ERROR(console_register(&CMD_LOG), TAG, "log");
    ESP_RETURN_ON_ERROR(console_register(&CMD_REBOOT), TAG, "reboot");
    return ESP_OK;
}

esp_err_t console_start(const char *task_name, uint32_t stack_size, uint32_t priority)
{
    const BaseType_t ok = xTaskCreate(console_task, task_name, stack_size, NULL, priority, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}
