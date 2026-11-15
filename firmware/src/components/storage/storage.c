#include "storage.h"

#include "pet_builtin.h"
#include "pet_pack.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "storage";

#define NVS_NAMESPACE   "devotchi"
#define LOCK_TIMEOUT_MS 1000

#define KEY_WIFI_SSID   "wifi_ssid"
#define KEY_WIFI_PSK    "wifi_psk"
#define KEY_TOKEN       "token"
#define KEY_ROOM        "room_id"
#define KEY_SERVER_URL  "server_url"
#define KEY_BK_TOKEN    "bk_token"
#define KEY_BK_ROOM     "bk_room"
#define KEY_BK_URL      "bk_url"
#define KEY_DEV_NAME    "dev_name"
#define KEY_PET         "s_pet"
#define KEY_PROV_REQ    "prov_req"

typedef struct {
    const char *key;
    const char *name;
    bool is_u16;
    uint16_t def;
} setting_def_t;

static const setting_def_t SETTINGS[STORAGE_SETTING_COUNT] = {
    [STORAGE_SETTING_BRIGHTNESS] = {"s_bright", "brightness", false, 75},
    [STORAGE_SETTING_DIM_S] = {"s_dim_s", "dimS", true, 30},
    [STORAGE_SETTING_DEEP_MIN] = {"s_deep_min", "deepMin", true, 20},
    [STORAGE_SETTING_HAPTIC] = {"s_haptic", "haptic", false, 2},
    [STORAGE_SETTING_LED] = {"s_led", "led", false, 1},
    [STORAGE_SETTING_BATT] = {"s_batt", "batt", false, 0},
    [STORAGE_SETTING_CLOCK] = {"s_clock", "clock", false, 0},
    [STORAGE_SETTING_CLOCK_BRI] = {"s_clk_bri", "clockBri", false, 30},
    [STORAGE_SETTING_H24] = {"s_24h", "h24", false, 1},
    [STORAGE_SETTING_NIGHT] = {"s_night", "night", false, 1},
    [STORAGE_SETTING_CLICKS] = {"s_clicks", "clicks", false, 1},
};

static SemaphoreHandle_t s_lock;
static bool s_degraded = true;

/* Pairing values handled by the multi-key operations; guarded by s_lock. */
static char s_token[STORAGE_TOKEN_SIZE];
static char s_room[STORAGE_ROOM_SIZE];
static char s_url[STORAGE_URL_SIZE];

static bool lock(void)
{
    return xSemaphoreTake(s_lock, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) == pdTRUE;
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

/* An absent key, a value that does not fit and a read error all read as "". */
static bool read_str(nvs_handle_t h, const char *key, char *buf, size_t cap)
{
    size_t len = cap;
    buf[0] = '\0';
    if (nvs_get_str(h, key, buf, &len) != ESP_OK) {
        buf[0] = '\0';
        return false;
    }
    return buf[0] != '\0';
}

static esp_err_t erase_key(nvs_handle_t h, const char *key)
{
    const esp_err_t err = nvs_erase_key(h, key);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

static bool get_str(const char *key, char *buf, size_t cap)
{
    buf[0] = '\0';
    if (s_degraded) {
        return false;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    const bool found = read_str(h, key, buf, cap);
    nvs_close(h);
    return found;
}

static bool has_str(const char *key)
{
    if (s_degraded) {
        return false;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t len = 0;
    const bool found = nvs_get_str(h, key, NULL, &len) == ESP_OK && len > 1;
    nvs_close(h);
    return found;
}

/* Opens the namespace for writing with s_lock held; pair with end_write(). */
static esp_err_t begin_write(nvs_handle_t *h)
{
    if (s_degraded) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!lock()) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, h);
    if (err != ESP_OK) {
        unlock();
    }
    return err;
}

static esp_err_t end_write(nvs_handle_t h, esp_err_t err, const char *what)
{
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    unlock();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s failed: %s", what, esp_err_to_name(err));
    }
    return err;
}

/*
 * Copies token, room id and the raw server_url value to the bk_* keys.
 * bk_token is written last: it is the marker that a backup exists.
 * Caller holds s_lock.
 */
static esp_err_t write_backup(nvs_handle_t h)
{
    if (!read_str(h, KEY_TOKEN, s_token, sizeof(s_token))) {
        return ESP_ERR_NOT_FOUND;
    }
    read_str(h, KEY_ROOM, s_room, sizeof(s_room));
    read_str(h, KEY_SERVER_URL, s_url, sizeof(s_url));
    ESP_RETURN_ON_ERROR(nvs_set_str(h, KEY_BK_ROOM, s_room), TAG, "bk_room");
    ESP_RETURN_ON_ERROR(nvs_set_str(h, KEY_BK_URL, s_url), TAG, "bk_url");
    ESP_RETURN_ON_ERROR(nvs_set_str(h, KEY_BK_TOKEN, s_token), TAG, "bk_token");
    return ESP_OK;
}

/* Keeps the oldest known pairing before it is overwritten or erased. Caller holds s_lock. */
static esp_err_t backup_if_first(nvs_handle_t h)
{
    size_t len = 0;
    const bool has_backup = nvs_get_str(h, KEY_BK_TOKEN, NULL, &len) == ESP_OK && len > 1;
    if (has_backup) {
        return ESP_OK;
    }
    const esp_err_t err = write_backup(h);
    if (err == ESP_ERR_NOT_FOUND) {
        return ESP_OK;
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "pairing backed up");
    }
    return err;
}

