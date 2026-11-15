#pragma once

#include "pet_install.h"

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
#define STORAGE_PET_ID_SIZE    (PET_PACK_ID_LEN + 1)

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
    /** Architecture 7.3: Show clock, Clock brightness, 12/24 h, Night dimming. */
    STORAGE_SETTING_CLOCK,
    STORAGE_SETTING_CLOCK_BRI,
    STORAGE_SETTING_H24,
    STORAGE_SETTING_NIGHT,
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

/** Selected pet (`s_pet`); false and "devo" when the key is absent. */
bool storage_get_pet(char id[STORAGE_PET_ID_SIZE]);

/** Writes `s_pet`; only a person or a pet_cmd selects a pet. */
esp_err_t storage_set_pet(const char *id);

/**
 * Opens the `pets` partition (architecture 7.3): on an init failure it is
 * erased by label once and opened again; without the partition (an older
 * table) or after a second failure there are 0 slots. Never touches `nvs`.
 */
pet_part_state_t storage_pets_init(void);

pet_part_state_t storage_pets_state(void);

/** PET_STORE_SLOTS, or 0 when the partition is unusable. */
size_t storage_pets_slots(void);

typedef struct {
    char id[STORAGE_PET_ID_SIZE];
    uint8_t rev;
} storage_pet_entry_t;

/** Stored packs (id and header rev), at most max; returns the number written. */
size_t storage_pets_list(storage_pet_entry_t *out, size_t max);

/** Copies a stored pack into buf; returns its length, 0 when missing or larger than cap. */
size_t storage_pets_get(const char *id, uint8_t *buf, size_t cap);

/** Writes (or atomically replaces) one pack: nvs_set_blob + nvs_commit. */
esp_err_t storage_pets_put(const char *id, const uint8_t *bytes, size_t len);

esp_err_t storage_pets_remove(const char *id);

bool storage_pets_has(const char *id);

size_t storage_pets_count(void);

/** The install steps' view of this storage (pet_install.h). */
const pet_store_ops_t *storage_pets_ops(void);
