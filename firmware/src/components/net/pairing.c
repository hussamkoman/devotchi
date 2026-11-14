#include "net_priv.h"

#include "cJSON.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "pairing";

#define HTTP_TIMEOUT_MS   10000
#define REQUEST_SIZE      192
#define RESPONSE_SIZE     768

/* Net task only. */
static char s_url[URL_FULL_SIZE];
static char s_request[REQUEST_SIZE];
static char s_response[RESPONSE_SIZE];

static void copy_string(const cJSON *root, const char *key, char *dst, size_t cap)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item) && item->valuestring != NULL && strlen(item->valuestring) < cap) {
        memcpy(dst, item->valuestring, strlen(item->valuestring) + 1);
    }
}

static uint32_t read_uint(const cJSON *root, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0 || item->valuedouble > (double)UINT32_MAX) {
        return 0;
    }
    return (uint32_t)item->valuedouble;
}

static void parse_response(size_t len, net_job_result_t *out)
{
    cJSON *root = cJSON_ParseWithLength(s_response, len);
    if (cJSON_IsObject(root)) {
        copy_string(root, "token", out->token, sizeof(out->token));
        copy_string(root, "roomId", out->room, sizeof(out->room));
        copy_string(root, "name", out->name, sizeof(out->name));
        copy_string(root, "code", out->code, sizeof(out->code));
        copy_string(root, "deviceSecret", out->secret, sizeof(out->secret));
        out->expires_in_s = read_uint(root, "expiresInS");
        out->poll_interval_ms = read_uint(root, "pollIntervalMs");
        out->proto = (int)read_uint(root, "proto");
    }
    cJSON_Delete(root);
}

/* body NULL sends a GET. The response body is cut at RESPONSE_SIZE - 1 bytes. */
static void request(const url_t *server, const char *path, const char *body, net_job_result_t *out)
{
    if (url_http(server, path, s_url, sizeof(s_url)) < 0) {
        out->err = ESP_ERR_INVALID_ARG;
        return;
    }
    esp_http_client_config_t cfg = {
        .url = s_url,
        .method = body != NULL ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .disable_auto_redirect = true,
    };
    tls_cfg_apply_http(&cfg);
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        out->err = ESP_ERR_NO_MEM;
        return;
    }

    const int body_len = body != NULL ? (int)strlen(body) : 0;
    if (body != NULL) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }
    esp_err_t err = esp_http_client_open(client, body_len);
    if (err == ESP_OK && body_len > 0 && esp_http_client_write(client, body, body_len) != body_len) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK && esp_http_client_fetch_headers(client) < 0) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK) {
        out->http_status = esp_http_client_get_status_code(client);
        const int n = esp_http_client_read_response(client, s_response, sizeof(s_response) - 1);
        if (n > 0) {
            parse_response((size_t)n, out);
        }
    } else {
        ESP_LOGW(TAG, "%s: %s", path, esp_err_to_name(err));
    }
    out->err = err;
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    memset(s_response, 0, sizeof(s_response));
}

/* Prints obj into s_request and deletes it; false when it did not fit. */
static bool take_body(cJSON *obj)
{
    const bool ok = obj != NULL && cJSON_PrintPreallocated(obj, s_request, sizeof(s_request), false);
    cJSON_Delete(obj);
    return ok;
}

static cJSON *identity_body(const net_identity_t *id)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "deviceId", id->device_id);
    cJSON_AddStringToObject(obj, "model", id->model);
    cJSON_AddStringToObject(obj, "fw", id->fw);
    return obj;
}

void pairing_claim(const url_t *server, const char *code, const net_identity_t *id, net_job_result_t *out)
{
    cJSON *obj = identity_body(id);
    cJSON_AddStringToObject(obj, "code", code);
    if (!take_body(obj)) {
        out->err = ESP_ERR_INVALID_SIZE;
        return;
    }
    request(server, "/api/pair/claim", s_request, out);
}

void pairing_start(const url_t *server, const net_identity_t *id, net_job_result_t *out)
{
    if (!take_body(identity_body(id))) {
        out->err = ESP_ERR_INVALID_SIZE;
        return;
    }
    request(server, "/api/device/pair/start", s_request, out);
}

void pairing_poll(const url_t *server, const char *secret, net_job_result_t *out)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "deviceSecret", secret);
    if (!take_body(obj)) {
        out->err = ESP_ERR_INVALID_SIZE;
        return;
    }
    request(server, "/api/device/pair/poll", s_request, out);
    memset(s_request, 0, sizeof(s_request));
}

void pairing_health(const url_t *server, net_job_result_t *out)
{
    request(server, "/health", NULL, out);
}
