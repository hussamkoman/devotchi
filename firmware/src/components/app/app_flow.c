#include "app_priv.h"

#include "app.h"
#include "card_fsm.h"
#include "console.h"
#include "fb_seq.h"
#include "feedback.h"
#include "pet_model.h"
#include "pet_pose.h"
#include "proto.h"
#include "qstore.h"
#include "reqq.h"
#include "showcase.h"
#include "storage.h"
#include "text.h"
#include "ui.h"
#include "ui_icons.h"
#include "wifi.h"

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "power.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
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
/* Home shows its button tabs this long after a press (design-system 5). */
#define HOME_TABS_MS        3000
/* The receipt strip over the next card lasts as long as that card arms (ux-flows 6). */
#define RECEIPT_STRIP_MS    CARD_ARM_NEXT_MS

/* Everything below belongs to the app task, except s_view (guarded by s_view_lock). */

static reqq_t s_q;
/* Bodies of the questions and plans in s_q (and of calls still arriving); the slots live in PSRAM. */
static qstore_t s_store;
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
static bool s_banner_attention;

static ui_screen_id_t s_screen = UI_SCREEN_PET;
/* The home pet loops its attention reaction until a press or the next status. */
static bool s_pet_attention;
static ui_pet_page_t s_page = UI_PET_PAGE_PET;
static uint32_t s_page_at;
static pet_pokes_t s_pokes;

static SemaphoreHandle_t s_view_lock;
static app_flow_view_t s_view;

/* The card of the head request; s_card_id is "" while no card is open. */
static card_fsm_t s_card;
static char s_card_id[PROTO_ID_SIZE];
static bool s_card_visible;
/* A visible card left the queue with a receipt: the next card replaces it (28b). */
static bool s_card_replaced;
/* The card on screen is a real request opened in or just after the showcase: hold to allow, REAL REQUEST bar. */
static bool s_card_guarded;
/* The resting face is on screen (not the showcase's clock page). */
static bool s_clock_visible;

/* Showcase (protocol 14.7): the sample card is a card instance of its own, never in s_q. */
static showcase_t s_sc;
static reqq_entry_t s_sample;
static qstore_slot_t *s_sample_slot;
static bool s_sample_open;

/* About 2/2: requests this device held and answered since boot. */
static uint32_t s_held_total;
static uint32_t s_answered_total;

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

/* Toasts and request fields are sanitised UTF-8 without control characters but '\n'; only quotes, backslashes and newlines need escaping. */
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

void app_toast_show(ui_toast_kind_t kind, const char *text, const char *detail, uint32_t ms)
{
    ui_toast_show(kind, text, detail, ms);
    /* Static: app task only, and too large for its stack. */
    static char escaped[2 * TOAST_SIZE];
    static char evt[EVT_SIZE + 2 * TOAST_SIZE];
    json_escape(escaped, sizeof(escaped), text);
    snprintf(evt, sizeof(evt), "{\"evt\":\"toast\",\"text\":\"%s\"}", escaped);
    console_evt(evt);
}

void app_toast_kind(ui_toast_kind_t kind, const char *text)
{
    app_toast_show(kind, text, NULL, UI_TOAST_DEFAULT_MS);
}

