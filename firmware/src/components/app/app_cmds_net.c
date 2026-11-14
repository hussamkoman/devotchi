#include "app_priv.h"

#include "console.h"
#include "storage.h"
#include "wifi.h"

#include "esp_check.h"

#include <string.h>

static const char *TAG = "app_cmds_net";

#define REQUEST_TIMEOUT_MS  3000
/* A claim is one HTTP call with a 10 s timeout, queued behind at most one other. */
#define PAIR_TIMEOUT_MS     25000
/* The trial itself gives up after 20 s. */
#define WIFI_TIMEOUT_MS     25000

static console_rc_t run(app_request_kind_t kind, const char *text, const char *arg, uint32_t timeout_ms)
{
    bool ok;
    char message[APP_REQUEST_MESSAGE_SIZE];
    switch (app_request(kind, text, arg, timeout_ms, &ok, message)) {
    case APP_REQUEST_DONE:
        if (ok) {
            if (message[0] != '\0') {
                cJSON *root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "note", message);
                console_json_take(root);
            }
            return CONSOLE_OK;
        }
        return console_fail(CONSOLE_ERR_FAILED, "%s", message);
    case APP_REQUEST_BUSY:
        return console_fail(CONSOLE_ERR_BUSY, "app task busy with an earlier request");
    case APP_REQUEST_TIMEOUT:
        break;
    }
    return console_fail(CONSOLE_ERR_BUSY, "no answer from the app task");
}

static console_rc_t cmd_server(int argc, char **argv, const char *raw_args)
{
    (void)raw_args;
    if (argc == 1) {
        app_snapshot_t snap;
        storage_url_source_t source;
        char stored[STORAGE_URL_SIZE];
        app_get_snapshot(&snap);
        storage_get_server_url(stored, &source);
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "server", snap.link.server);
        cJSON_AddStringToObject(root, "serverSource", source == STORAGE_URL_SOURCE_NVS ? "nvs" : "default");
        return console_json_take(root) ? CONSOLE_OK : console_fail(CONSOLE_ERR_FAILED, "output");
    }
    if (argc != 2) {
        return console_fail(CONSOLE_ERR_BAD_ARGS, "server [ws://host[:port] | wss://host[:port]]");
    }
    return run(APP_REQUEST_SERVER, argv[1], NULL, REQUEST_TIMEOUT_MS);
}

static console_rc_t cmd_pair(int argc, char **argv, const char *raw_args)
{
    (void)raw_args;
    if (argc != 2) {
        return console_fail(CONSOLE_ERR_BAD_ARGS, "pair <CODE>");
    }
    return run(APP_REQUEST_PAIR, argv[1], NULL, PAIR_TIMEOUT_MS);
}

static console_rc_t cmd_wifi(int argc, char **argv, const char *raw_args)
{
    (void)raw_args;
    if (argc == 1) {
        app_snapshot_t snap;
        wifi_status_t status;
        app_get_snapshot(&snap);
        wifi_get_status(&status);
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "state", app_wifi_state_name(snap.link.wifi));
        cJSON_AddStringToObject(root, "ssid", snap.link.ssid);
        cJSON_AddStringToObject(root, "ip", status.ip);
        cJSON_AddNumberToObject(root, "rssi", status.rssi);
        cJSON_AddBoolToObject(root, "saved", storage_has_wifi());
        return console_json_take(root) ? CONSOLE_OK : console_fail(CONSOLE_ERR_FAILED, "output");
    }
    if (argc != 3 || argv[1][0] == '\0' || strlen(argv[1]) >= STORAGE_SSID_SIZE ||
        strlen(argv[2]) >= STORAGE_PSK_SIZE) {
        return console_fail(CONSOLE_ERR_BAD_ARGS, "wifi [<ssid> <passphrase>] (quote values with spaces)");
    }
    return run(APP_REQUEST_WIFI, argv[1], argv[2], WIFI_TIMEOUT_MS);
}

