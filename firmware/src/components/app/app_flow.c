#include "app_priv.h"

#include "app.h"
#include "console.h"
#include "fb_seq.h"
#include "feedback.h"
#include "pet_model.h"
#include "pet_pose.h"
#include "proto.h"
#include "reqq.h"
#include "storage.h"
#include "text.h"
#include "ui.h"
#include "wifi.h"

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "flow";

/* Protocol 4.7: the ack debounce and the heartbeat. */
#define ACK_DEBOUNCE_MS     100
#define ACK_MAX_DELAY_MS    500
#define HEARTBEAT_MS        4000
/* An answer without a dismiss or an error for this long was probably lost. */
#define CONFIRM_MS          3000
#define BUZZ_MIN_GAP_MS     2000
#define NAG_MS              20000
#define NAG_FINAL_MS        5000
#define NAG_FINAL_WINDOW_MS 30000
#define BAR_REFRESH_MS      250
/* One cycle of the attention pattern (fb_seq.c) for urgency 2. */
#define ATTENTION_ONCE_MS   2300
#define TX_SIZE             512
#define EVT_SIZE            256
#define TOAST_SIZE          UI_TEXT_SIZE
#define VIEW_LOCK_MS        100
/* The About page gives way to the face after this long without a button press. */
#define PAGE_IDLE_MS        20000

/* Everything below belongs to the app task, except s_view (guarded by s_view_lock). */

static reqq_t s_q;
static pet_model_t s_pet;
static char s_device_id[APP_DEVICE_ID_SIZE];
static char s_tx[TX_SIZE];
static esp_timer_handle_t s_timer;

/* app_now_ms() values; 0 means none. */
static uint32_t s_ack_due;
static uint32_t s_ack_first;
static uint32_t s_heartbeat_at;
static uint32_t s_nag_at;
static uint32_t s_identify_until;
static bool s_buzzed;
static uint32_t s_last_buzz;

static bool s_attention;
static char s_attention_text[TOAST_SIZE];
static char s_banner_shown[TOAST_SIZE];
static bool s_banner_visible;

static ui_screen_id_t s_screen = UI_SCREEN_PET;
static ui_pet_page_t s_page = UI_PET_PAGE_PET;
static uint32_t s_page_at;
static pet_pokes_t s_pokes;

static SemaphoreHandle_t s_view_lock;
static app_flow_view_t s_view;

static uint32_t after(uint32_t now, uint32_t ms)
{
    const uint32_t t = now + ms;
    return t != 0 ? t : 1;
}

static bool due(uint32_t at, uint32_t now)
{
    return at != 0 && (int32_t)(now - at) >= 0;
}

static uint32_t earliest(uint32_t a, uint32_t b, uint32_t now)
{
    if (a == 0) {
        return b;
    }
    if (b == 0) {
        return a;
    }
    return (int32_t)(a - now) < (int32_t)(b - now) ? a : b;
}

/* Toasts and request fields are printable ASCII; only quotes, backslashes and newlines need escaping. */
static void json_escape(char *dst, size_t cap, const char *src)
{
    size_t n = 0;
    for (; *src != '\0' && n + 3 < cap; src++) {
        if (*src == '"' || *src == '\\') {
            dst[n++] = '\\';
            dst[n++] = *src;
        } else if (*src == '\n') {
            dst[n++] = '\\';
            dst[n++] = 'n';
        } else {
            dst[n++] = *src;
        }
    }
    dst[n] = '\0';
}

void app_toast(const char *text)
{
    ui_toast(text, UI_TOAST_DEFAULT_MS);
    /* Static: app task only, and too large for its stack. */
    static char escaped[2 * TOAST_SIZE];
    static char evt[EVT_SIZE + 2 * TOAST_SIZE];
    json_escape(escaped, sizeof(escaped), text);
    snprintf(evt, sizeof(evt), "{\"evt\":\"toast\",\"text\":\"%s\"}", escaped);
    console_evt(evt);
}