void app_toast(const char *text)
{
    app_toast_kind(UI_TOAST_INFO, text);
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

static bool request_screen(ui_screen_id_t screen)
{
    return screen == UI_SCREEN_APPROVAL || screen == UI_SCREEN_QUESTION || screen == UI_SCREEN_PLAN;
}

static bool is_visible_head(const reqq_entry_t *e, uint32_t now)
{
    return e == reqq_head(&s_q) && request_screen(s_screen) && !identify_active(now);
}

/* Takes a request out of the queue and frees the body of a question or plan. */
static void drop_request(const char *id)
{
    char copy[PROTO_ID_SIZE];
    strlcpy(copy, id, sizeof(copy));
    reqq_remove(&s_q, copy);
    qstore_release(&s_store, copy);
}

/* The request whose card the buttons drive: the queue's head, or the showcase's sample. */
static reqq_entry_t *card_entry(void)
{
    reqq_entry_t *head = reqq_head(&s_q);
    if (head != NULL) {
        return head;
    }
    return s_sample_open ? &s_sample : NULL;
}

/* The entry is still the one on screen after a send (a failed send can empty the queue). */
static bool card_still(const reqq_entry_t *e)
{
    return e == &s_sample ? s_sample_open : reqq_find(&s_q, s_card_id) == e;
}

static void showcase_evt(const char *op)
{
    char evt[96];
    snprintf(evt, sizeof(evt), "{\"evt\":\"showcase\",\"op\":\"%s\",\"page\":\"%s\",\"active\":%s}", op,
             showcase_page_name(s_sc.page), s_sc.active ? "true" : "false");
    console_evt(evt);
}

/* Ends the showcase (and its sample card); the 10 s guard of ux-flows 8 starts. */
static void showcase_end(uint32_t now, const char *why)
{
    if (!s_sc.active) {
        return;
    }
    showcase_leave(&s_sc, now);
    s_sample_open = false;
    s_page = UI_PET_PAGE_PET;
    ui_pet_set_caption(NULL, NULL, NULL, NULL);
    showcase_evt(why);
}

/* ---- messages ---- */

/* A request joined the queue: it is named in `holding` soon, and the device buzzes once. */
static void request_added(const char *id, uint32_t now)
{
    /* A real request ends the showcase before it is shown (protocol 14.7). */
    showcase_end(now, "request");
    app_settings_to_top(now);
    s_held_total++;
    request_evt("add", id, "new");
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
    request_added(p->id, now);
}

/*
 * A frame of a question or plan went into the store. Only a complete request
 * joins the queue (and so `holding`); one without a slot is never named and
 * the server lets it go neutral or another consumer answers it (protocol 14.10).
 */
static void on_ask_frame(const char *id, qstore_rc_t rc, uint32_t now)
{
    if (rc != QSTORE_COMPLETE) {
        request_evt(rc == QSTORE_PARTIAL ? "frame" : "add", id, qstore_rc_name(rc));
        if (rc == QSTORE_FULL) {
            ESP_LOGW(TAG, "question store full, request not shown");
        }
        return;
    }
    switch (reqq_add_slot(&s_q, qstore_find(&s_store, id))) {
    case REQQ_ADDED:
        request_added(id, now);
        return;
    case REQQ_DUPLICATE:
        request_evt("add", id, "duplicate");
        return;
    case REQQ_FULL:
        qstore_release(&s_store, id);
        request_evt("add", id, "full");
        return;
    }
}

/* The server moved a question's or plan's deadline (protocol 14.6); an unknown id is ignored. */
static void on_deadline(const proto_deadline_t *d, uint32_t now)
{
    if (!reqq_set_deadline(&s_q, d->id, d->timeout_ms, now)) {
        return;
    }
    if (strcmp(d->id, s_card_id) == 0) {
        card_fsm_set_deadline(&s_card, reqq_find(&s_q, d->id)->deadline_ms);
    }
    schedule_nag(now);
    char evt[96];
    snprintf(evt, sizeof(evt), "{\"evt\":\"deadline\",\"id\":\"%s\",\"timeout\":%lu}", d->id,
             (unsigned long)d->timeout_ms);
    console_evt(evt);
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

static ui_toast_kind_t tone_kind(card_tone_t tone)
{
    switch (tone) {
    case CARD_TONE_ALLOWED:
        return UI_TOAST_POSITIVE;
    case CARD_TONE_DENIED:
        return UI_TOAST_NEGATIVE;
    case CARD_TONE_NEUTRAL:
        break;
    }
    return UI_TOAST_INFO;
}

/*
 * A visible card closed: the receipt is a strip over the next card while it
 * arms, or a toast on home (ux-flows 3.2). Called after the entry left the queue.
 */
static void show_receipt(const card_receipt_t *r)
{
    static char text[2 * TOAST_SIZE];
    static char escaped[4 * TOAST_SIZE];
    static char evt[EVT_SIZE + 4 * TOAST_SIZE];
    const bool strip = reqq_count(&s_q) > 0;
    if (strip) {
        snprintf(text, sizeof(text), "%s \xC2\xB7 next request", r->title);
        ui_receipt_strip(tone_kind(r->tone), text, RECEIPT_STRIP_MS);
        s_card_replaced = true;
    } else {
        ui_toast_show(tone_kind(r->tone), r->title, r->detail[0] != '\0' ? r->detail : NULL, UI_TOAST_DEFAULT_MS);
        snprintf(text, sizeof(text), r->detail[0] != '\0' ? "%s \xC2\xB7 %s" : "%s%s", r->title, r->detail);
    }
    json_escape(escaped, sizeof(escaped), text);
    snprintf(evt, sizeof(evt), "{\"evt\":\"receipt\",\"text\":\"%s\",\"strip\":%s}", escaped,
             strip ? "true" : "false");
    console_evt(evt);
}

static void on_dismiss(const proto_dismiss_t *d, uint32_t now)
{
    reqq_entry_t *e = reqq_find(&s_q, d->id);
    if (e == NULL) {
        /* A call still being assembled is over too: its slot is free for the next request at once. */
        if (qstore_release(&s_store, d->id)) {
            request_evt("remove", d->id, dismiss_reason_name(d->reason));
        }
        return;
    }
    const bool visible = is_visible_head(e, now);
    /* `by` wins over e->answered: this device's answer may have lost the race to another consumer's. */
    const bool ours = d->by[0] != '\0' ? strcmp(d->by, s_device_id) == 0 : e->answered;
    /* Static: the entry is about to be overwritten by the queue shift, and the app task's stack is small. */
    static card_receipt_t r;
    card_receipt_dismiss(&r, d, ours, e);
    drop_request(d->id);
    request_evt("remove", d->id, dismiss_reason_name(d->reason));
    if (visible) {
        show_receipt(&r);
        if (!ours && d->reason == PROTO_DISMISS_ANSWERED) {
            play(FB_SEQ_TICK, 0);
        }
    }
    queue_changed(now);
}

/* The server refused an answer: the card's buttons come back. */
static void unsend(reqq_entry_t *e)
{
    e->answered = false;
    if (strcmp(e->id, s_card_id) == 0) {
        card_fsm_set_sent(&s_card, false);
    }
}

static void on_error(const proto_error_t *err, uint32_t now)
{
    reqq_entry_t *e = err->has_id ? reqq_find(&s_q, err->id) : NULL;
    if (e == NULL) {
        ESP_LOGD(TAG, "server error %s", err->code);
        return;
    }
    if (strcmp(err->code, "UNKNOWN_REQUEST") == 0) {
        drop_request(err->id);
        request_evt("remove", err->id, "unknown");
        app_toast("No longer pending");
        queue_changed(now);
    } else if (strcmp(err->code, "INVALID_MESSAGE") == 0) {
        unsend(e);
        app_toast("Not delivered");
    } else if (strcmp(err->code, "NOT_ALLOWED") == 0) {
        /* Only when the device and the server disagree about `cut`; the request stays pending. */
        unsend(e);
        app_toast("Not allowed from this device");
    } else {
        ESP_LOGD(TAG, "server error %s for a queued request", err->code);
    }
}

static void pet_mood_changed(proto_mood_t before);

/* Agent traffic does not wake a dark screen, but it turns a lit clock back into home (ux-flows 7). */
static void agent_traffic(uint32_t now)
{
    app_agent_activity(now);
    if (s_clock_visible) {
        app_user_activity(now);
    }
}

static void on_notify(const proto_notify_t *n, uint32_t now)
{
    const proto_mood_t before = s_pet.mood;
    const pet_event_class_t cls = pet_model_apply_notify(&s_pet, n);
    static const pet_react_t REACT[] = {
        [PET_EVENT_NONE] = PET_REACT_NONE,         [PET_EVENT_POSITIVE] = PET_REACT_POSITIVE,
        [PET_EVENT_NEGATIVE] = PET_REACT_NEGATIVE, [PET_EVENT_COMPLETE] = PET_REACT_COMPLETE,
        [PET_EVENT_ATTENTION] = PET_REACT_ATTENTION,
    };
    pet_mood_changed(before);
    char line[TOAST_SIZE];
    pet_notify_text(line, sizeof(line), n);
    pet_model_add_recent(&s_pet, line, now, app_clock_epoch_ms());
    if (n->urgency > 0 && (size_t)cls < sizeof(REACT) / sizeof(REACT[0]) && REACT[cls] != PET_REACT_NONE) {
        ui_pet_react(REACT[cls]);
        s_pet_attention = cls == PET_EVENT_ATTENTION;
    }
    agent_traffic(now);
    if (n->urgency == 0) {
        return;
    }
    app_user_activity(now);
    if (n->urgency >= 3) {
        if (n->detail[0] != '\0') {
            text_sanitize_device(s_attention_text, sizeof(s_attention_text), n->detail);
        } else {
            pet_notify_text(s_attention_text, sizeof(s_attention_text), n);
        }
        s_attention = true;
        play(FB_SEQ_ATTENTION, 0);
        return;
    }
    char text[TOAST_SIZE];
    pet_notify_text(text, sizeof(text), n);
    app_toast_kind(cls == PET_EVENT_POSITIVE || cls == PET_EVENT_COMPLETE ? UI_TOAST_POSITIVE
                   : cls == PET_EVENT_NEGATIVE                            ? UI_TOAST_NEGATIVE
                   : cls == PET_EVENT_ATTENTION                           ? UI_TOAST_ATTENTION
                                                                          : UI_TOAST_INFO,
                   text);
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

bool app_flow_mood_idle(void)
{
    return s_pet.mood == MOOD_IDLE || s_pet.mood == MOOD_SLEEPING;
}

uint8_t app_flow_pending_count(void)
{
    return (uint8_t)reqq_count(&s_q);
}

/* The pet's own reactions to what the agent does (pet-animations 3). */
static void pet_mood_changed(proto_mood_t before)
{
    if (s_pet_attention) {
        s_pet_attention = false;
        ui_pet_react(PET_REACT_NONE);
    }
    if (before == MOOD_SLEEPING && s_pet.mood != MOOD_SLEEPING) {
        ui_pet_react(PET_REACT_WAKE);
    }
}

void app_flow_on_message(const proto_msg_t *msg, uint32_t now)
{
    const proto_mood_t before = s_pet.mood;
    switch (msg->type) {
    case PROTO_STATUS:
        pet_model_apply_status(&s_pet, &msg->u.status);
        pet_model_add_recent(&s_pet, s_pet.status, now, app_clock_epoch_ms());
        agent_traffic(now);
        pet_mood_changed(before);
        break;
    case PROTO_MOOD:
        pet_model_apply_mood(&s_pet, &msg->u.mood);
        agent_traffic(now);
        pet_mood_changed(before);
        break;
    case PROTO_NOTIFY:
        on_notify(&msg->u.notify, now);
        break;
    case PROTO_PERMISSION:
        on_permission(&msg->u.permission, now);
        break;
    case PROTO_QUESTION:
        on_ask_frame(msg->u.question.id, qstore_add_question(&s_store, &msg->u.question, now), now);
        break;
    case PROTO_PLAN:
        on_ask_frame(msg->u.plan.id, qstore_add_plan(&s_store, &msg->u.plan, now), now);
        break;
    case PROTO_DEADLINE:
        on_deadline(&msg->u.deadline, now);
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
    case PROTO_PET_CMD:
    case PROTO_MODE:
    case PROTO_TIME:
        break;
    }
}

void app_flow_on_link_lost(uint32_t now)
{
    /* Calls still arriving are resent whole after the next register_ack (protocol 14.4). */
    qstore_clear(&s_store);
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

static const char *const NUDGE_TEXT[BTN_COUNT] = {
    [BTN_APPROVE] = "Hold to allow",
    [BTN_REJECT] = "",
    [BTN_ACTION] = "Hold for more",
};

static ui_tab_btn_t tab_of(btn_id_t id)
{
    switch (id) {
    case BTN_REJECT:
        return UI_TAB_REJECT;
    case BTN_ACTION:
        return UI_TAB_ACTION;
    case BTN_APPROVE:
    case BTN_COUNT:
        break;
    }
    return UI_TAB_APPROVE;
}

static proto_choice_t decide_choice(card_decision_t d)
{
    switch (d) {
    case CARD_DECISION_ALLOW_SESSION:
        return PROTO_CHOICE_ALLOW_SESSION;
    case CARD_DECISION_DENY_ASK_WHY:
        return PROTO_CHOICE_DENY_ASK_WHY;
    case CARD_DECISION_DENY_STOP:
        return PROTO_CHOICE_DENY_STOP;
    case CARD_DECISION_DECLINE:
        return PROTO_CHOICE_DECLINE;
    case CARD_DECISION_APPROVE:
        return PROTO_CHOICE_APPROVE;
    case CARD_DECISION_APPROVE_EDITS:
        return PROTO_CHOICE_APPROVE_ACCEPT_EDITS;
    case CARD_DECISION_REVISE:
        return PROTO_CHOICE_REVISE;
    default:
        break;
    }
    return PROTO_CHOICE_NONE;
}

/* `[[0],[1,3],null]` for the console event; the frame itself comes from proto_write_question_response(). */
static void answers_json(char *dst, size_t cap, const proto_answer_t *a, size_t n)
{
    size_t len = (size_t)snprintf(dst, cap, "[");
    for (size_t i = 0; i < n && len < cap; i++) {
        len += (size_t)snprintf(dst + len, cap - len, "%s", i > 0 ? "," : "");
        if (a[i].no_preference) {
            len += (size_t)snprintf(dst + len, cap - len, "null");
            continue;
        }
        len += (size_t)snprintf(dst + len, cap - len, "[");
        bool first = true;
        for (unsigned bit = 0; bit < PROTO_Q_OPTIONS_MAX && len < cap; bit++) {
            if ((a[i].mask & (1u << bit)) != 0) {
                len += (size_t)snprintf(dst + len, cap - len, first ? "%u" : ",%u", bit);
                first = false;
            }
        }
        len += len < cap ? (size_t)snprintf(dst + len, cap - len, "]") : 0;
    }
    if (len < cap) {
        snprintf(dst + len, cap - len, "]");
    }
}

/* The frame of a decision; -1 for none. answers: the question_response answers (n of them). */
static int write_decision(card_decision_t d, const char *id, const proto_answer_t *answers, size_t n,
                          const char **type, char *extra, size_t extra_cap)
{
    switch (d) {
    case CARD_DECISION_ALLOW:
    case CARD_DECISION_DENY:
        *type = "permission_response";
        snprintf(extra, extra_cap, ",\"id\":\"%s\",\"granted\":%s", id,
                 d == CARD_DECISION_ALLOW ? "true" : "false");
        return proto_write_permission_response(s_tx, sizeof(s_tx), id, d == CARD_DECISION_ALLOW);
    case CARD_DECISION_DEFER:
        *type = "permission_defer";
        snprintf(extra, extra_cap, ",\"id\":\"%s\"", id);
        return proto_write_permission_defer(s_tx, sizeof(s_tx), id);
    case CARD_DECISION_ANSWER:
    case CARD_DECISION_NO_PREFERENCE: {
        if (n == 0) {
            return -1;
        }
        char json[64];
        answers_json(json, sizeof(json), answers, n);
        *type = "question_response";
        snprintf(extra, extra_cap, ",\"id\":\"%s\",\"answers\":%s", id, json);
        return proto_write_question_response(s_tx, sizeof(s_tx), id, answers, n);
    }
    case CARD_DECISION_ALLOW_SESSION:
    case CARD_DECISION_DENY_ASK_WHY:
    case CARD_DECISION_DENY_STOP:
    case CARD_DECISION_DECLINE:
    case CARD_DECISION_APPROVE:
    case CARD_DECISION_APPROVE_EDITS:
    case CARD_DECISION_REVISE: {
        const proto_choice_t choice = decide_choice(d);
        *type = "decide";
        snprintf(extra, extra_cap, ",\"id\":\"%s\",\"choice\":\"%s\"", id, proto_choice_name(choice));
        return proto_write_decide(s_tx, sizeof(s_tx), id, choice);
    }
    case CARD_DECISION_NONE:
        break;
    }
    return -1;
}

/*
 * What the entry itself forbids, checked again next to the serialiser: a
 * session grant needs a whole command with a grant; a plan approval needs
 * every step on the device, and accepting all edits needs the whole plan.
 */
static bool entry_refuses(const reqq_entry_t *e, card_decision_t d)
{
    const bool plan = e->kind == REQQ_PLAN && e->slot != NULL;
    switch (d) {
    case CARD_DECISION_ALLOW_SESSION:
        return e->kind != REQQ_PERMISSION || !e->has_grant || e->cut;
    case CARD_DECISION_APPROVE:
        return !plan || e->slot->u.plan.more > 0;
    case CARD_DECISION_APPROVE_EDITS:
        return !plan || e->slot->u.plan.more > 0 || e->slot->u.plan.cut;
    case CARD_DECISION_ANSWER:
    case CARD_DECISION_NO_PREFERENCE:
    case CARD_DECISION_DECLINE:
        return e->kind != REQQ_QUESTION;
    default:
        return false;
    }
}

static const char *sample_word(card_decision_t d)
{
    switch (d) {
    case CARD_DECISION_ALLOW:
    case CARD_DECISION_ALLOW_SESSION:
        return "allowed";
    case CARD_DECISION_DENY:
    case CARD_DECISION_DENY_ASK_WHY:
    case CARD_DECISION_DENY_STOP:
        return "denied";
    case CARD_DECISION_ANSWER:
    case CARD_DECISION_NO_PREFERENCE:
        return "answered";
    case CARD_DECISION_DECLINE:
        return "declined";
    default:
        break;
    }
    return "sent to terminal";
}

/* The end of a sample card: a receipt, back to the showcase menu. */
static void sample_end(const char *word, bool positive)
{
    char text[48];
    snprintf(text, sizeof(text), "Sample: %s", word);
    ui_toast_show(positive ? UI_TOAST_POSITIVE : UI_TOAST_INFO, text, NULL, UI_TOAST_DEFAULT_MS);
    char evt[128];
    snprintf(evt, sizeof(evt), "{\"evt\":\"receipt\",\"text\":\"%s\",\"strip\":false,\"sample\":true}", text);
    console_evt(evt);
    s_sample_open = false;
    showcase_back(&s_sc);
}

static void sample_decided(card_decision_t d)
{
    sample_end(sample_word(d), d == CARD_DECISION_ALLOW || d == CARD_DECISION_ALLOW_SESSION ||
                                   d == CARD_DECISION_ANSWER);
}

/*
 * The only way a decision reaches the wire. The card's own check runs again
 * here, next to the serialiser, with the entry's: nothing goes out for a sent
 * or expired card, before the reading is done, or for a choice the request does not allow.
 */
static void send_decision(reqq_entry_t *e, card_decision_t d, uint32_t now)
{
    if (strcmp(e->id, s_card_id) != 0 || !card_fsm_may_send(&s_card, d, now) || entry_refuses(e, d)) {
        ESP_LOGW(TAG, "decision %s refused for the card on screen", card_decision_name(d));
        return;
    }
    /* The sample flag travels with the card instance: a sample answer becomes a receipt and nothing is sent. */
    if (e->sample) {
        sample_decided(d);
        return;
    }
    char id[PROTO_ID_SIZE];
    memcpy(id, e->id, sizeof(id));
    proto_answer_t answers[PROTO_Q_MAX];
    const size_t n = card_fsm_answers(&s_card, d, answers);
    const char *type = "";
    char extra[160];
    const int len = write_decision(d, id, answers, n, &type, extra, sizeof(extra));
    const bool sent = len > 0 && app_link_send(now, type, s_tx, len, extra);
    /* A failed send can take the link down, and the link loss empties the queue. */
    reqq_entry_t *still = reqq_find(&s_q, id);
    if (still == NULL) {
        return;
    }
    if (sent) {
        s_answered_total++;
        still->answered = true;
        still->answered_ms = now;
        still->n_answers = (uint8_t)n;
        memcpy(still->answers, answers, n * sizeof(answers[0]));
        card_fsm_set_sent(&s_card, true);
        schedule_nag(now);
    } else {
        app_toast("Not delivered");
    }
}

static void card_evt(const char *what)
{
    char evt[160];
    snprintf(evt, sizeof(evt),
             "{\"evt\":\"card\",\"op\":\"%s\",\"view\":\"%s\",\"page\":%u,\"row\":\"%s\",\"question\":%u,"
             "\"opt\":%u}",
             what, card_view_name(s_card.view), s_card.page + 1, card_row_name(card_fsm_focused_row(&s_card)),
             s_card.q + 1, s_card.opt);
    console_evt(evt);
}

/* A press on a question or plan keeps it alive while someone is reading (protocol 14.6). */
static void send_extend(const char *id, uint32_t now)
{
    char extra[48];
    snprintf(extra, sizeof(extra), ",\"id\":\"%s\"", id);
    app_link_send(now, "extend", s_tx, proto_write_extend(s_tx, sizeof(s_tx), id), extra);
}

static void on_card_out(reqq_entry_t *e, const card_out_t *o, uint32_t now)
{
    switch (o->kind) {
    case CARD_OUT_NONE:
        return;
    case CARD_OUT_PROGRESS:
        ui_tabs_hold_progress(tab_of(o->btn), o->permille > 0 ? o->permille : 1);
        return;
    case CARD_OUT_CANCEL:
        ui_tabs_hold_progress(UI_TAB_APPROVE, 0);
        return;
    case CARD_OUT_PAGE:
    case CARD_OUT_CURSOR:
        play(FB_SEQ_CLICK, 0);
        card_evt(o->kind == CARD_OUT_PAGE ? "page" : "cursor");
        return;
    case CARD_OUT_MENU_OPEN:
    case CARD_OUT_MENU_BACK:
        play(FB_SEQ_TICK, 0);
        app_btn_arm(now, o->arm_ms);
        card_evt(o->kind == CARD_OUT_MENU_OPEN ? "menu" : "back");
        return;
    case CARD_OUT_VIEW:
        play(FB_SEQ_CLICK, 0);
        app_btn_arm(now, o->arm_ms);
        card_evt("view");
        return;
    case CARD_OUT_TICK:
        play(FB_SEQ_CLICK, 0);
        card_evt("tick");
        return;
    case CARD_OUT_SHAKE:
        play(FB_SEQ_CLICK, 0);
        ui_tabs_shake(tab_of(o->btn));
        card_evt("shake");
        return;
    case CARD_OUT_NUDGE: {
        /* A click on a hold-only tab only explains the hold (design-system 5). */
        const char *text = s_card.kind == REQQ_PLAN && o->btn == BTN_APPROVE ? "Hold to approve" : NUDGE_TEXT[o->btn];
        play(FB_SEQ_CLICK, 0);
        ui_tabs_hold_progress(tab_of(o->btn), 0);
        ui_tabs_nudge(tab_of(o->btn), text);
        char evt[96];
        snprintf(evt, sizeof(evt), "{\"evt\":\"nudge\",\"btn\":\"%s\",\"text\":\"%s\"}", btn_id_name(o->btn), text);
        console_evt(evt);
        return;
    }
    case CARD_OUT_DECIDE:
        play(FB_SEQ_CLICK, 0);
        ui_tabs_hold_progress(UI_TAB_APPROVE, 0);
        send_decision(e, o->decision, now);
        return;
    }
}

static void on_approval_button(const btn_fsm_out_t *out, uint32_t now)
{
    reqq_entry_t *e = card_entry();
    if (e == NULL || strcmp(e->id, s_card_id) != 0) {
        return;
    }
    const card_out_t o = card_fsm_button(&s_card, out, now);
    if (o.extend && !e->sample) {
        send_extend(e->id, now);
    }
    /* A failed send can take the link down and empty the queue. */
    if (card_still(e)) {
        on_card_out(e, &o, now);
    }
}

static void pet_evt(pet_react_t r)
{
    char evt[48];
    snprintf(evt, sizeof(evt), "{\"evt\":\"pet\",\"action\":\"%s\"}", pet_react_name(r));
    console_evt(evt);
}

const char *app_flow_page_name(ui_pet_page_t page)
{
    switch (page) {
    case UI_PET_PAGE_ABOUT:
        return "about";
    case UI_PET_PAGE_ABOUT2:
        return "about2";
    case UI_PET_PAGE_RECENT:
        return "recent";
    case UI_PET_PAGE_PET:
    case UI_PET_PAGE_COUNT:
        break;
    }
    return "pet";
}

/* A page of the pet screen other than the face: About and recent activity. */
static bool on_about(void)
{
    return s_page != UI_PET_PAGE_PET;
}

static void set_page(ui_pet_page_t page, uint32_t now)
{
    s_page = page;
    s_page_at = now;
    char evt[48];
    snprintf(evt, sizeof(evt), "{\"evt\":\"page\",\"page\":\"%s\"}", app_flow_page_name(page));
    console_evt(evt);
}

static bool on_pet_button(const btn_fsm_out_t *out, uint32_t now)
{
    /* Any press answers the attention loop. */
    if (s_pet_attention) {
        s_pet_attention = false;
        ui_pet_react(PET_REACT_NONE);
    }
    if (s_page == UI_PET_PAGE_PET) {
        ui_pet_show_tabs(HOME_TABS_MS);
    }
    if (out->id == BTN_ACTION && out->kind == BTN_HOLD) {
        /* The quick menu (16b) opens from home only. */
        return s_page != UI_PET_PAGE_PET;
    }
    if (out->kind != BTN_CLICK) {
        return true;
    }
    if (out->id == BTN_ACTION) {
        static const ui_pet_page_t NEXT[UI_PET_PAGE_COUNT] = {
            [UI_PET_PAGE_PET] = UI_PET_PAGE_RECENT, [UI_PET_PAGE_RECENT] = UI_PET_PAGE_PET,
            [UI_PET_PAGE_ABOUT] = UI_PET_PAGE_ABOUT2, [UI_PET_PAGE_ABOUT2] = UI_PET_PAGE_ABOUT};
        set_page(NEXT[s_page], now);
    } else if (on_about()) {
        if (out->id == BTN_REJECT && out->kind == BTN_CLICK) {
            set_page(UI_PET_PAGE_PET, now);
        } else {
            s_page_at = now;
        }
    } else if (out->id == BTN_APPROVE) {
        ui_pet_react(PET_REACT_PAT);
        play(FB_SEQ_TICK, 0);
        pet_evt(PET_REACT_PAT);
    } else {
        const pet_react_t r = pet_pokes_add(&s_pokes, now) ? PET_REACT_DIZZY : PET_REACT_POKE;
        ui_pet_react(r);
        pet_evt(r);
    }
    return true;
}

/* ---- showcase pages (80-84) ---- */

/* Moods in the order of the showcase (81: Proud is 9 of 11), with the one-line meaning of the mood guide. */
static const struct {
    proto_mood_t mood;
    const char *name;
    const char *line;
} SC_MOODS[SHOWCASE_MOODS] = {
    {MOOD_IDLE, "Idle", "Between tasks"},
    {MOOD_HAPPY, "Happy", "Tests passing, builds green"},
    {MOOD_FOCUSED, "Focused", "Deep in a complex task"},
    {MOOD_THINKING, "Thinking", "Analysing, planning, reading code"},
    {MOOD_WORKING, "Working", "Editing and running things"},
    {MOOD_ANXIOUS, "Anxious", "Waiting on something long or risky"},
    {MOOD_EXCITED, "Excited", "Just shipped something"},
    {MOOD_SAD, "Sad", "Tests failing, things not working"},
    {MOOD_PROUD, "Proud", "All green after a big refactor"},
    {MOOD_ANGRY, "Angry", "Repeated failures, flaky tests"},
    {MOOD_SLEEPING, "Sleeping", "Idle for a long time"},
};

static const struct {
    pet_react_t react;
    const char *name;
    const char *line;
} SC_REACTIONS[SHOWCASE_REACTIONS] = {
    {PET_REACT_PAT, "Pat", "APPROVE on home"},
    {PET_REACT_POKE, "Poke", "REJECT on home"},
    {PET_REACT_DIZZY, "Dizzy", "Three pokes within two seconds"},
    {PET_REACT_WAKE, "Wake-up", "Leaving the clock face"},
    {PET_REACT_ATTENTION, "Attention", "An urgent notification"},
};

/* The sample cards (83, 84) carry constant content and the id `sample`, which no serialiser accepts. */
static void sample_open(bool question, uint32_t now)
{
    memset(&s_sample, 0, sizeof(s_sample));
    strlcpy(s_sample.id, "sample", sizeof(s_sample.id));
    strlcpy(s_sample.project, "devotchi", sizeof(s_sample.project));
    strlcpy(s_sample.sid, "3fa9c1", sizeof(s_sample.sid));
    s_sample.sample = true;
    s_sample.timeout_ms = 30000;
    s_sample.deadline_ms = now + s_sample.timeout_ms;
    if (question && s_sample_slot != NULL) {
        memset(s_sample_slot, 0, sizeof(*s_sample_slot));
        qstore_slot_t *q = s_sample_slot;
        q->kind = QSTORE_QUESTION;
        strlcpy(q->id, s_sample.id, sizeof(q->id));
        q->count = 1;
        q->received = 1;
        q->complete = true;
        q->timeout_ms = s_sample.timeout_ms;
        q->deadline_ms = s_sample.deadline_ms;
        q->u.q[0].has_header = true;
        strlcpy(q->u.q[0].header, "Storage", sizeof(q->u.q[0].header));
        strlcpy(q->u.q[0].text, "Where should the pets be stored?", sizeof(q->u.q[0].text));
        q->u.q[0].n_options = 3;
        strlcpy(q->u.q[0].options[0].label, "NVS partition", sizeof(q->u.q[0].options[0].label));
        strlcpy(q->u.q[0].options[0].desc, "Wear levelling, atomic replace", sizeof(q->u.q[0].options[0].desc));
        strlcpy(q->u.q[0].options[1].label, "LittleFS", sizeof(q->u.q[0].options[1].label));
        strlcpy(q->u.q[0].options[1].desc, "A file system of its own", sizeof(q->u.q[0].options[1].desc));
        strlcpy(q->u.q[0].options[2].label, "In the app image", sizeof(q->u.q[0].options[2].label));
        strlcpy(q->u.q[0].options[2].desc, "No downloads at all", sizeof(q->u.q[0].options[2].desc));
        s_sample.kind = REQQ_QUESTION;
        s_sample.slot = q;
        strlcpy(s_sample.tool, "AskUserQuestion", sizeof(s_sample.tool));
    } else {
        s_sample.kind = REQQ_PERMISSION;
        strlcpy(s_sample.tool, "Bash", sizeof(s_sample.tool));
        strlcpy(s_sample.summary, "rm -rf dist node_modules", sizeof(s_sample.summary));
        strlcpy(s_sample.cmd, "rm -rf dist node_modules", sizeof(s_sample.cmd));
        s_sample.has_cmd = true;
        strlcpy(s_sample.desc, "Clean the build output", sizeof(s_sample.desc));
        strlcpy(s_sample.reason, "Recursive delete in the project", sizeof(s_sample.reason));
        s_sample.risk = PROTO_RISK_HIGH;
    }
    s_sample_open = true;
}

static void sample_notification(const char *op)
{
    app_toast_show(UI_TOAST_POSITIVE, "Tests passed", "Sample notification", UI_TOAST_DEFAULT_MS);
    ui_pet_react(PET_REACT_POSITIVE);
    play(FB_SEQ_POSITIVE, 0);
    showcase_evt(op);
}

static void showcase_pressed(const showcase_act_t *a, uint32_t now)
{
    switch (a->kind) {
    case SHOWCASE_ACT_NONE:
        return;
    case SHOWCASE_ACT_CARD:
        return;
    case SHOWCASE_ACT_LEAVE:
        s_sample_open = false;
        s_page = UI_PET_PAGE_PET;
        ui_pet_set_caption(NULL, NULL, NULL, NULL);
        play(FB_SEQ_TICK, 0);
        showcase_evt("leave");
        return;
    case SHOWCASE_ACT_OPEN:
        if (a->page == SHOWCASE_PAGE_PERMISSION || a->page == SHOWCASE_PAGE_QUESTION) {
            sample_open(a->page == SHOWCASE_PAGE_QUESTION, now);
        } else if (a->page == SHOWCASE_PAGE_REACTIONS) {
            ui_pet_react(SC_REACTIONS[s_sc.reaction].react);
        }
        break;
    case SHOWCASE_ACT_MOVED:
        if (s_sc.page == SHOWCASE_PAGE_REACTIONS) {
            ui_pet_react(SC_REACTIONS[s_sc.reaction].react);
        }
        break;
    case SHOWCASE_ACT_PLAY:
        if (s_sc.page == SHOWCASE_PAGE_NOTIFY) {
            sample_notification("play");
            return;
        }
        ui_pet_react(s_sc.page == SHOWCASE_PAGE_MOODS ? PET_REACT_PAT : SC_REACTIONS[s_sc.reaction].react);
        break;
    case SHOWCASE_ACT_NOTIFY:
        sample_notification("notify");
        return;
    case SHOWCASE_ACT_BACK:
    case SHOWCASE_ACT_NIGHT:
        break;
    }
    play(FB_SEQ_CLICK, 0);
    showcase_evt(a->kind == SHOWCASE_ACT_OPEN ? "open" : a->kind == SHOWCASE_ACT_BACK ? "back" : "move");
}

static bool showcase_button_press(const btn_fsm_out_t *out, uint32_t now)
{
    const showcase_act_t a = showcase_button(&s_sc, out, now);
    if (a.kind == SHOWCASE_ACT_CARD) {
        on_approval_button(out, now);
        return true;
    }
    showcase_pressed(&a, now);
    return true;
}

bool app_flow_about_open(void)
{
    return s_page == UI_PET_PAGE_ABOUT || s_page == UI_PET_PAGE_ABOUT2;
}

bool app_flow_showcase_active(void)
{
    return s_sc.active;
}

bool app_flow_showcase_enter(uint32_t now)
{
    if (!showcase_enter(&s_sc, (uint8_t)reqq_count(&s_q), now)) {
        app_toast_kind(UI_TOAST_ATTENTION, "A request is waiting");
        showcase_evt("refused");
        return false;
    }
    /* showcase_enter refuses a non-empty queue: sample cards never cover a real one. */
    assert(reqq_count(&s_q) == 0);
    app_settings_close();
    s_sample_open = false;
    s_page = UI_PET_PAGE_PET;
    showcase_evt("enter");
    return true;
}

void app_flow_showcase_on_request(app_request_t *req, uint32_t now)
{
    showcase_page_t page = SHOWCASE_PAGE_MENU;
    if (strcmp(req->text, "off") == 0) {
        showcase_end(now, "leave");
        app_request_complete(true, "");
        return;
    }
    if (strcmp(req->text, "on") != 0 && !showcase_page_parse(req->text, &page)) {
        app_request_complete(false, "showcase on|off|menu|moods|reactions|permission|question|clock|notify");
        return;
    }
    if (!s_sc.active && !app_flow_showcase_enter(now)) {
        app_request_complete(false, "A request is waiting");
        return;
    }
    s_sample_open = false;
    showcase_open(&s_sc, page, now);
    if (page == SHOWCASE_PAGE_MENU) {
        s_sc.cursor = SHOWCASE_ROW_MOODS;
    } else {
        const showcase_act_t a = {
            .kind = page == SHOWCASE_PAGE_NOTIFY ? SHOWCASE_ACT_NOTIFY : SHOWCASE_ACT_OPEN, .page = page};
        showcase_pressed(&a, now);
    }
    app_request_complete(true, "");
}

/* The showcase drives the buttons unless a real request or identify is up. */
static bool showcase_owns_buttons(uint32_t now)
{
    return s_sc.active && reqq_head(&s_q) == NULL && !identify_active(now);
}

btn_mode_t app_flow_btn_mode(btn_id_t id, uint32_t now)
{
    /* The urgent banner and identify have one tab: any button. */
    if (s_attention || identify_active(now)) {
        return BTN_MODE_CLICK;
    }
    if (showcase_owns_buttons(now)) {
        /* REJECT keeps its hold everywhere in the showcase: leave. */
        if (s_sample_open && id != BTN_REJECT && s_card_id[0] != '\0') {
            return card_fsm_mode(&s_card, id, now);
        }
        return showcase_btn_mode(&s_sc, id);
    }
    if (request_screen(s_screen)) {
        return s_card_id[0] != '\0' ? card_fsm_mode(&s_card, id, now) : BTN_MODE_NONE;
    }
    if (s_clock_visible) {
        return BTN_MODE_CLICK;
    }
    /* Home: ACTION hold opens the quick menu. */
    return id == BTN_ACTION ? BTN_MODE_NAV_HOLD : BTN_MODE_CLICK;
}

bool app_flow_clock_visible(void)
{
    return s_clock_visible;
}

bool app_flow_card_visible(void)
{
    return s_card_visible && app_link_flow_shown();
}

bool app_flow_on_button(const btn_fsm_out_t *out, uint32_t now)
{
    if (out->kind == BTN_PROGRESS) {
        if (request_screen(s_screen)) {
            on_approval_button(out, now);
        }
        return true;
    }
    if (s_attention) {
        /* The press that acknowledges an urgent notification does nothing else. */
        s_attention = false;
        if (feedback_current() == FB_SEQ_ATTENTION) {
            feedback_stop();
        }
        return true;
    }
    if (identify_active(now)) {
        /* The identify screen's one tab: any button says "OK, that is this device". */
        s_identify_until = 0;
        if (feedback_current() == FB_SEQ_IDENTIFY) {
            feedback_stop();
        }
        return true;
    }
    if (showcase_owns_buttons(now)) {
        return showcase_button_press(out, now);
    }
    if (request_screen(s_screen)) {
        on_approval_button(out, now);
        return true;
    }
    if (s_clock_visible) {
        /* A waking press (app.c arms the wake guard) does nothing else. */
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
    if (s_card_id[0] != '\0' && card_entry() != NULL) {
        const card_out_t o = card_fsm_tick(&s_card, now);
        on_card_out(card_entry(), &o, now);
    }
    if (s_sample_open && reqq_head(&s_q) == NULL && due(s_sample.deadline_ms, now)) {
        sample_end("time ran out", false);
    }
    const showcase_act_t idle = showcase_tick(&s_sc, now);
    if (idle.kind == SHOWCASE_ACT_LEAVE) {
        showcase_pressed(&idle, now);
    }
    char id[PROTO_ID_SIZE];
    bool expired = false;
    while (qstore_expire(&s_store, now, id)) {
        request_evt("remove", id, "incomplete");
    }
    while (reqq_expire(&s_q, now, id)) {
        qstore_release(&s_store, id);
        request_evt("remove", id, "expired");
        expired = true;
        if (s_card_visible && strcmp(id, s_card_id) == 0) {
            static card_receipt_t r;
            card_receipt_timeout(&r);
            show_receipt(&r);
        }
    }
    if (expired) {
        queue_changed(now);
        if (reqq_count(&s_q) > 0) {
            schedule_nag(now);
        }
    }
    for (size_t i = 0; i < s_q.count; i++) {
        reqq_entry_t *e = &s_q.entries[i];
        if (e->answered && now - e->answered_ms >= CONFIRM_MS) {
            unsend(e);
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
    if (on_about() && due(after(s_page_at, PAGE_IDLE_MS), now)) {
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
    if (qstore_next_expiry(&s_store, now, &deadline)) {
        at = earliest(at, deadline, now);
    }
    for (size_t i = 0; i < s_q.count; i++) {
        if (s_q.entries[i].answered) {
            at = earliest(at, after(s_q.entries[i].answered_ms, CONFIRM_MS), now);
        }
    }
    if (request_screen(s_screen) && reqq_count(&s_q) > 0) {
        at = earliest(at, after(now, BAR_REFRESH_MS), now);
    }
    /* The tabs turn active when the arming delay ends. */
    uint32_t armed_at;
    if (s_card_visible && app_btn_arm_end(&armed_at)) {
        at = earliest(at, armed_at != 0 ? armed_at : 1, now);
    }
    if (on_about()) {
        at = earliest(at, after(s_page_at, PAGE_IDLE_MS), now);
    }
    if (s_sc.active) {
        at = earliest(at, after(s_sc.last_press_ms, SHOWCASE_IDLE_MS), now);
    }
    if (s_sample_open) {
        at = earliest(at, s_sample.deadline_ms, now);
        at = earliest(at, after(now, BAR_REFRESH_MS), now);
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

static void set_banner(const char *text, bool attention)
{
    if (text == NULL || text[0] == '\0') {
        if (s_banner_visible) {
            s_banner_visible = false;
            s_banner_shown[0] = '\0';
            ui_banner_hide();
        }
        return;
    }
    if (!s_banner_visible || attention != s_banner_attention || strcmp(s_banner_shown, text) != 0) {
        s_banner_visible = true;
        s_banner_attention = attention;
        strlcpy(s_banner_shown, text, sizeof(s_banner_shown));
        ui_banner_show(text, attention);
    }
}

void app_flow_drop_offline_banner(void)
{
    if (!s_attention) {
        set_banner(NULL, false);
    }
}

void app_flow_hide_banner(void)
{
    set_banner(NULL, false);
}

static void render_approval(const reqq_entry_t *e, const card_fsm_t *c, bool armed, uint8_t position,
                            uint8_t count, uint32_t now, app_flow_view_t *view)
{
    /* Static: a command page and its first line do not belong on the app task's stack. */
    static char wrapped[CARD_COLS * CARD_LINES * 4 + CARD_LINES + 1];
    static char page_text[sizeof(wrapped) + 2 * CARD_LINES];
    static char cmd_line[PROTO_CMD_SIZE];
    if (c->pages == 0 || !text_wrap_page(wrapped, sizeof(wrapped), e->cmd, c->cols, CARD_LINES, c->page)) {
        wrapped[0] = '\0';
    }
    /* A shell command hangs behind its "$ " prompt: every line is indented by the prompt's width. */
    const bool prompt = c->cols < CARD_COLS;
    size_t n = 0;
    for (const char *p = wrapped; *p != '\0' && n + 3 < sizeof(page_text); p++) {
        if (prompt && (p == wrapped || p[-1] == '\n')) {
            page_text[n++] = ' ';
            page_text[n++] = ' ';
        }
        page_text[n++] = *p;
    }
    page_text[n] = '\0';
    const size_t first = strcspn(e->cmd, "\n");
    memcpy(cmd_line, e->cmd, first);
    cmd_line[first] = '\0';
    const int32_t left = (int32_t)(e->deadline_ms - now);
    const size_t lines = text_wrap_lines(e->cmd, c->cols);
    const size_t seen = ((size_t)c->page + 1) * CARD_LINES;
    uint16_t permille = 0;
    if (left > 0 && e->timeout_ms > 0) {
        const uint64_t p = (uint64_t)left * 1000 / e->timeout_ms;
        permille = p > 1000 ? 1000 : (uint16_t)p;
    }
    ui_approval_t a = {
        .tool = e->tool,
        .risk = e->risk,
        .project = e->project,
        .sid = e->sid,
        /* Only `desc` is Claude's own words; `summary` is the server's. */
        .words = e->has_cmd ? e->desc : e->summary,
        .words_claude = e->has_cmd,
        .reason = e->risk != PROTO_RISK_LOW ? e->reason : "",
        .details = page_text,
        .prompt = prompt && c->page == 0,
        .position = position,
        .count = count,
        .page = (uint8_t)(c->page + 1),
        .pages = c->pages,
        .lines_after = (uint16_t)(lines > seen ? lines - seen : 0),
        .remaining_permille = permille,
        .remaining_s = (uint16_t)(left > 0 ? (left + 999) / 1000 : 0),
        .sent = e->answered,
        .armed = armed,
        .allow = card_fsm_allow(c),
        .view = c->view,
        .cmd_line = cmd_line,
        .cursor = c->cursor,
        .session = card_fsm_session(c),
        .rule = e->grant_rule,
        .rule_prefix = e->grant_prefix,
    };
    a.rows = card_fsm_rows(c, a.row);
    ui_approval_set(&a);

    view->has_request = true;
    strlcpy(view->request.id, e->id, sizeof(view->request.id));
    strlcpy(view->request.tool, e->tool, sizeof(view->request.tool));
    strlcpy(view->request.summary, e->summary, sizeof(view->request.summary));
    view->request.risk = e->risk;
    view->request.position = position;
    view->request.count = count;
    view->request.page = (uint8_t)(c->page + 1);
    view->request.pages = c->pages;
    view->request.seen = c->seen;
    view->request.deadline_ms = e->deadline_ms;
    view->request.answered = e->answered;
    view->request.armed = armed;
    view->request.cut = e->cut;
    view->request.decide = c->decide;
    view->request.menu = c->view == CARD_VIEW_MENU;
    view->request.view = c->view;
    view->request.allow = a.allow;
    view->request.session = a.session;
    view->request.rows = a.rows;
    memcpy(view->request.row, a.row, sizeof(a.row));
    view->request.cursor = c->cursor;
}

static void remaining(const reqq_entry_t *e, uint32_t now, uint16_t *permille, uint16_t *seconds)
{
    const int32_t left = (int32_t)(e->deadline_ms - now);
    *permille = 0;
    if (left > 0 && e->timeout_ms > 0) {
        const uint64_t p = (uint64_t)left * 1000 / e->timeout_ms;
        *permille = p > 1000 ? 1000 : (uint16_t)p;
    }
    *seconds = (uint16_t)(left > 0 ? (left + 999) / 1000 : 0);
}

static void view_common(const reqq_entry_t *e, const card_fsm_t *c, bool armed, app_flow_view_t *view)
{
    view->has_request = true;
    view->request.kind = e->kind;
    strlcpy(view->request.id, e->id, sizeof(view->request.id));
    strlcpy(view->request.tool, e->tool, sizeof(view->request.tool));
    view->request.position = 1;
    view->request.count = (uint8_t)reqq_count(&s_q);
    view->request.page = (uint8_t)(c->page + 1);
    view->request.pages = c->pages;
    view->request.seen = c->seen;
    view->request.deadline_ms = e->deadline_ms;
    view->request.answered = e->answered;
    view->request.armed = armed;
    view->request.cut = c->cut;
    view->request.menu = c->view == CARD_VIEW_MENU;
    view->request.view = c->view;
    view->request.allow = card_fsm_allow(c);
    view->request.session = card_fsm_session(c);
    view->request.rows = card_fsm_rows(c, view->request.row);
    view->request.cursor = c->cursor;
}

static void render_question(const reqq_entry_t *e, const card_fsm_t *c, bool armed, uint32_t now,
                            app_flow_view_t *view)
{
    /* Static: the answers in words for the review are too large for the app task's stack. */
    static char labels[PROTO_Q_MAX][24];
    static char answers[PROTO_Q_MAX][PROTO_Q_OPTIONS_MAX * PROTO_Q_LABEL_SIZE];
    const qstore_question_t *q = &e->slot->u.q[c->q];
    const bool answered = (c->answered & (1u << c->q)) != 0;
    ui_question_t u = {
        .view = c->view,
        .header = q->has_header ? q->header : "",
        .text = q->text,
        .index = (uint8_t)(c->q + 1),
        .count = c->q_count,
        .page = (uint8_t)(c->page + 1),
        .pages = c->pages,
        .read_first = c->read_pages[c->q] > 0,
        .n_options = q->n_options,
        .multi = q->multi,
        .opt = c->opt,
        .cursor = c->cursor,
        .armed = armed,
        .sent = e->answered,
    };
    remaining(e, now, &u.remaining_permille, &u.remaining_s);
    for (uint8_t i = 0; i < q->n_options && i < PROTO_Q_OPTIONS_MAX; i++) {
        const bool bit = (c->mask[c->q] & (1u << i)) != 0;
        u.labels[i] = q->options[i].label;
        u.descs[i] = q->options[i].desc;
        if (q->multi) {
            u.marks[i] = bit ? UI_OPT_TICKED : UI_OPT_BOX;
            u.ticked += bit ? 1 : 0;
        } else {
            u.marks[i] = answered && bit ? UI_OPT_CHOSEN : UI_OPT_PLAIN;
        }
    }
    if (c->view == CARD_VIEW_REVIEW) {
        u.answers = c->q_count;
        for (uint8_t i = 0; i < c->q_count; i++) {
            const qstore_question_t *qi = &e->slot->u.q[i];
            if (qi->has_header) {
                strlcpy(labels[i], qi->header, sizeof(labels[i]));
            } else {
                snprintf(labels[i], sizeof(labels[i]), "Question %u", i + 1);
            }
            const proto_answer_t a = {.mask = c->mask[i]};
            card_answer_text(answers[i], sizeof(answers[i]), qi, &a);
            u.review_label[i] = labels[i];
            u.review_answer[i] = answers[i];
        }
    }
    ui_question_set(&u);

    view_common(e, c, armed, view);
    strlcpy(view->request.summary, q->text, sizeof(view->request.summary));
    view->request.question = (uint8_t)(c->q + 1);
    view->request.questions = c->q_count;
    view->request.options = q->n_options;
    view->request.multi = q->multi;
    view->request.opt = c->opt;
    view->request.mask = c->mask[c->q];
    view->request.done = c->answered;
}

static void render_plan(const reqq_entry_t *e, const card_fsm_t *c, bool armed, uint32_t now, app_flow_view_t *view)
{
    const qstore_plan_t *p = &e->slot->u.plan;
    ui_plan_t u = {
        .view = c->view,
        .title = p->title,
        .steps = p->steps,
        .n_steps = p->n_steps,
        .page = (uint8_t)(c->page + 1),
        .pages = c->pages,
        .more = p->more,
        .cut = p->cut,
        .project = e->project,
        .approve = card_fsm_allow(c),
        .edits = card_fsm_session(c),
        .cursor = c->cursor,
        .armed = armed,
        .sent = e->answered,
    };
    remaining(e, now, &u.remaining_permille, &u.remaining_s);
    u.rows = card_fsm_rows(c, u.row);
    ui_plan_set(&u);

    view_common(e, c, armed, view);
    strlcpy(view->request.summary, p->title, sizeof(view->request.summary));
    view->request.steps = p->n_steps;
    view->request.more = p->more;
}

/* A card for the head request: opened when the head changes, armed whenever it comes on screen. */
static void show_card(const reqq_entry_t *head, uint32_t now)
{
    if (strcmp(head->id, s_card_id) != 0) {
        if (head->kind == REQQ_PERMISSION) {
            card_fsm_open(&s_card, head, (app_link_server_caps() & PROTO_CAP_DECIDE) != 0);
        } else {
            uint16_t lines[PROTO_Q_MAX] = {0};
            for (uint8_t i = 0; head->kind == REQQ_QUESTION && i < head->slot->count && i < PROTO_Q_MAX; i++) {
                lines[i] = ui_question_lines(head->slot->u.q[i].text);
            }
            card_fsm_open_ask(&s_card, head, lines, app_link_server_caps());
        }
        card_fsm_set_sent(&s_card, head->answered);
        /* Ux-flows 8: a real request in or just after the showcase needs the hold, whatever the risk. */
        s_card_guarded = !head->sample && showcase_guarded(&s_sc, now);
        card_fsm_set_hold_only(&s_card, s_card_guarded);
        strlcpy(s_card_id, head->id, sizeof(s_card_id));
        ui_tabs_hold_progress(UI_TAB_APPROVE, 0);
        if (s_card_guarded) {
            /* The request pattern that request_added() started opens with the two pulses of ux-flows 8. */
            app_btn_arm(now, SHOWCASE_ARM_MS);
        } else if (s_card_replaced) {
            app_btn_arm(now, CARD_ARM_NEXT_MS);
            play(FB_SEQ_NEXT, 0);
        } else {
            app_btn_arm(now, CARD_ARM_MS);
        }
    } else if (!s_card_visible) {
        app_btn_arm(now, s_card_guarded ? SHOWCASE_ARM_MS : CARD_ARM_MS);
    }
    s_card_replaced = false;
    s_card_visible = true;
}

static void hide_card(uint32_t now)
{
    /* A press that began on the card ends unheard: a menu hold must not open Settings on home. */
    if (s_card_visible) {
        app_btn_arm(now, 0);
    }
    s_card_visible = false;
    s_card_replaced = false;
    if (card_entry() == NULL) {
        s_card_id[0] = '\0';
        s_card_guarded = false;
    }
}

static void preview_entry(reqq_entry_t *e, bool high, uint32_t now)
{
    memset(e, 0, sizeof(*e));
    strlcpy(e->id, "0123456789abcdef0123456789abcdef", sizeof(e->id));
    strlcpy(e->tool, "Bash", sizeof(e->tool));
    strlcpy(e->project, "devotchi", sizeof(e->project));
    strlcpy(e->sid, "3fa9c1", sizeof(e->sid));
    e->has_cmd = true;
    if (high) {
        strlcpy(e->summary, "git push --force origin master", sizeof(e->summary));
        strlcpy(e->cmd, "git push --force origin master", sizeof(e->cmd));
        strlcpy(e->desc, "Update the remote branch", sizeof(e->desc));
        strlcpy(e->reason, "Force push: rewrites the remote branch", sizeof(e->reason));
        e->risk = PROTO_RISK_HIGH;
    } else {
        strlcpy(e->summary, "pnpm test --filter server", sizeof(e->summary));
        strlcpy(e->cmd, "pnpm test --filter server", sizeof(e->cmd));
        strlcpy(e->desc, "Run the server test suite", sizeof(e->desc));
        e->risk = PROTO_RISK_LOW;
    }
    e->timeout_ms = 30000;
    e->deadline_ms = now + 21000;
}

/* "5 h 02 min", "14 min", "40 s". */
static void duration_text(char *dst, size_t cap, uint32_t s)
{
    if (s >= 3600) {
        snprintf(dst, cap, "%lu h %02lu min", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60));
    } else if (s >= 60) {
        snprintf(dst, cap, "%lu min", (unsigned long)(s / 60));
    } else {
        snprintf(dst, cap, "%lu s", (unsigned long)s);
    }
}

static void render_about(uint32_t now)
{
    /* Static: app task only, and the snapshot is too large for its stack. */
    static app_snapshot_t snap;
    static char server[STORAGE_URL_SIZE];
    static char host[40];
    static char fw[40];
    static char pet[48];
    static char wifi[48];
    static char link[48];
    static char room[40];
    static char battery[40];
    static char uptime[24];
    static char heap[40];
    static char frames[40];
    static char requests[40];
    static char pets[40];
    static uint32_t last_frames;
    static uint32_t last_ms;
    char id[APP_DEVICE_ID_SIZE];
    wifi_status_t w;
    storage_url_source_t source;

    app_get_snapshot(&snap);
    wifi_get_status(&w);
    storage_get_server_url(server, &source);
    const char *url = snap.link.server[0] != '\0' ? snap.link.server : server;
    const char *scheme = strstr(url, "://");
    strlcpy(host, scheme != NULL ? scheme + 3 : url, sizeof(host));
    app_get_device_id(id);
    char sha[8];
    esp_app_get_elf_sha256(sha, sizeof(sha));
    snprintf(fw, sizeof(fw), "%.24s \xC2\xB7 %s", esp_app_get_description()->version, sha);
    /* Static: the view holds the stored pets' list. */
    static app_pets_view_t pv;
    app_pets_get_view(&pv);
    snprintf(pet, sizeof(pet), "%s%s", pv.shown, pv.source == PET_SOURCE_STORED ? "" : " (built in)");
    if (pet[0] >= 'a' && pet[0] <= 'z') {
        pet[0] = (char)(pet[0] - 'a' + 'A');
    }
    if (w.connected) {
        snprintf(wifi, sizeof(wifi), "%.24s %d dBm", snap.link.ssid, w.rssi);
    } else {
        snprintf(wifi, sizeof(wifi), "%s", snap.link.ssid[0] != '\0' ? snap.link.ssid : "-");
    }
    snprintf(link, sizeof(link), "%s (%s)", app_state_name(snap.state), app_ws_state_name(snap.link.ws));
    snprintf(room, sizeof(room), "%.20s \xC2\xB7 mode %s", snap.link.name[0] != '\0' ? snap.link.name : id,
             proto_mode_name(app_link_mode()));
    power_battery_t b;
    power_get_battery(&b);
    if (b.valid && b.state != BATTERY_STATE_ABSENT) {
        snprintf(battery, sizeof(battery), "%ld.%02ld V %d %% %s", (long)(b.mv / 1000), (long)(b.mv % 1000 / 10),
                 b.pct, battery_state_name(b.state));
    } else {
        snprintf(battery, sizeof(battery), "none \xC2\xB7 %s", b.ext_power ? "USB power" : "no power");
    }
    duration_text(uptime, sizeof(uptime), app_uptime_s());
    snprintf(heap, sizeof(heap), "%u kB free \xC2\xB7 min %u",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024));
    ui_stats_t st;
    ui_get_stats(&st);
    const uint32_t dt = now - last_ms;
    const uint32_t fps = last_ms != 0 && dt > 0 ? (st.frames - last_frames) * 1000u / dt : 0;
    last_frames = st.frames;
    last_ms = now;
    snprintf(frames, sizeof(frames), "%lu fps \xC2\xB7 %lu.%lu ms draw", (unsigned long)fps,
             (unsigned long)(st.avg_ms_x10 / 10), (unsigned long)(st.avg_ms_x10 % 10));
    snprintf(requests, sizeof(requests), "%lu held \xC2\xB7 %lu answered", (unsigned long)s_held_total,
             (unsigned long)s_answered_total);
    if (pv.partition == PET_PART_OK) {
        snprintf(pets, sizeof(pets), "%u of %u stored", (unsigned)pv.stored, (unsigned)pv.slots);
    } else {
        snprintf(pets, sizeof(pets), "partition %s", pet_part_state_name(pv.partition));
    }
    const char *error = snap.link.last_error[0] != '\0' ? snap.link.last_error
                        : storage_is_degraded()       ? "storage: defaults in use"
                                                      : "-";
    const char *values[UI_ABOUT_COUNT] = {
        [UI_ABOUT_FW] = fw,
        [UI_ABOUT_DEVICE] = snap.link.name[0] != '\0' ? snap.link.name : id,
        [UI_ABOUT_PET] = pet,
        [UI_ABOUT_WIFI] = wifi,
        [UI_ABOUT_IP] = w.connected ? w.ip : "-",
        [UI_ABOUT_SERVER] = host,
        [UI_ABOUT_LINK] = link,
        [UI_ABOUT_ROOM] = room,
        [UI_ABOUT_BATTERY] = battery,
        [UI_ABOUT_UPTIME] = uptime,
        [UI_ABOUT_RESET] = app_reset_reason(),
        [UI_ABOUT_HEAP] = heap,
        [UI_ABOUT_FRAMES] = frames,
        [UI_ABOUT_REQUESTS] = requests,
        [UI_ABOUT_PETS] = pets,
        [UI_ABOUT_ERROR] = error,
        [UI_ABOUT_FONTS] = "Figtree, JB Mono (OFL)",
    };
    ui_about_set(values);
}

/* Recent activity: the time of day when the clock is known, otherwise how long ago. */
static void render_recent(uint32_t now)
{
    static char times[UI_RECENT_ROWS][CLOCK_TIME_SIZE + 8];
    const char *time_p[UI_RECENT_ROWS] = {0};
    const char *text_p[UI_RECENT_ROWS] = {0};
    uint8_t n = 0;
    for (; n < UI_RECENT_ROWS; n++) {
        const pet_recent_t *r = pet_model_recent(&s_pet, n);
        if (r == NULL) {
            break;
        }
        char date[CLOCK_DATE_SIZE];
        if (!app_clock_format(r->epoch_ms, times[n], date)) {
            duration_text(times[n], sizeof(times[n]), (now - r->at_ms) / 1000);
        }
        time_p[n] = times[n];
        text_p[n] = r->text;
    }
    ui_recent_set(time_p, text_p, n);
}

static void render_clock(bool night, bool sample)
{
    char time[CLOCK_TIME_SIZE];
    char date[CLOCK_DATE_SIZE];
    /* The showcase's clock page shows a sample time when the device has none (80). */
    if (!app_clock_format(app_clock_epoch_ms(), time, date) && sample) {
        strlcpy(time, "14:32", sizeof(time));
        strlcpy(date, "Sun 15 Nov", sizeof(date));
    }
    const ui_clock_t c = {.time = time, .date = date, .night = night};
    ui_clock_set(&c);
}

/* The showcase menu (80) on the list screen. */
static void render_showcase_menu(void)
{
    static ui_settings_t ui;
    static const char *const ICONS[SHOWCASE_ROW_COUNT] = {UI_ICON_PET,  UI_ICON_PLAY,  UI_ICON_SHIELD, UI_ICON_Q,
                                                          UI_ICON_BELL, UI_ICON_CLOCK, UI_ICON_X};
    static const char *const LABELS[SHOWCASE_ROW_COUNT] = {
        "Moods", "Reactions", "Sample permission", "Sample question", "Sample notification", "Clock face",
        "Leave showcase"};
    memset(&ui, 0, sizeof(ui));
    ui.title = "Showcase";
    ui.count = SHOWCASE_ROW_COUNT;
    ui.cursor = (uint8_t)s_sc.cursor;
    ui.approve = "Open";
    for (int i = 0; i < SHOWCASE_ROW_COUNT; i++) {
        ui.icons[i] = ICONS[i];
        ui.labels[i] = LABELS[i];
    }
    snprintf(ui.values[SHOWCASE_ROW_MOODS], sizeof(ui.values[0]), "%d", SHOWCASE_MOODS);
    snprintf(ui.values[SHOWCASE_ROW_REACTIONS], sizeof(ui.values[0]), "%d", SHOWCASE_REACTIONS);
    ui_settings_set(&ui);
}

/* A showcase page that is not a sample card. */
static ui_screen_id_t render_showcase(void)
{
    static char bar[24];
    switch (s_sc.page) {
    case SHOWCASE_PAGE_MOODS:
        snprintf(bar, sizeof(bar), "Moods %u/%d", s_sc.mood + 1u, SHOWCASE_MOODS);
        ui_pet_set("", SC_MOODS[s_sc.mood].mood, "");
        ui_pet_set_caption(bar, SC_MOODS[s_sc.mood].name, SC_MOODS[s_sc.mood].line, "Pat");
        ui_pet_page_set(UI_PET_PAGE_PET);
        return UI_SCREEN_PET;
    case SHOWCASE_PAGE_REACTIONS:
        ui_pet_set("", MOOD_IDLE, "");
        ui_pet_set_caption("Reactions", SC_REACTIONS[s_sc.reaction].name, SC_REACTIONS[s_sc.reaction].line,
                           "Replay");
        ui_pet_page_set(UI_PET_PAGE_PET);
        return UI_SCREEN_PET;
    case SHOWCASE_PAGE_NOTIFY:
        ui_pet_set("", MOOD_HAPPY, "");
        ui_pet_set_caption("Notification", "Sample notification", "A toast and the pet's reaction", "Replay");
        ui_pet_page_set(UI_PET_PAGE_PET);
        return UI_SCREEN_PET;
    case SHOWCASE_PAGE_CLOCK:
        render_clock(s_sc.night, true);
        return UI_SCREEN_CLOCK;
    case SHOWCASE_PAGE_MENU:
    case SHOWCASE_PAGE_PERMISSION:
    case SHOWCASE_PAGE_QUESTION:
    case SHOWCASE_PAGE_COUNT:
        break;
    }
    render_showcase_menu();
    return UI_SCREEN_SETTINGS;
}

ui_screen_id_t app_flow_render(uint32_t now, const char *offline_banner, app_flow_preview_t preview)
{
    /* Static: the view is larger than the app task's stack allows for one buffer. */
    static app_flow_view_t view;
    memset(&view, 0, sizeof(view));
    view.mood = s_pet.mood;
    ui_screen_id_t screen;
    if (preview != APP_FLOW_PREVIEW_OFF) {
        set_banner(NULL, false);
        hide_card(now);
        if (preview == APP_FLOW_PREVIEW_IDENTIFY) {
            ui_identify_set(app_link_name());
            screen = UI_SCREEN_IDENTIFY;
        } else {
            static reqq_entry_t sample;
            static card_fsm_t card;
            preview_entry(&sample, preview == APP_FLOW_PREVIEW_APPROVAL_HIGH, now);
            card_fsm_open(&card, &sample, true);
            render_approval(&sample, &card, true, 1, preview == APP_FLOW_PREVIEW_APPROVAL_HIGH ? 3 : 1, now, &view);
            screen = UI_SCREEN_APPROVAL;
        }
        s_screen = screen;
        publish_view(&view);
        return screen;
    }

    const bool identify = identify_active(now);
    const reqq_entry_t *head = card_entry();
    const bool showcase = s_sc.active && reqq_head(&s_q) == NULL;
    if (identify || (head != NULL && !head->sample)) {
        s_page = UI_PET_PAGE_PET;
    }
    const bool clock = !identify && head == NULL && !showcase &&
                       (app_clock_show() != APP_CLOCK_SHOW_AUTO || app_power_clock_shown());
    /* Identify is full-screen: the name must not sit under a banner; the About page shows the link itself. */
    const bool about = s_page == UI_PET_PAGE_ABOUT || s_page == UI_PET_PAGE_ABOUT2;
    set_banner(identify || clock ? NULL : s_attention ? s_attention_text : about ? NULL : offline_banner,
               !identify && !clock && s_attention);
    ui_mark_t mark = UI_MARK_NONE;
    s_clock_visible = false;
    if (identify) {
        hide_card(now);
        screen = UI_SCREEN_IDENTIFY;
    } else if (head != NULL) {
        show_card(head, now);
        const bool armed = app_btn_armed(now);
        mark = head->sample ? UI_MARK_SAMPLE : s_card_guarded ? UI_MARK_REAL : UI_MARK_NONE;
        switch (head->kind) {
        case REQQ_QUESTION:
            render_question(head, &s_card, armed, now, &view);
            screen = UI_SCREEN_QUESTION;
            break;
        case REQQ_PLAN:
            render_plan(head, &s_card, armed, now, &view);
            screen = UI_SCREEN_PLAN;
            break;
        case REQQ_PERMISSION:
        default:
            render_approval(head, &s_card, armed, 1, head->sample ? 1 : (uint8_t)reqq_count(&s_q), now, &view);
            screen = UI_SCREEN_APPROVAL;
            break;
        }
    } else if (showcase) {
        hide_card(now);
        mark = UI_MARK_DEMO;
        screen = render_showcase();
    } else if (clock) {
        hide_card(now);
        ui_pet_set_caption(NULL, NULL, NULL, NULL);
        render_clock(app_clock_night(), false);
        s_clock_visible = true;
        screen = UI_SCREEN_CLOCK;
    } else {
        hide_card(now);
        ui_pet_set_caption(NULL, NULL, NULL, NULL);
        ui_pet_set(s_pet.project, s_pet.mood, s_pet.status);
        if (about) {
            render_about(now);
        } else if (s_page == UI_PET_PAGE_RECENT) {
            render_recent(now);
        }
        ui_pet_page_set(s_page);
        screen = UI_SCREEN_PET;
    }
    ui_mark_set(mark);
    view.page = s_page;
    s_screen = screen;
    publish_view(&view);
    return screen;
}

esp_err_t app_flow_init(void)
{
    qstore_slot_t *slots = heap_caps_calloc(QSTORE_SLOTS, sizeof(qstore_slot_t), MALLOC_CAP_SPIRAM);
    if (slots == NULL) {
        slots = calloc(QSTORE_SLOTS, sizeof(qstore_slot_t));
    }
    ESP_RETURN_ON_FALSE(slots != NULL, ESP_ERR_NO_MEM, TAG, "question store");
    qstore_init(&s_store, slots);
    s_sample_slot = heap_caps_calloc(1, sizeof(qstore_slot_t), MALLOC_CAP_SPIRAM);
    if (s_sample_slot == NULL) {
        s_sample_slot = calloc(1, sizeof(qstore_slot_t));
    }
    showcase_init(&s_sc);
    reqq_init(&s_q);
    pet_model_init(&s_pet);
    pet_pokes_init(&s_pokes);
    app_get_device_id(s_device_id);
    s_view_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_view_lock != NULL, ESP_ERR_NO_MEM, TAG, "lock");
    const esp_timer_create_args_t timer = {.callback = on_timer_cb, .name = "flow"};
    return esp_timer_create(&timer, &s_timer);
}