esp_err_t storage_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "mutex");

#if CONFIG_DEVOTCHI_DEBUG_NVS_FAIL
    const esp_err_t err = ESP_FAIL;
#else
    const esp_err_t err = nvs_flash_init();
#endif
    if (err != ESP_OK) {
        /* The partition holds the WiFi credentials and the pairing: it is never erased here. */
        ESP_LOGE(TAG, "NVS unavailable (%s), running with defaults", esp_err_to_name(err));
        s_degraded = true;
        return err;
    }
    s_degraded = false;
    return ESP_OK;
}

bool storage_is_degraded(void)
{
    return s_degraded;
}

bool storage_get_wifi(char ssid[STORAGE_SSID_SIZE], char psk[STORAGE_PSK_SIZE])
{
    psk[0] = '\0';
    if (!get_str(KEY_WIFI_SSID, ssid, STORAGE_SSID_SIZE)) {
        return false;
    }
    get_str(KEY_WIFI_PSK, psk, STORAGE_PSK_SIZE);
    return true;
}

bool storage_has_wifi(void)
{
    return has_str(KEY_WIFI_SSID);
}

esp_err_t storage_set_wifi(const char *ssid, const char *psk)
{
    ESP_RETURN_ON_FALSE(ssid != NULL && ssid[0] != '\0' && strlen(ssid) < STORAGE_SSID_SIZE,
                        ESP_ERR_INVALID_ARG, TAG, "ssid");
    ESP_RETURN_ON_FALSE(psk != NULL && strlen(psk) < STORAGE_PSK_SIZE,
                        ESP_ERR_INVALID_ARG, TAG, "psk");
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(begin_write(&h), TAG, "open");
    /* The SSID decides whether credentials exist, so it goes last. */
    esp_err_t err = nvs_set_str(h, KEY_WIFI_PSK, psk);
    if (err == ESP_OK) {
        err = nvs_set_str(h, KEY_WIFI_SSID, ssid);
    }
    return end_write(h, err, "set wifi");
}

void storage_get_server_url(char url[STORAGE_URL_SIZE], storage_url_source_t *source)
{
    const bool stored = get_str(KEY_SERVER_URL, url, STORAGE_URL_SIZE);
    if (!stored) {
        strlcpy(url, CONFIG_DEVOTCHI_DEFAULT_SERVER_URL, STORAGE_URL_SIZE);
    }
    if (source != NULL) {
        *source = stored ? STORAGE_URL_SOURCE_NVS : STORAGE_URL_SOURCE_DEFAULT;
    }
}

esp_err_t storage_set_server_url(const char *url)
{
    const bool erase = (url == NULL || url[0] == '\0');
    ESP_RETURN_ON_FALSE(erase || strlen(url) < STORAGE_URL_SIZE, ESP_ERR_INVALID_ARG, TAG, "url");
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(begin_write(&h), TAG, "open");
    esp_err_t err = backup_if_first(h);
    if (err == ESP_OK) {
        err = erase ? erase_key(h, KEY_SERVER_URL) : nvs_set_str(h, KEY_SERVER_URL, url);
    }
    return end_write(h, err, "set server url");
}

bool storage_get_pairing(char token[STORAGE_TOKEN_SIZE], char room[STORAGE_ROOM_SIZE])
{
    if (room != NULL) {
        get_str(KEY_ROOM, room, STORAGE_ROOM_SIZE);
    }
    if (token == NULL) {
        return has_str(KEY_TOKEN);
    }
    return get_str(KEY_TOKEN, token, STORAGE_TOKEN_SIZE);
}

bool storage_is_paired(void)
{
    return has_str(KEY_TOKEN);
}

esp_err_t storage_set_pairing(const char *token, const char *room)
{
    ESP_RETURN_ON_FALSE(token != NULL && token[0] != '\0' && strlen(token) < STORAGE_TOKEN_SIZE,
                        ESP_ERR_INVALID_ARG, TAG, "token");
    ESP_RETURN_ON_FALSE(room != NULL && strlen(room) < STORAGE_ROOM_SIZE,
                        ESP_ERR_INVALID_ARG, TAG, "room");
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(begin_write(&h), TAG, "open");
    esp_err_t err = backup_if_first(h);
    /* NVS has no multi-key transaction; the token is what authenticates, so it goes last. */
    if (err == ESP_OK) {
        err = nvs_set_str(h, KEY_ROOM, room);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(h, KEY_TOKEN, token);
    }
    return end_write(h, err, "set pairing");
}

esp_err_t storage_clear_pairing(void)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(begin_write(&h), TAG, "open");
    esp_err_t err = backup_if_first(h);
    if (err == ESP_OK) {
        err = erase_key(h, KEY_TOKEN);
    }
    if (err == ESP_OK) {
        err = erase_key(h, KEY_ROOM);
    }
    return end_write(h, err, "clear pairing");
}

