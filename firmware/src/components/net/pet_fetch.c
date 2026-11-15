#include "net_priv.h"

#include "pet_pack.h"

#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "pet_fetch";

#define HTTP_TIMEOUT_MS  10000
#define PATH_SIZE        48
#define AUTH_SIZE        (sizeof("Bearer ") + NET_TOKEN_SIZE)

/* Net task only; read by the app task after NET_EV_JOB_DONE, before it submits the next fetch. */
static uint8_t s_body[PET_PACK_MAX_SIZE];
static char s_url[URL_FULL_SIZE];
static char s_auth[AUTH_SIZE];

const uint8_t *net_pet_body(void)
{
    return s_body;
}

void pet_fetch(const url_t *server, const char *id, const char *token, net_job_result_t *out)
{
    out->content_length = -1;
    /* The app checked the id already; the URL is still built only from an id that passes here. */
    char path[PATH_SIZE];
    if (!pet_pack_id_ok(id) || snprintf(path, sizeof(path), "/api/pets/%s/pack", id) >= (int)sizeof(path) ||
        url_http(server, path, s_url, sizeof(s_url)) < 0 ||
        snprintf(s_auth, sizeof(s_auth), "Bearer %s", token) >= (int)sizeof(s_auth)) {
        out->err = ESP_ERR_INVALID_ARG;
        return;
    }
    esp_http_client_config_t cfg = {
        .url = s_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .disable_auto_redirect = true,
    };
    tls_cfg_apply_http(&cfg);
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        out->err = ESP_ERR_NO_MEM;
        return;
    }
    esp_http_client_set_header(client, "Authorization", s_auth);
    esp_err_t err = esp_http_client_open(client, 0);
    int64_t length = -1;
    if (err == ESP_OK) {
        length = esp_http_client_fetch_headers(client);
        if (length < 0) {
            err = ESP_FAIL;
        }
    }
    if (err == ESP_OK) {
        out->http_status = esp_http_client_get_status_code(client);
        out->content_length = esp_http_client_is_chunked_response(client) || length <= 0 ? -1 : (int32_t)length;
        /* Only a 200 with a length that fits is read; every other case fails a check without the body. */
        if (out->http_status == 200 && out->content_length > 0 && out->content_length <= PET_PACK_MAX_SIZE) {
            size_t got = 0;
            bool failed = false;
            while (got < (size_t)out->content_length) {
                const int n = esp_http_client_read(client, (char *)s_body + got, out->content_length - (int)got);
                if (n <= 0) {
                    failed = true;
                    break;
                }
                got += (size_t)n;
            }
            out->pet_len = got;
            out->pet_complete = !failed && got == (size_t)out->content_length;
        }
    } else {
        ESP_LOGW(TAG, "%s: %s", id, esp_err_to_name(err));
    }
    out->err = err;
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    memset(s_auth, 0, sizeof(s_auth));
}
