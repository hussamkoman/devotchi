#include "app_priv.h"

#include "console.h"
#include "net.h"
#include "pet_builtin.h"
#include "pet_install.h"
#include "pet_pack.h"
#include "proto.h"
#include "storage.h"
#include "ui.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "pets";

#define TX_SIZE  (PROTO_PETS_MAX_BYTES + 1)
/* A download waits for Wi-Fi, the token and a free HTTP slot; past this it fails so later installs are not `busy`. */
#define APP_PETS_FETCH_START_MS  30000

/* App task only, except s_view (guarded by s_view_mux). */
static char s_selected[STORAGE_PET_ID_SIZE];
static pet_source_t s_source;
static pet_cmd_t s_pending;
static bool s_fetch_wanted;
static uint32_t s_fetch_wanted_ms;
static bool s_fetching;
/* The pack being installed, its read-back, and the shown pet's decoded copy. */
static uint8_t s_pack[PET_PACK_MAX_SIZE];
static uint8_t s_scratch[PET_PACK_MAX_SIZE];
static uint8_t s_backup[PET_PACK_MAX_SIZE];
static pet_def_t s_def;
static pet_def_t s_page_def;
static char s_tx[TX_SIZE];
static portMUX_TYPE s_view_mux = portMUX_INITIALIZER_UNLOCKED;
static app_pets_view_t s_view;

static void publish_view(void)
{
    app_pets_view_t v = {
        .source = s_source,
        .partition = storage_pets_state(),
        .slots = storage_pets_slots(),
        .fetching = s_fetch_wanted || s_fetching,
    };
    snprintf(v.selected, sizeof(v.selected), "%s", s_selected);
    snprintf(v.shown, sizeof(v.shown), "%s", s_def.id);
    v.stored = storage_pets_list(v.entries, PET_STORE_SLOTS);
    portENTER_CRITICAL(&s_view_mux);
    s_view = v;
    portEXIT_CRITICAL(&s_view_mux);
}

void app_pets_get_view(app_pets_view_t *out)
{
    portENTER_CRITICAL(&s_view_mux);
    *out = s_view;
    portEXIT_CRITICAL(&s_view_mux);
}

const char *app_pets_selected(void)
{
    return s_selected;
}

/* Decodes the pet s_pet names (or Devo) and hands it to every view that follows the selection. */
static void load_selected(void)
{
    const uint8_t *bytes;
    size_t len;
    s_source = pet_install_resolve(storage_pets_ops(), s_selected, s_scratch, sizeof(s_scratch), &bytes, &len);
    if (pet_pack_decode(bytes, len, &s_def) != PET_PACK_OK) {
        const pet_builtin_t *b = pet_builtin_find(PET_BUILTIN_DEFAULT);
        pet_pack_decode(b->bytes, b->len, &s_def);
        s_source = PET_SOURCE_FALLBACK;
    }
    if (s_source == PET_SOURCE_FALLBACK) {
        ESP_LOGW(TAG, "pet %s does not load, showing %s", s_selected, s_def.id);
    }
    ui_pet_set_def(&s_def);
    publish_view();
}

void app_pets_init(void)
{
    storage_get_pet(s_selected);
    load_selected();
}

static uint8_t builtin_rev(const pet_builtin_t *b)
{
    return b->len > 5 ? b->bytes[5] : 0;
}

static void send_pets(uint32_t now, const char *op, const char *pet, pet_result_t result)
{
    storage_pet_entry_t stored[PET_STORE_SLOTS];
    const size_t n = storage_pets_list(stored, PET_STORE_SLOTS);
    proto_pets_t p = {
        .selected = s_selected,
        .slots = (int)storage_pets_slots(),
        .free = (int)(storage_pets_slots() > n ? storage_pets_slots() - n : 0),
        .result_op = op,
        .result_pet = pet,
        .result_ok = result == PET_RESULT_OK,
        .result_error = pet_result_name(result),
    };
    for (unsigned i = 0; i < PET_BUILTIN_COUNT; i++) {
        const pet_builtin_t *b = pet_builtin_at(i);
        p.installed[p.n_installed++] = (proto_pet_entry_t){b->id, builtin_rev(b), true};
    }
    for (size_t i = 0; i < n && p.n_installed < PROTO_PETS_MAX; i++) {
        p.installed[p.n_installed++] = (proto_pet_entry_t){stored[i].id, stored[i].rev, false};
    }
    app_link_send(now, "pets", s_tx, proto_write_pets(s_tx, sizeof(s_tx), &p), NULL);
    if (op != NULL) {
        char evt[128];
        snprintf(evt, sizeof(evt), "{\"evt\":\"pets\",\"op\":\"%s\",\"pet\":\"%s\",\"ok\":%s,\"error\":\"%s\"}", op,
                 pet != NULL ? pet : "", result == PET_RESULT_OK ? "true" : "false", pet_result_name(result));
        console_evt(evt);
    }
    publish_view();
}

void app_pets_on_registered(uint32_t now)
{
    send_pets(now, NULL, NULL, PET_RESULT_OK);
}

static void selected_changed(const char *id)
{
    snprintf(s_selected, sizeof(s_selected), "%s", id);
    load_selected();
}