static void request_evt(const char *op, const char *id, const char *reason)
{
    char evt[EVT_SIZE];
    snprintf(evt, sizeof(evt), "{\"evt\":\"request\",\"op\":\"%s\",\"id\":\"%s\",\"count\":%u,\"reason\":\"%s\"}", op,
             id, (unsigned)reqq_count(&s_q), reason);
    console_evt(evt);
}

/* ---- feedback ---- */

static bool own_loop_playing(void)
{
    const fb_seq_id_t cur = feedback_current();
    return cur == FB_SEQ_REQUEST || cur == FB_SEQ_PENDING;
}

/* The request loop yields to every other pattern; app_flow_on_feedback_end() resumes it. */
static void play(fb_seq_id_t id, uint32_t duration_ms)
{
    if (own_loop_playing()) {
        feedback_stop();
    }
    feedback_play_for(id, duration_ms);
}

static void stop_own_loop(void)
{
    if (own_loop_playing()) {
        feedback_stop();
    }
}

void app_flow_on_feedback_end(void)
{
    if (feedback_current() != FB_SEQ_COUNT) {
        return;
    }
    if (s_attention) {
        feedback_play(FB_SEQ_ATTENTION);
    } else if (reqq_count(&s_q) > 0) {
        feedback_play(FB_SEQ_PENDING);
    }
}

/* ---- queue bookkeeping ---- */

static bool identify_active(uint32_t now)
{
    return s_identify_until != 0 && !due(s_identify_until, now);
}

/* Earliest deadline of the requests still waiting for a human. */
static bool next_open_deadline(uint32_t now, uint32_t *out)
{
    bool found = false;
    for (size_t i = 0; i < s_q.count; i++) {
        const reqq_entry_t *e = &s_q.entries[i];
        if (e->answered) {
            continue;
        }
        if (!found || (int32_t)(e->deadline_ms - now) < (int32_t)(*out - now)) {
            *out = e->deadline_ms;
            found = true;
        }
    }
    return found;
}

static void schedule_nag(uint32_t now)
{
    uint32_t deadline;
    if (!next_open_deadline(now, &deadline)) {
        s_nag_at = 0;
        return;
    }
    const int32_t left = (int32_t)(deadline - now);
    if (left <= NAG_FINAL_WINDOW_MS) {
        s_nag_at = after(now, NAG_FINAL_MS);
    } else {
        s_nag_at = earliest(after(now, NAG_MS), deadline - NAG_FINAL_WINDOW_MS, now);
    }
}

static void queue_changed(uint32_t now)
{
    if (reqq_count(&s_q) == 0) {
        s_ack_due = 0;
        s_ack_first = 0;
        s_heartbeat_at = 0;
        s_nag_at = 0;
        stop_own_loop();
    } else if (s_nag_at == 0) {
        schedule_nag(now);
    }
}

static void send_holding(uint32_t now)
{
    char ids[REQQ_CAP][PROTO_ID_SIZE];
    const size_t n = reqq_ids(&s_q, ids, REQQ_CAP);
    s_ack_due = 0;
    s_ack_first = 0;
    if (n == 0) {
        s_heartbeat_at = 0;
        return;
    }
    reqq_mark_all_acked(&s_q);
    s_heartbeat_at = after(now, HEARTBEAT_MS);
    char extra[24];
    snprintf(extra, sizeof(extra), ",\"count\":%u", (unsigned)n);
    app_link_send(now, "holding", s_tx, proto_write_holding(s_tx, sizeof(s_tx), ids, n), extra);
}

static void schedule_ack(uint32_t now)
{
    if (s_ack_first == 0) {
        s_ack_first = now != 0 ? now : 1;
    }
    s_ack_due = earliest(after(now, ACK_DEBOUNCE_MS), after(s_ack_first, ACK_MAX_DELAY_MS), now);
}

static bool is_visible_head(const reqq_entry_t *e, uint32_t now)
{
    return e == reqq_head(&s_q) && s_screen == UI_SCREEN_APPROVAL && !identify_active(now);
}

/* ---- messages ---- */