esp_err_t storage_backup_pairing(bool force)
{
    if (!force && storage_has_backup()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!storage_is_paired()) {
        return ESP_ERR_NOT_FOUND;
    }
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(begin_write(&h), TAG, "open");
    return end_write(h, write_backup(h), "backup pairing");
}

esp_err_t storage_restore_pairing(void)
{
    if (!storage_has_backup()) {
        return ESP_ERR_NOT_FOUND;
    }
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(begin_write(&h), TAG, "open");
    esp_err_t err = ESP_ERR_NOT_FOUND;
    if (read_str(h, KEY_BK_TOKEN, s_token, sizeof(s_token))) {
        read_str(h, KEY_BK_ROOM, s_room, sizeof(s_room));
        /* An empty bk_url records that the key was absent and the Kconfig default applied. */
        const bool had_url = read_str(h, KEY_BK_URL, s_url, sizeof(s_url));
        err = had_url ? nvs_set_str(h, KEY_SERVER_URL, s_url) : erase_key(h, KEY_SERVER_URL);
        if (err == ESP_OK) {
            err = nvs_set_str(h, KEY_ROOM, s_room);
        }
        if (err == ESP_OK) {
            err = nvs_set_str(h, KEY_TOKEN, s_token);
        }
    }
    return end_write(h, err, "restore pairing");
}

bool storage_has_backup(void)
{
    return has_str(KEY_BK_TOKEN);
}

const char *storage_setting_name(storage_setting_t id)
{
    return id < STORAGE_SETTING_COUNT ? SETTINGS[id].name : "";
}

uint16_t storage_get_setting(storage_setting_t id)
{
    if (id >= STORAGE_SETTING_COUNT) {
        return 0;
    }
    const setting_def_t *def = &SETTINGS[id];
    uint16_t value = def->def;
    nvs_handle_t h;
    if (s_degraded || nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return value;
    }
    if (def->is_u16) {
        if (nvs_get_u16(h, def->key, &value) != ESP_OK) {
            value = def->def;
        }
    } else {
        uint8_t v8;
        if (nvs_get_u8(h, def->key, &v8) == ESP_OK) {
            value = v8;
        }
    }
    nvs_close(h);
    return value;
}

esp_err_t storage_set_setting(storage_setting_t id, uint16_t value)
{
    ESP_RETURN_ON_FALSE(id < STORAGE_SETTING_COUNT, ESP_ERR_INVALID_ARG, TAG, "setting id");
    const setting_def_t *def = &SETTINGS[id];
    ESP_RETURN_ON_FALSE(def->is_u16 || value <= UINT8_MAX, ESP_ERR_INVALID_ARG, TAG, "setting value");
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(begin_write(&h), TAG, "open");
    const esp_err_t err = def->is_u16 ? nvs_set_u16(h, def->key, value)
                                      : nvs_set_u8(h, def->key, (uint8_t)value);
    return end_write(h, err, "set setting");
}

esp_err_t storage_erase_all(void)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(begin_write(&h), TAG, "open");
    const esp_err_t err = nvs_erase_all(h);
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "namespace erased");
    }
    return end_write(h, err, "erase all");
}

bool storage_get_prov_request(void)
{
    uint8_t value = 0;
    nvs_handle_t h;
    if (s_degraded || nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    if (nvs_get_u8(h, KEY_PROV_REQ, &value) != ESP_OK) {
        value = 0;
    }
    nvs_close(h);
    return value != 0;
}

esp_err_t storage_set_prov_request(bool requested)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(begin_write(&h), TAG, "open");
    return end_write(h, nvs_set_u8(h, KEY_PROV_REQ, requested ? 1 : 0), "set prov request");
}

bool storage_get_dev_name(char name[STORAGE_DEV_NAME_SIZE])
{
    return get_str(KEY_DEV_NAME, name, STORAGE_DEV_NAME_SIZE);
}

bool storage_get_pet(char id[STORAGE_PET_ID_SIZE])
{
    if (!get_str(KEY_PET, id, STORAGE_PET_ID_SIZE) || id[0] == '\0') {
        snprintf(id, STORAGE_PET_ID_SIZE, "%s", PET_BUILTIN_DEFAULT);
        return false;
    }
    return true;
}

esp_err_t storage_set_pet(const char *id)
{
    ESP_RETURN_ON_FALSE(id != NULL && pet_pack_id_ok(id), ESP_ERR_INVALID_ARG, TAG, "pet id");
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(begin_write(&h), TAG, "open");
    return end_write(h, nvs_set_str(h, KEY_PET, id), "set pet");
}

esp_err_t storage_set_dev_name(const char *name)
{
    ESP_RETURN_ON_FALSE(name != NULL && strlen(name) < STORAGE_DEV_NAME_SIZE,
                        ESP_ERR_INVALID_ARG, TAG, "name");
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(begin_write(&h), TAG, "open");
    return end_write(h, nvs_set_str(h, KEY_DEV_NAME, name), "set dev name");
}
