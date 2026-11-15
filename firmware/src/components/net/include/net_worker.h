#pragma once

#include "net.h"
#include "url.h"

#define NET_JOB_ARG_SIZE  NET_SECRET_SIZE

typedef struct {
    net_job_kind_t kind;
    uint32_t tag;
    /** Server base URL; unused by NET_JOB_WS_STOP. */
    char base_url[URL_BASE_SIZE];
    /** Pairing code for a claim, device secret for a poll. */
    char arg[NET_JOB_ARG_SIZE];
    /** Device token for NET_JOB_PET_FETCH. */
    char token[NET_TOKEN_SIZE];
} net_job_t;

typedef struct {
    const char *device_id;
    const char *model;
    const char *fw;
} net_identity_t;

/** Starts the net task. The identity strings must stay valid. */
esp_err_t net_worker_init(net_event_cb_t cb, const net_identity_t *identity, const char *task_name,
                          uint32_t task_stack, uint32_t task_prio);

/**
 * Queues a job. Only one HTTP job may be outstanding: submit the next after
 * NET_EV_JOB_DONE, because all of them report through one result slot.
 */
bool net_worker_submit(const net_job_t *job);