static void on_permission(const proto_permission_t *p, uint32_t now)
{
    const reqq_add_t rc = reqq_add(&s_q, p, now);
    switch (rc) {
    case REQQ_DUPLICATE:
        request_evt("add", p->id, "duplicate");
        return;
    case REQQ_FULL:
        request_evt("add", p->id, "full");
        ESP_LOGW(TAG, "queue full, request not shown");
        return;
    case REQQ_ADDED:
        break;
    }
    request_evt("add", p->id, "new");
    app_user_activity(now);
    if (s_identify_until != 0) {
        s_identify_until = 0;
        if (feedback_current() == FB_SEQ_IDENTIFY) {
            feedback_stop();
        }
    }
    if (!s_buzzed || now - s_last_buzz >= BUZZ_MIN_GAP_MS) {
        s_buzzed = true;
        s_last_buzz = now;
        play(FB_SEQ_REQUEST, 0);
    } else if (!own_loop_playing() && feedback_current() == FB_SEQ_COUNT) {
        feedback_play(FB_SEQ_PENDING);
    }
    schedule_ack(now);
    queue_changed(now);
}

static const char *dismiss_reason_name(proto_dismiss_reason_t r)
{
    switch (r) {
    case PROTO_DISMISS_ANSWERED:
        return "answered";
    case PROTO_DISMISS_CANCELLED:
        return "cancelled";
    case PROTO_DISMISS_TIMEOUT:
        return "timeout";
    case PROTO_DISMISS_RELEASED:
        return "released";
    }
    return "";
}

static void on_dismiss(const proto_dismiss_t *d, uint32_t now)
{
    const reqq_entry_t *e = reqq_find(&s_q, d->id);
    if (e == NULL) {
        return;
    }
    const bool visible = is_visible_head(e, now);
    const bool ours = e->answered || (d->by[0] != '\0' && strcmp(d->by, s_device_id) == 0);
    reqq_remove(&s_q, d->id);
    request_evt("remove", d->id, dismiss_reason_name(d->reason));
    if (visible && !ours) {
        switch (d->reason) {
        case PROTO_DISMISS_ANSWERED:
            app_toast("Answered elsewhere");
            play(FB_SEQ_TICK, 0);
            break;
        case PROTO_DISMISS_CANCELLED:
            app_toast("Handled in terminal");
            break;
        case PROTO_DISMISS_TIMEOUT:
            app_toast("Timed out - use terminal");
            break;
        case PROTO_DISMISS_RELEASED:
            break;
        }
    }
    queue_changed(now);
}

static void on_error(const proto_error_t *err, uint32_t now)
{
    reqq_entry_t *e = err->has_id ? reqq_find(&s_q, err->id) : NULL;
    if (e == NULL) {
        ESP_LOGD(TAG, "server error %s", err->code);
        return;
    }
    if (strcmp(err->code, "UNKNOWN_REQUEST") == 0) {
        reqq_remove(&s_q, err->id);
        request_evt("remove", err->id, "unknown");
        app_toast("No longer pending");
        queue_changed(now);
    } else if (strcmp(err->code, "INVALID_MESSAGE") == 0) {
        e->answered = false;
        app_toast("Not delivered");
    } else {
        ESP_LOGD(TAG, "server error %s for a queued request", err->code);
    }
}

static void on_notify(const proto_notify_t *n, uint32_t now)
{
    const pet_event_class_t cls = pet_model_apply_notify(&s_pet, n);
    app_agent_activity(now);
    if (n->urgency == 0) {
        return;
    }
    app_user_activity(now);
    if (n->urgency >= 3) {
        if (n->detail[0] != '\0') {
            text_sanitize_ascii(s_attention_text, sizeof(s_attention_text), n->detail);
        } else {
            pet_notify_text(s_attention_text, sizeof(s_attention_text), n);
        }
        s_attention = true;
        play(FB_SEQ_ATTENTION, 0);
        return;
    }
    char text[TOAST_SIZE];
    pet_notify_text(text, sizeof(text), n);
    app_toast(text);
    if (n->urgency < 2) {
        return;
    }
    switch (cls) {
    case PET_EVENT_POSITIVE:
        play(FB_SEQ_POSITIVE, 0);
        break;
    case PET_EVENT_NEGATIVE:
        play(FB_SEQ_NEGATIVE, 0);
        break;
    case PET_EVENT_COMPLETE:
        play(FB_SEQ_COMPLETE, 0);
        break;
    case PET_EVENT_ATTENTION:
        play(FB_SEQ_ATTENTION, ATTENTION_ONCE_MS);
        break;
    case PET_EVENT_NONE:
        break;
    }
}

