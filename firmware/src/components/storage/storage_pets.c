#include "storage.h"

#include "pet_install.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <assert.h>
#include <string.h>

static const char *TAG = "pets";

#define PETS_NAMESPACE  "pets"

static pet_part_state_t s_state = PET_PART_ABSENT;
static SemaphoreHandle_t s_lock;
/* Guarded by s_lock: the header of a listed pack is read through it. */
static uint8_t s_scan[PET_PACK_MAX_SIZE];

static bool usable(void)
{
    return s_state == PET_PART_OK || s_state == PET_PART_ERASED;
}

static int part_init(void *ctx, const char *label)
{
    (void)ctx;
    const esp_err_t err = nvs_flash_init_partition(label);
    if (err == ESP_ERR_NOT_FOUND) {
        return 1;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "init of %s: %s", label, esp_err_to_name(err));
        return -1;
    }
    return 0;
}

/* The one erase in the firmware: the label is checked so it can never reach `nvs`. */
static bool part_erase(void *ctx, const char *label)
{
    (void)ctx;
    assert(pet_part_erase_allowed(label));
    if (!pet_part_erase_allowed(label)) {
        return false;
    }
    ESP_LOGW(TAG, "erasing partition %s", label);
    return nvs_flash_erase_partition(label) == ESP_OK;
}

pet_part_state_t storage_pets_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        s_state = PET_PART_FAILED;
        return s_state;
    }
    const pet_part_ops_t ops = {.init = part_init, .erase = part_erase};
    s_state = pet_part_init(&ops);
    ESP_LOGI(TAG, "partition %s, %u slots", pet_part_state_name(s_state), (unsigned)storage_pets_slots());
    return s_state;
}

pet_part_state_t storage_pets_state(void)
{
    return s_state;
}

size_t storage_pets_slots(void)
{
    return usable() ? PET_STORE_SLOTS : 0;
}

static bool open_pets(nvs_open_mode_t mode, nvs_handle_t *h)
{
    if (!usable()) {
        return false;
    }
    const esp_err_t err = nvs_open_from_partition(PET_STORE_LABEL, PETS_NAMESPACE, mode, h);
    if (err != ESP_OK && !(err == ESP_ERR_NVS_NOT_FOUND && mode == NVS_READONLY)) {
        ESP_LOGW(TAG, "open: %s", esp_err_to_name(err));
    }
    return err == ESP_OK;
}

size_t storage_pets_get(const char *id, uint8_t *buf, size_t cap)
{
    nvs_handle_t h;
    if (!pet_pack_id_ok(id) || !open_pets(NVS_READONLY, &h)) {
        return 0;
    }
    size_t len = cap;
    const esp_err_t err = nvs_get_blob(h, id, buf, &len);
    nvs_close(h);
    return err == ESP_OK ? len : 0;
}

bool storage_pets_has(const char *id)
{
    nvs_handle_t h;
    if (!pet_pack_id_ok(id) || !open_pets(NVS_READONLY, &h)) {
        return false;
    }
    size_t len = 0;
    const esp_err_t err = nvs_get_blob(h, id, NULL, &len);
    nvs_close(h);
    return err == ESP_OK && len > 0;
}

esp_err_t storage_pets_put(const char *id, const uint8_t *bytes, size_t len)
{
    ESP_RETURN_ON_FALSE(pet_pack_id_ok(id) && len > 0 && len <= PET_PACK_MAX_SIZE, ESP_ERR_INVALID_ARG, TAG, "put");
    nvs_handle_t h;
    ESP_RETURN_ON_FALSE(open_pets(NVS_READWRITE, &h), ESP_ERR_INVALID_STATE, TAG, "partition");
    esp_err_t err = nvs_set_blob(h, id, bytes, len);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "put %s: %s", id, esp_err_to_name(err));
    }
    return err;
}

esp_err_t storage_pets_remove(const char *id)
{
    ESP_RETURN_ON_FALSE(pet_pack_id_ok(id), ESP_ERR_INVALID_ARG, TAG, "remove");
    nvs_handle_t h;
    ESP_RETURN_ON_FALSE(open_pets(NVS_READWRITE, &h), ESP_ERR_INVALID_STATE, TAG, "partition");
    esp_err_t err = nvs_erase_key(h, id);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

size_t storage_pets_list(storage_pet_entry_t *out, size_t max)
{
    if (!usable() || s_lock == NULL) {
        return 0;
    }
    nvs_iterator_t it = NULL;
    size_t n = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = nvs_entry_find(PET_STORE_LABEL, PETS_NAMESPACE, NVS_TYPE_BLOB, &it);
    while (err == ESP_OK && n < max) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        const size_t len = pet_pack_id_ok(info.key) ? storage_pets_get(info.key, s_scan, sizeof(s_scan)) : 0;
        if (len > 8 && memcmp(s_scan, "DPET", 4) == 0) {
            const size_t id_len = strnlen(info.key, PET_PACK_ID_LEN);
            memcpy(out[n].id, info.key, id_len);
            out[n].id[id_len] = '\0';
            out[n].rev = s_scan[5];
            n++;
        }
        err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    xSemaphoreGive(s_lock);
    return n;
}

size_t storage_pets_count(void)
{
    storage_pet_entry_t list[PET_STORE_SLOTS + 1];
    return storage_pets_list(list, PET_STORE_SLOTS + 1);
}

static bool op_put(void *ctx, const char *id, const uint8_t *bytes, size_t len)
{
    (void)ctx;
    return storage_pets_put(id, bytes, len) == ESP_OK;
}

static size_t op_get(void *ctx, const char *id, uint8_t *buf, size_t cap)
{
    (void)ctx;
    return storage_pets_get(id, buf, cap);
}

static bool op_remove(void *ctx, const char *id)
{
    (void)ctx;
    return storage_pets_remove(id) == ESP_OK;
}

static bool op_has(void *ctx, const char *id)
{
    (void)ctx;
    return storage_pets_has(id);
}

static size_t op_count(void *ctx)
{
    (void)ctx;
    return storage_pets_count();
}

static bool op_select(void *ctx, const char *id)
{
    (void)ctx;
    return storage_set_pet(id) == ESP_OK;
}

const pet_store_ops_t *storage_pets_ops(void)
{
    static pet_store_ops_t ops = {
        .put = op_put, .get = op_get, .remove = op_remove, .has = op_has, .count = op_count, .select = op_select,
    };
    ops.slots = storage_pets_slots();
    return &ops;
}