void app_pets_on_cmd(const proto_pet_cmd_t *raw, uint32_t now)
{
    pet_cmd_t cmd;
    const pet_result_t check = pet_cmd_check(raw, &cmd);
    const char *op = proto_pet_op_name(raw->op);
    /* A malformed id is never echoed back: the result names only an id that passed the check. */
    const char *pet = check == PET_RESULT_UNKNOWN ? "" : cmd.id;
    if (check != PET_RESULT_OK) {
        send_pets(now, op, pet, check);
        return;
    }
    pet_result_t r = PET_RESULT_OK;
    switch (cmd.op) {
    case PROTO_PET_OP_INSTALL:
        if (s_fetch_wanted || s_fetching) {
            send_pets(now, op, pet, PET_RESULT_BUSY);
            return;
        }
        s_pending = cmd;
        s_fetch_wanted = true;
        s_fetch_wanted_ms = now;
        publish_view();
        return;
    case PROTO_PET_OP_SELECT:
        r = pet_install_select(storage_pets_ops(), cmd.id, s_scratch, sizeof(s_scratch));
        if (r == PET_RESULT_OK) {
            selected_changed(cmd.id);
        }
        break;
    case PROTO_PET_OP_REMOVE: {
        bool reselected;
        r = pet_install_remove(storage_pets_ops(), cmd.id, s_selected, &reselected);
        if (reselected) {
            selected_changed(PET_BUILTIN_DEFAULT);
        }
        break;
    }
    case PROTO_PET_OP_UNKNOWN:
        r = PET_RESULT_UNKNOWN;
        break;
    }
    send_pets(now, op, pet, r);
}

bool app_pets_take_fetch(char id[NET_JOB_ARG_SIZE])
{
    if (!s_fetch_wanted) {
        return false;
    }
    snprintf(id, NET_JOB_ARG_SIZE, "%s", s_pending.id);
    s_fetch_wanted = false;
    s_fetching = true;
    return true;
}

void app_pets_on_fetch(const net_job_result_t *r, uint32_t now)
{
    if (!s_fetching) {
        return;
    }
    s_fetching = false;
    const pet_download_t dl = {
        .status = r->err == ESP_OK ? r->http_status : 0,
        .content_length = r->content_length,
        .body = net_pet_body(),
        .received = r->pet_len,
        .complete = r->pet_complete,
    };
    pet_result_t res = pet_download_check(&s_pending, &dl);
    bool selected = false;
    if (res == PET_RESULT_OK) {
        memcpy(s_pack, net_pet_body(), dl.received);
        res = pet_install_commit(storage_pets_ops(), &s_pending, s_pack, dl.received, s_scratch, sizeof(s_scratch),
                                 s_backup, sizeof(s_backup), &selected);
    }
    ESP_LOGI(TAG, "install %s rev %u: %s", s_pending.id, s_pending.rev, pet_result_name(res));
    if (selected) {
        selected_changed(s_pending.id);
    } else if (res == PET_RESULT_OK && strcmp(s_pending.id, s_def.id) == 0) {
        /* A new revision of the pet on screen. */
        load_selected();
    }
    send_pets(now, "install", s_pending.id, res);
}

void app_pets_fetch_failed(uint32_t now)
{
    if (s_fetch_wanted || s_fetching) {
        s_fetch_wanted = false;
        s_fetching = false;
        send_pets(now, "install", s_pending.id, PET_RESULT_HTTP);
    }
}

void app_pets_tick(uint32_t now)
{
    if (s_fetch_wanted && now - s_fetch_wanted_ms >= APP_PETS_FETCH_START_MS) {
        s_fetch_wanted = false;
        ESP_LOGW(TAG, "install %s rev %u: download did not start", s_pending.id, s_pending.rev);
        send_pets(now, "install", s_pending.id, PET_RESULT_HTTP);
    }
}

pet_result_t app_pets_select(const char *id, uint32_t now)
{
    if (!pet_pack_id_ok(id)) {
        return PET_RESULT_UNKNOWN;
    }
    const pet_result_t r = pet_install_select(storage_pets_ops(), id, s_scratch, sizeof(s_scratch));
    if (r == PET_RESULT_OK) {
        selected_changed(id);
        send_pets(now, NULL, NULL, PET_RESULT_OK);
    }
    return r;
}

pet_result_t app_pets_remove(const char *id, uint32_t now)
{
    if (!pet_pack_id_ok(id)) {
        return PET_RESULT_UNKNOWN;
    }
    bool reselected;
    const pet_result_t r = pet_install_remove(storage_pets_ops(), id, s_selected, &reselected);
    if (reselected) {
        selected_changed(PET_BUILTIN_DEFAULT);
    }
    if (r == PET_RESULT_OK) {
        send_pets(now, NULL, NULL, PET_RESULT_OK);
    }
    return r;
}

size_t app_pets_count(void)
{
    storage_pet_entry_t stored[PET_STORE_SLOTS];
    return PET_BUILTIN_COUNT + storage_pets_list(stored, PET_STORE_SLOTS);
}

const pet_def_t *app_pets_load(size_t index, bool *builtin)
{
    const uint8_t *bytes = NULL;
    size_t len = 0;
    *builtin = index < PET_BUILTIN_COUNT;
    if (*builtin) {
        const pet_builtin_t *b = pet_builtin_at((unsigned)index);
        bytes = b->bytes;
        len = b->len;
    } else {
        storage_pet_entry_t stored[PET_STORE_SLOTS];
        const size_t n = storage_pets_list(stored, PET_STORE_SLOTS);
        const size_t i = index - PET_BUILTIN_COUNT;
        if (i < n) {
            len = storage_pets_get(stored[i].id, s_scratch, sizeof(s_scratch));
            bytes = s_scratch;
        }
    }
    if (bytes == NULL || pet_pack_decode(bytes, len, &s_page_def) != PET_PACK_OK) {
        return NULL;
    }
    return &s_page_def;
}
