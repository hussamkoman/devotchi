#pragma once

#include "net.h"

#define WS_MSG_POOL_SIZE  6

/** Allocates the receive pool. The task parameters are used for every client. */
esp_err_t ws_init(net_event_cb_t cb, const char *task_name, uint32_t task_stack, uint32_t task_prio);

/**
 * Creates and starts the client for uri (ws[s]://host[:port]/ws); the token
 * travels in the Authorization header only. ESP_ERR_INVALID_STATE while a
 * client exists: ws_request_stop() and NET_EV_WS_STOPPED come first.
 */
esp_err_t ws_start(const char *uri, const char *token);

/** Sends one text frame, waiting at most a second; false when it was not sent. */
bool ws_send(const char *buf, size_t len);

/**
 * Queues the destruction of the client on the net task, which can take as
 * long as a connect attempt; NET_EV_WS_STOPPED follows. False when the job
 * could not be queued.
 */
bool ws_request_stop(void);

void ws_msg_release(net_ws_msg_t *msg);

/** Messages lost because they were binary, too long, or the pool was empty. */
uint32_t ws_rx_dropped(void);
