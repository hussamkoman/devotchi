#include "net_worker.h"

#include "net_priv.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "net";

#define JOB_QUEUE_DEPTH     4
#define SUBMIT_TIMEOUT_MS   10
#define POST_RETRY_MS       50

static net_event_cb_t s_cb;
static net_identity_t s_identity;
static QueueHandle_t s_jobs;

/* Written by the net task while an HTTP job runs; the submitter reads it after NET_EV_JOB_DONE. */
static net_job_result_t s_result;

/* A lost result would leave the app waiting for ever, so the post is repeated until it is queued. */
static void post_until_queued(const net_event_t *ev)
{
    while (!s_cb(ev)) {
        vTaskDelay(pdMS_TO_TICKS(POST_RETRY_MS));
    }
}

static void run_http_job(const net_job_t *job)
{
    memset(&s_result, 0, sizeof(s_result));
    s_result.kind = job->kind;
    s_result.tag = job->tag;

    url_t server;
    if (!url_parse(job->base_url, &server)) {
        s_result.err = ESP_ERR_INVALID_ARG;
    } else {
        switch (job->kind) {
        case NET_JOB_PAIR_CLAIM:
            pairing_claim(&server, job->arg, &s_identity, &s_result);
            break;
        case NET_JOB_PAIR_START:
            pairing_start(&server, &s_identity, &s_result);
            break;
        case NET_JOB_PAIR_POLL:
            pairing_poll(&server, job->arg, &s_result);
            break;
        case NET_JOB_HEALTH:
            pairing_health(&server, &s_result);
            break;
        case NET_JOB_WS_STOP:
            break;
        }
    }
    const net_event_t ev = {.id = NET_EV_JOB_DONE, .u.job = &s_result};
    post_until_queued(&ev);
}

static void net_task(void *arg)
{
    (void)arg;
    /* Static: a job holds a URL and is too large for the stack frames this firmware allows. */
    static net_job_t job;
    for (;;) {
        if (xQueueReceive(s_jobs, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (job.kind == NET_JOB_WS_STOP) {
            ws_destroy();
            const net_event_t ev = {.id = NET_EV_WS_STOPPED};
            post_until_queued(&ev);
        } else {
            run_http_job(&job);
        }
        memset(&job, 0, sizeof(job));
    }
}

esp_err_t net_worker_init(net_event_cb_t cb, const net_identity_t *identity, const char *task_name,
                          uint32_t task_stack, uint32_t task_prio)
{
    s_cb = cb;
    s_identity = *identity;
    s_jobs = xQueueCreate(JOB_QUEUE_DEPTH, sizeof(net_job_t));
    ESP_RETURN_ON_FALSE(s_jobs != NULL, ESP_ERR_NO_MEM, TAG, "queue");
    const BaseType_t ok = xTaskCreate(net_task, task_name, task_stack, NULL, task_prio, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    return ESP_OK;
}

bool net_worker_submit(const net_job_t *job)
{
    return xQueueSend(s_jobs, job, pdMS_TO_TICKS(SUBMIT_TIMEOUT_MS)) == pdTRUE;
}