static void on_identify(uint32_t ms, uint32_t now)
{
    app_user_activity(now);
    ui_identify_set(app_link_name());
    s_identify_until = after(now, ms);
    play(FB_SEQ_IDENTIFY, ms);
}

uint8_t app_flow_pending_count(void)
{
    return (uint8_t)reqq_count(&s_q);
}

void app_flow_on_message(const proto_msg_t *msg, uint32_t now)
{
    switch (msg->type) {
    case PROTO_STATUS:
        pet_model_apply_status(&s_pet, &msg->u.status);
        app_agent_activity(now);
        break;
    case PROTO_MOOD:
        pet_model_apply_mood(&s_pet, &msg->u.mood);
        app_agent_activity(now);
        break;
    case PROTO_NOTIFY:
        on_notify(&msg->u.notify, now);
        break;
    case PROTO_PERMISSION:
        on_permission(&msg->u.permission, now);
        break;
    case PROTO_DISMISS:
        on_dismiss(&msg->u.dismiss, now);
        break;
    case PROTO_IDENTIFY:
        on_identify(msg->u.identify_ms, now);
        break;
    case PROTO_ERROR:
        on_error(&msg->u.error, now);
        break;
    case PROTO_UNKNOWN:
    case PROTO_REGISTER_ACK:
    case PROTO_PING:
    case PROTO_PONG:
    case PROTO_PEERS:
    case PROTO_MODE:
        break;
    }
}

void app_flow_on_link_lost(uint32_t now)
{
    if (reqq_count(&s_q) == 0) {
        return;
    }
    while (reqq_count(&s_q) > 0) {
        char id[PROTO_ID_SIZE];
        memcpy(id, reqq_head(&s_q)->id, sizeof(id));
        reqq_remove(&s_q, id);
        request_evt("remove", id, "link_lost");
    }
    app_toast("Connection lost - answer in terminal");
    queue_changed(now);
}

/* ---- buttons ---- */

static void answer(reqq_entry_t *e, bool granted, uint32_t now)
{
    char id[PROTO_ID_SIZE];
    memcpy(id, e->id, sizeof(id));
    char extra[80];
    snprintf(extra, sizeof(extra), ",\"id\":\"%s\",\"granted\":%s", id, granted ? "true" : "false");
    const int len = proto_write_permission_response(s_tx, sizeof(s_tx), id, granted);
    const bool sent = app_link_send(now, "permission_response", s_tx, len, extra);
    /* A failed send can take the link down, and the link loss empties the queue. */
    reqq_entry_t *still = reqq_find(&s_q, id);
    if (still == NULL) {
        return;
    }
    if (sent) {
        still->answered = true;
        still->answered_ms = now;
        schedule_nag(now);
    } else {
        app_toast("Not delivered");
    }
}

static void defer(reqq_entry_t *e, uint32_t now)
{
    char id[PROTO_ID_SIZE];
    memcpy(id, e->id, sizeof(id));
    char extra[48];
    snprintf(extra, sizeof(extra), ",\"id\":\"%s\"", id);
    const bool sent =
        app_link_send(now, "permission_defer", s_tx, proto_write_permission_defer(s_tx, sizeof(s_tx), id), extra);
    reqq_entry_t *still = reqq_find(&s_q, id);
    if (still == NULL) {
        return;
    }
    if (sent) {
        still->answered = true;
        still->answered_ms = now;
    } else {
        app_toast("Not delivered");
    }
}

