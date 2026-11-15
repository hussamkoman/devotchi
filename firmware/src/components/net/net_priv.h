#pragma once

#include "net.h"
#include "net_worker.h"
#include "url.h"

#include "esp_http_client.h"
#include "esp_websocket_client.h"

/* tls_cfg.c: the one place that decides which CA both clients verify against. */
void tls_cfg_apply_ws(esp_websocket_client_config_t *cfg);
void tls_cfg_apply_http(esp_http_client_config_t *cfg);

/** Destroys the WebSocket client if one exists. Net task only: it waits for the client task. */
void ws_destroy(void);

/* pairing.c: blocking HTTP calls for the net task. Each fills http_status and the fields it received. */
void pairing_claim(const url_t *server, const char *code, const net_identity_t *id, net_job_result_t *out);
void pairing_start(const url_t *server, const net_identity_t *id, net_job_result_t *out);
void pairing_poll(const url_t *server, const char *secret, net_job_result_t *out);
void pairing_health(const url_t *server, net_job_result_t *out);

/* pet_fetch.c: the pack download for the net task (protocol 14.8). */
void pet_fetch(const url_t *server, const char *id, const char *token, net_job_result_t *out);