static console_rc_t cmd_wifi_setup(int argc, char **argv, const char *raw_args)
{
    (void)argc;
    (void)argv;
    (void)raw_args;
    const esp_err_t err = storage_set_prov_request(true);
    if (err != ESP_OK) {
        return console_fail(CONSOLE_ERR_FAILED, "storage: %s", esp_err_to_name(err));
    }
    console_reboot_after_command();
    return CONSOLE_OK;
}

static console_rc_t cmd_msg(int argc, char **argv, const char *raw_args)
{
    (void)argv;
    if (argc < 2 || raw_args[0] == '\0') {
        return console_fail(CONSOLE_ERR_BAD_ARGS, "msg <json>");
    }
    if (strlen(raw_args) >= APP_REQUEST_TEXT_SIZE) {
        return console_fail(CONSOLE_ERR_BAD_ARGS, "message too long");
    }
    return run(APP_REQUEST_MSG, raw_args, NULL, REQUEST_TIMEOUT_MS);
}

static console_rc_t cmd_reset_pairing(int argc, char **argv, const char *raw_args)
{
    (void)argc;
    (void)argv;
    (void)raw_args;
    return run(APP_REQUEST_RESET_PAIRING, NULL, NULL, REQUEST_TIMEOUT_MS);
}

static console_rc_t cmd_pair_backup(int argc, char **argv, const char *raw_args)
{
    (void)raw_args;
    const bool force = argc == 2 && strcmp(argv[1], "--force") == 0;
    if (argc > 2 || (argc == 2 && !force)) {
        return console_fail(CONSOLE_ERR_BAD_ARGS, "pair-backup [--force]");
    }
    switch (storage_backup_pairing(force)) {
    case ESP_OK:
        return CONSOLE_OK;
    case ESP_ERR_NOT_FOUND:
        return console_fail(CONSOLE_ERR_INVALID_STATE, "not paired");
    case ESP_ERR_INVALID_STATE:
        if (storage_has_backup()) {
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "note", "a backup exists and was kept (--force replaces it)");
            console_json_take(root);
            return CONSOLE_OK;
        }
        return console_fail(CONSOLE_ERR_FAILED, "storage unavailable");
    default:
        return console_fail(CONSOLE_ERR_FAILED, "storage error");
    }
}

static console_rc_t cmd_pair_restore(int argc, char **argv, const char *raw_args)
{
    (void)argc;
    (void)argv;
    (void)raw_args;
    return run(APP_REQUEST_PAIR_RESTORE, NULL, NULL, REQUEST_TIMEOUT_MS);
}

static console_rc_t cmd_preview(int argc, char **argv, const char *raw_args)
{
    (void)raw_args;
    if (argc != 2) {
        return console_fail(CONSOLE_ERR_BAD_ARGS, "preview pairing|provisioning|approval-low|approval-high|identify|settings|off");
    }
    return run(APP_REQUEST_PREVIEW, argv[1], NULL, REQUEST_TIMEOUT_MS);
}

static const console_cmd_t CMDS[] = {
    {"server", "server [<url>]: show, or set and persist, the server base URL, then reconnect", cmd_server},
    {"pair", "pair <CODE>: claim a pairing code created on the PC (devotchi pair --new)", cmd_pair},
    {"wifi", "wifi [<ssid> <passphrase>]: show status, or try credentials (saved only once connected)", cmd_wifi},
    {"wifi-setup", "restart into BLE provisioning; the stored credentials stay until new ones work", cmd_wifi_setup},
    {"msg", "msg <json>: handle a protocol message as if the server had sent it", cmd_msg},
    {"reset-pairing", "erase token and room id (the first pairing is kept as a backup)", cmd_reset_pairing},
    {"pair-backup", "pair-backup [--force]: copy token, room id and server URL to the backup keys", cmd_pair_backup},
    {"pair-restore", "write the backup back and reconnect", cmd_pair_restore},
    {"preview", "preview pairing|provisioning|approval-low|approval-high|identify|settings|off: show a screen with sample data", cmd_preview},
};

esp_err_t app_cmds_net_register(void)
{
    for (size_t i = 0; i < sizeof(CMDS) / sizeof(CMDS[0]); i++) {
        ESP_RETURN_ON_ERROR(console_register(&CMDS[i]), TAG, "%s", CMDS[i].name);
    }
    return ESP_OK;
}