static void on_approval_button(const btn_fsm_out_t *out, uint32_t now)
{
    reqq_entry_t *e = reqq_head(&s_q);
    if (e == NULL || e->answered) {
        return;
    }
    if (out->id == BTN_APPROVE && out->kind == BTN_CLICK) {
        play(FB_SEQ_CLICK, 0);
        if (e->risk == PROTO_RISK_HIGH) {
            app_toast("Hold to approve");
        } else {
            answer(e, true, now);
        }
    } else if (out->id == BTN_APPROVE && out->kind == BTN_LONG) {
        play(FB_SEQ_CLICK, 0);
        answer(e, true, now);
    } else if (out->id == BTN_REJECT && out->kind == BTN_CLICK) {
        play(FB_SEQ_CLICK, 0);
        answer(e, false, now);
    } else if (out->id == BTN_ACTION && out->kind == BTN_CLICK) {
        const size_t pages = text_wrap_pages(e->details, UI_DETAILS_COLS, UI_DETAILS_LINES);
        if (pages > 1) {
            play(FB_SEQ_CLICK, 0);
            e->page = (uint8_t)((e->page + 1) % pages);
        }
    } else if (out->id == BTN_ACTION && out->kind == BTN_LONG) {
        play(FB_SEQ_CLICK, 0);
        defer(e, now);
    }
}

static void pet_evt(ui_pet_reaction_t r)
{
    char evt[48];
    snprintf(evt, sizeof(evt), "{\"evt\":\"pet\",\"action\":\"%s\"}", ui_pet_reaction_name(r));
    console_evt(evt);
}

static void set_page(ui_pet_page_t page, uint32_t now)
{
    s_page = page;
    s_page_at = now;
    char evt[48];
    snprintf(evt, sizeof(evt), "{\"evt\":\"page\",\"page\":\"%s\"}", page == UI_PET_PAGE_ABOUT ? "about" : "pet");
    console_evt(evt);
}

static bool on_pet_button(const btn_fsm_out_t *out, uint32_t now)
{
    if (out->id == BTN_ACTION && out->kind == BTN_LONG) {
        return false;
    }
    if (out->id == BTN_REJECT && out->kind == BTN_LONG) {
        const proto_mode_t next = app_link_mode() == PROTO_MODE_DESK ? PROTO_MODE_ASK : PROTO_MODE_DESK;
        char extra[24];
        snprintf(extra, sizeof(extra), ",\"mode\":\"%s\"", proto_mode_name(next));
        app_link_send(now, "set_mode", s_tx, proto_write_set_mode(s_tx, sizeof(s_tx), next), extra);
        app_toast(next == PROTO_MODE_DESK ? "At desk: on" : "At desk: off");
        play(FB_SEQ_TICK, 0);
    } else if (out->id == BTN_ACTION) {
        set_page(s_page == UI_PET_PAGE_PET ? UI_PET_PAGE_ABOUT : UI_PET_PAGE_PET, now);
    } else if (s_page == UI_PET_PAGE_ABOUT) {
        s_page_at = now;
    } else if (out->id == BTN_APPROVE) {
        ui_pet_react(UI_PET_REACT_PAT);
        play(FB_SEQ_TICK, 0);
        pet_evt(UI_PET_REACT_PAT);
    } else {
        const ui_pet_reaction_t r = pet_pokes_add(&s_pokes, now) ? UI_PET_REACT_DIZZY : UI_PET_REACT_POKE;
        ui_pet_react(r);
        pet_evt(r);
    }
    return true;
}

bool app_flow_on_button(const btn_fsm_out_t *out, uint32_t now)
{
    if (s_attention) {
        /* The press that acknowledges an urgent notification does nothing else. */
        s_attention = false;
        if (feedback_current() == FB_SEQ_ATTENTION) {
            feedback_stop();
        }
        return true;
    }
    if (identify_active(now)) {
        return true;
    }
    if (s_screen == UI_SCREEN_APPROVAL) {
        on_approval_button(out, now);
        return true;
    }
    return on_pet_button(out, now);
}

