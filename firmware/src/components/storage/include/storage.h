#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STORAGE_TOKEN_SIZE     64
#define STORAGE_ROOM_SIZE      32
#define STORAGE_URL_SIZE       128
#define STORAGE_SSID_SIZE      33
#define STORAGE_PSK_SIZE       65
#define STORAGE_DEV_NAME_SIZE  32

typedef enum {
    STORAGE_URL_SOURCE_DEFAULT,
    STORAGE_URL_SOURCE_NVS,
} storage_url_source_t;

typedef enum {
    STORAGE_SETTING_BRIGHTNESS,
    STORAGE_SETTING_DIM_S,
    STORAGE_SETTING_DEEP_MIN,
    STORAGE_SETTING_HAPTIC,
    STORAGE_SETTING_LED,
    STORAGE_SETTING_BATT,
    STORAGE_SETTING_COUNT,
} storage_setting_t;

/**
 * Opens NVS. A failure is not fatal: the device then runs with defaults,
 * reads return "absent" and writes return ESP_ERR_INVALID_STATE.
 */
esp_err_t storage_init(void);

/** True when NVS could not be initialised and defaults are in effect. */
bool storage_is_degraded(void);

/** Reads the station credentials; false when no SSID is stored. */
bool storage_get_wifi(char ssid[STORAGE_SSID_SIZE], char psk[STORAGE_PSK_SIZE]);
bool storage_has_wifi(void);
esp_err_t storage_set_wifi(const char *ssid, const char *psk);

/** Reads the server base URL, falling back to CONFIG_DEVOTCHI_DEFAULT_SERVER_URL. */
void storage_get_server_url(char url[STORAGE_URL_SIZE], storage_url_source_t *source);

/** Persists the server base URL; NULL or "" erases the key so the default applies. */
esp_err_t storage_set_server_url(const char *url);

/** Reads token and room id; false when no token is stored. Either pointer may be NULL. */
bool storage_get_pairing(char token[STORAGE_TOKEN_SIZE], char room[STORAGE_ROOM_SIZE]);
bool storage_is_paired(void);
esp_err_t storage_set_pairing(const char *token, const char *room);

/** Erases token and room id only. */
esp_err_t storage_clear_pairing(void);

/**
 * Copies token, room id and the raw server URL to the backup keys.
 * ESP_ERR_NOT_FOUND without a token; ESP_ERR_INVALID_STATE when a backup
 * exists and force is false.
 */
esp_err_t storage_backup_pairing(bool force);

/** Writes the backup back; ESP_ERR_NOT_FOUND without a backup. The backup is kept. */
esp_err_t storage_restore_pairing(void);
bool storage_has_backup(void);

/** Name used by the console (`set`) and in `state`. */
const char *storage_setting_name(storage_setting_t id);
uint16_t storage_get_setting(storage_setting_t id);
esp_err_t storage_set_setting(storage_setting_t id, uint16_t value);

/**
 * Factory reset: erases every key of the devotchi namespace (nvs_erase_all on
 * that namespace, never the partition), backups included.
 */
esp_err_t storage_erase_all(void);

bool storage_get_prov_request(void);
esp_err_t storage_set_prov_request(bool requested);

/** Reads the device name given by the server; false (and "") when none is stored. */
bool storage_get_dev_name(char name[STORAGE_DEV_NAME_SIZE]);
esp_err_t storage_set_dev_name(const char *name);