void app_flow_show_about(uint32_t now)
{
    set_page(UI_PET_PAGE_ABOUT, now);
}

bool app_flow_takes_over(uint32_t now)
{
    return identify_active(now) || reqq_head(&s_q) != NULL;
}

/* ---- timers ---- */

static void on_timer_cb(void *arg)
{
    (void)arg;
    const app_event_t ev = {.id = APP_EV_FLOW_TIMER, .t_ms = app_now_ms()};
    app_post(&ev);
}

static void run_deadlines(uint32_t now)
{
    char id[PROTO_ID_SIZE];
    bool expired = false;
    while (reqq_expire(&s_q, now, id)) {
        request_evt("remove", id, "expired");
        expired = true;
    }
    if (expired) {
        app_toast("Timed out - use terminal");
        queue_changed(now);
        if (reqq_count(&s_q) > 0) {
            schedule_nag(now);
        }
    }
    for (size_t i = 0; i < s_q.count; i++) {
        reqq_entry_t *e = &s_q.entries[i];
        if (e->answered && now - e->answered_ms >= CONFIRM_MS) {
            e->answered = false;
            app_toast("Not delivered");
            schedule_nag(now);
        }
    }
    if (due(s_ack_due, now) || (due(s_heartbeat_at, now) && reqq_count(&s_q) > 0)) {
        send_holding(now);
    }
    if (due(s_nag_at, now)) {
        s_nag_at = 0;
        uint32_t deadline;
        if (next_open_deadline(now, &deadline)) {
            play(FB_SEQ_NAG, 0);
        }
        schedule_nag(now);
    }
    if (s_identify_until != 0 && due(s_identify_until, now)) {
        s_identify_until = 0;
    }
    if (s_page == UI_PET_PAGE_ABOUT && due(after(s_page_at, PAGE_IDLE_MS), now)) {
        set_page(UI_PET_PAGE_PET, now);
    }
}

void app_flow_run_deadlines(uint32_t now)
{
    run_deadlines(now);
}

void app_flow_arm_timer(uint32_t now)
{
    uint32_t at = earliest(s_ack_due, s_heartbeat_at, now);
    at = earliest(at, s_nag_at, now);
    at = earliest(at, s_identify_until, now);
    uint32_t deadline;
    if (reqq_next_deadline(&s_q, now, &deadline)) {
        at = earliest(at, deadline, now);
    }
    for (size_t i = 0; i < s_q.count; i++) {
        if (s_q.entries[i].answered) {
            at = earliest(at, after(s_q.entries[i].answered_ms, CONFIRM_MS), now);
        }
    }
    if (s_screen == UI_SCREEN_APPROVAL && reqq_count(&s_q) > 0) {
        at = earliest(at, after(now, BAR_REFRESH_MS), now);
    }
    if (s_page == UI_PET_PAGE_ABOUT) {
        at = earliest(at, after(s_page_at, PAGE_IDLE_MS), now);
    }
    esp_timer_stop(s_timer);
    if (at == 0) {
        return;
    }
    const int32_t ms = (int32_t)(at - now);
    esp_timer_start_once(s_timer, ms > 1 ? (uint64_t)ms * 1000 : 1000);
}

/* ---- rendering ---- */

static void publish_view(const app_flow_view_t *v)
{
    if (xSemaphoreTake(s_view_lock, pdMS_TO_TICKS(VIEW_LOCK_MS)) == pdTRUE) {
        s_view = *v;
        xSemaphoreGive(s_view_lock);
    }
}

void app_flow_get_view(app_flow_view_t *out)
{
    memset(out, 0, sizeof(*out));
    if (xSemaphoreTake(s_view_lock, pdMS_TO_TICKS(VIEW_LOCK_MS)) == pdTRUE) {
        *out = s_view;
        xSemaphoreGive(s_view_lock);
    }
}

static void set_banner(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        if (s_banner_visible) {
            s_banner_visible = false;
            s_banner_shown[0] = '\0';
            ui_banner_hide();
        }
        return;
    }
    if (!s_banner_visible || strcmp(s_banner_shown, text) != 0) {
        s_banner_visible = true;
        strlcpy(s_banner_shown, text, sizeof(s_banner_shown));
        ui_banner_show(text);
    }
}

void app_flow_hide_banner(void)
{
    set_banner(NULL);
}

static void render_approval(const reqq_entry_t *e, uint8_t position, uint8_t count, uint32_t now,
                            app_flow_view_t *view)
{
    /* Static: a details page does not belong on the app task's stack. */
    static char page_text[UI_DETAILS_COLS * UI_DETAILS_LINES + UI_DETAILS_LINES + 1];
    const size_t pages = text_wrap_pages(e->details, UI_DETAILS_COLS, UI_DETAILS_LINES);
    const size_t page = pages > 0 ? e->page % pages : 0;
    if (pages == 0 || !text_wrap_page(page_text, sizeof(page_text), e->details, UI_DETAILS_COLS, UI_DETAILS_LINES,
                                      page)) {
        page_text[0] = '\0';
    }
    const int32_t left = (int32_t)(e->deadline_ms - now);
    uint16_t permille = 0;
    if (left > 0 && e->timeout_ms > 0) {
        const uint64_t p = (uint64_t)left * 1000 / e->timeout_ms;
        permille = p > 1000 ? 1000 : (uint16_t)p;
    }
    const ui_approval_t a = {
        .tool = e->tool,
        .risk = e->risk,
        .project = e->project,
        .summary = e->summary,
        .details = page_text,
        .position = position,
        .count = count,
        .page = (uint8_t)(page + 1),
        .pages = (uint8_t)pages,
        .remaining_permille = permille,
        .sent = e->answered,
    };
    ui_approval_set(&a);

    view->has_request = true;
    strlcpy(view->request.id, e->id, sizeof(view->request.id));
    strlcpy(view->request.tool, e->tool, sizeof(view->request.tool));
    strlcpy(view->request.summary, e->summary, sizeof(view->request.summary));
    view->request.risk = e->risk;
    view->request.position = position;
    view->request.count = count;
    view->request.page = (uint8_t)(page + 1);
    view->request.pages = (uint8_t)pages;
    view->request.deadline_ms = e->deadline_ms;
    view->request.answered = e->answered;
}

static void preview_entry(reqq_entry_t *e, bool high, uint32_t now)
{
    memset(e, 0, sizeof(*e));
    strlcpy(e->id, "0123456789abcdef0123456789abcdef", sizeof(e->id));
    strlcpy(e->tool, "Bash", sizeof(e->tool));
    strlcpy(e->project, "devotchi", sizeof(e->project));
    if (high) {
        strlcpy(e->summary, "rm -rf dist build node_modules/.cache", sizeof(e->summary));
        strlcpy(e->details,
                "Remove build output and caches before a clean rebuild of every package\n"
                "$ rm -rf dist build node_modules/.cache\n"
                "cwd /work/devotchi",
                sizeof(e->details));
        e->risk = PROTO_RISK_HIGH;
    } else {
        strlcpy(e->summary, "Run the unit tests", sizeof(e->summary));
        strlcpy(e->details, "$ pnpm test --filter server", sizeof(e->details));
        e->risk = PROTO_RISK_LOW;
    }
    e->timeout_ms = 30000;
    e->deadline_ms = now + 21000;
}

static void render_about(void)
{
    /* Static: app task only, and the snapshot is too large for its stack. */
    static app_snapshot_t snap;
    static char server[STORAGE_URL_SIZE];
    char uptime[24];
    char rssi[16];
    char link[40];
    char id[APP_DEVICE_ID_SIZE];
    wifi_status_t w;
    storage_url_source_t source;

    app_get_snapshot(&snap);
    wifi_get_status(&w);
    storage_get_server_url(server, &source);
    app_get_device_id(id);
    const uint32_t up = app_uptime_s();
    snprintf(uptime, sizeof(uptime), "%luh %02lum %02lus", (unsigned long)(up / 3600), (unsigned long)(up / 60 % 60),
             (unsigned long)(up % 60));
    if (w.connected) {
        snprintf(rssi, sizeof(rssi), "%d dBm", w.rssi);
    } else {
        strlcpy(rssi, "-", sizeof(rssi));
    }
    snprintf(link, sizeof(link), "%s (%s)", app_state_name(snap.state), app_ws_state_name(snap.link.ws));
    const char *values[UI_ABOUT_COUNT] = {
        [UI_ABOUT_NAME] = snap.link.name[0] != '\0' ? snap.link.name : "-",
        [UI_ABOUT_FW] = esp_app_get_description()->version,
        [UI_ABOUT_DEVICE_ID] = id,
        [UI_ABOUT_UPTIME] = uptime,
        [UI_ABOUT_RESET] = app_reset_reason(),
        [UI_ABOUT_SSID] = snap.link.ssid[0] != '\0' ? snap.link.ssid : "-",
        [UI_ABOUT_IP] = w.connected ? w.ip : "-",
        [UI_ABOUT_RSSI] = rssi,
        [UI_ABOUT_SERVER] = snap.link.server[0] != '\0' ? snap.link.server : server,
        [UI_ABOUT_LINK] = link,
        [UI_ABOUT_ERROR] = snap.link.last_error[0] != '\0' ? snap.link.last_error : "-",
        [UI_ABOUT_NVS] = storage_is_degraded() ? "error, defaults in use" : "ok",
    };
    ui_about_set(values);
}

ui_screen_id_t app_flow_render(uint32_t now, const char *offline_banner, app_flow_preview_t preview)
{
    /* Static: the view is larger than the app task's stack allows for one buffer. */
    static app_flow_view_t view;
    memset(&view, 0, sizeof(view));
    view.mood = s_pet.mood;
    ui_screen_id_t screen;
    if (preview != APP_FLOW_PREVIEW_OFF) {
        set_banner(NULL);
        if (preview == APP_FLOW_PREVIEW_IDENTIFY) {
            ui_identify_set(app_link_name());
            screen = UI_SCREEN_IDENTIFY;
        } else {
            static reqq_entry_t sample;
            preview_entry(&sample, preview == APP_FLOW_PREVIEW_APPROVAL_HIGH, now);
            render_approval(&sample, 1, preview == APP_FLOW_PREVIEW_APPROVAL_HIGH ? 3 : 1, now, &view);
            screen = UI_SCREEN_APPROVAL;
        }
        s_screen = screen;
        publish_view(&view);
        return screen;
    }

    const bool identify = identify_active(now);
    const reqq_entry_t *head = reqq_head(&s_q);
    if (identify || head != NULL) {
        s_page = UI_PET_PAGE_PET;
    }
    /* Identify is full-screen: the name must not sit under a banner; the About page shows the link itself. */
    const bool about = s_page == UI_PET_PAGE_ABOUT;
    set_banner(identify ? NULL : s_attention ? s_attention_text : about ? NULL : offline_banner);
    if (identify) {
        screen = UI_SCREEN_IDENTIFY;
    } else if (head != NULL) {
        render_approval(head, 1, (uint8_t)reqq_count(&s_q), now, &view);
        screen = UI_SCREEN_APPROVAL;
    } else {
        ui_pet_set(s_pet.project, s_pet.mood, s_pet.status);
        if (about) {
            render_about();
        }
        ui_pet_page_set(s_page);
        screen = UI_SCREEN_PET;
    }
    view.page = s_page;
    s_screen = screen;
    publish_view(&view);
    return screen;
}

esp_err_t app_flow_init(void)
{
    reqq_init(&s_q);
    pet_model_init(&s_pet);
    pet_pokes_init(&s_pokes);
    app_get_device_id(s_device_id);
    s_view_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_view_lock != NULL, ESP_ERR_NO_MEM, TAG, "lock");
    const esp_timer_create_args_t timer = {.callback = on_timer_cb, .name = "flow"};
    return esp_timer_create(&timer, &s_timer);
}
