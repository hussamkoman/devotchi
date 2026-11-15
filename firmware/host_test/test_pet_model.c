#include "pet_model.h"

#include "unity.h"

#include <stdio.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static pet_model_t m;

static proto_status_t status(const char *msg, bool has_mood, proto_mood_t mood, const char *project)
{
    proto_status_t s = {0};
    snprintf(s.msg, sizeof(s.msg), "%s", msg);
    s.has_mood = has_mood;
    s.mood = mood;
    snprintf(s.project, sizeof(s.project), "%s", project);
    return s;
}

static proto_notify_t notify(const char *event, proto_mood_t mood, const char *detail, const char *source)
{
    proto_notify_t n = {0};
    snprintf(n.event, sizeof(n.event), "%s", event);
    n.mood = mood;
    snprintf(n.detail, sizeof(n.detail), "%s", detail);
    snprintf(n.source, sizeof(n.source), "%s", source);
    n.urgency = 1;
    return n;
}

static void test_init(void)
{
    pet_model_init(&m);
    TEST_ASSERT_EQUAL(MOOD_IDLE, m.mood);
    TEST_ASSERT_EQUAL_STRING("", m.status);
    TEST_ASSERT_EQUAL_STRING("", m.project);
}

static void test_status_without_mood_keeps_mood_of_last_mood_message(void)
{
    pet_model_init(&m);
    const proto_mood_msg_t happy = {.mood = MOOD_HAPPY};
    pet_model_apply_mood(&m, &happy);
    TEST_ASSERT_EQUAL(MOOD_HAPPY, m.mood);

    const proto_status_t s1 = status("Edit src/app.c", false, MOOD_IDLE, "devotchi");
    pet_model_apply_status(&m, &s1);
    TEST_ASSERT_EQUAL(MOOD_HAPPY, m.mood);
    TEST_ASSERT_EQUAL_STRING("Edit src/app.c", m.status);
    TEST_ASSERT_EQUAL_STRING("devotchi", m.project);

    const proto_status_t s2 = status("Run tests", true, MOOD_WORKING, "");
    pet_model_apply_status(&m, &s2);
    TEST_ASSERT_EQUAL(MOOD_WORKING, m.mood);
    TEST_ASSERT_EQUAL_STRING("Run tests", m.status);
    /* A status without a project keeps the label of the session that set it. */
    TEST_ASSERT_EQUAL_STRING("devotchi", m.project);

    const proto_status_t s3 = status("Read docs", false, MOOD_SAD, "other");
    pet_model_apply_status(&m, &s3);
    TEST_ASSERT_EQUAL(MOOD_WORKING, m.mood);
    TEST_ASSERT_EQUAL_STRING("other", m.project);
}

static void test_notify_sets_mood_and_last_event(void)
{
    pet_model_init(&m);
    const proto_notify_t n = notify("tests_failed", MOOD_SAD, "3 of 47 failing", "");
    TEST_ASSERT_EQUAL(PET_EVENT_NEGATIVE, pet_model_apply_notify(&m, &n));
    TEST_ASSERT_EQUAL(MOOD_SAD, m.mood);
    TEST_ASSERT_EQUAL_STRING("tests_failed", m.last_event);
    TEST_ASSERT_EQUAL_STRING("3 of 47 failing", m.last_detail);

    const proto_status_t s = status("Fixing", false, MOOD_IDLE, "");
    pet_model_apply_status(&m, &s);
    TEST_ASSERT_EQUAL(MOOD_SAD, m.mood);
}

static void test_text_is_sanitised(void)
{
    pet_model_init(&m);
    const proto_status_t s = status("caf\xc3\xa9\tok", false, MOOD_IDLE, "pr\xc3\xb8j");
    pet_model_apply_status(&m, &s);
    TEST_ASSERT_EQUAL_STRING("caf??ok", m.status);
    TEST_ASSERT_EQUAL_STRING("pr?j", m.project);
}

typedef struct {
    const char *event;
    proto_mood_t mood;
    pet_event_class_t cls;
} class_case_t;

static void test_event_class_table(void)
{
    static const class_case_t cases[] = {
        {"tests_passed", MOOD_SAD, PET_EVENT_POSITIVE},
        {"build_succeeded", MOOD_IDLE, PET_EVENT_POSITIVE},
        {"deploy_complete", MOOD_IDLE, PET_EVENT_POSITIVE},
        {"lint_succeeded", MOOD_SAD, PET_EVENT_POSITIVE},
        {"e2e_passed", MOOD_ANGRY, PET_EVENT_POSITIVE},
        {"tests_failed", MOOD_HAPPY, PET_EVENT_NEGATIVE},
        {"build_failed", MOOD_HAPPY, PET_EVENT_NEGATIVE},
        {"deploy_failed", MOOD_IDLE, PET_EVENT_NEGATIVE},
        {"error", MOOD_HAPPY, PET_EVENT_NEGATIVE},
        {"lint_failed", MOOD_PROUD, PET_EVENT_NEGATIVE},
        {"task_complete", MOOD_SAD, PET_EVENT_COMPLETE},
        {"task_paused", MOOD_HAPPY, PET_EVENT_NONE},
        {"needs_input", MOOD_HAPPY, PET_EVENT_ATTENTION},
        /* A bare suffix is not an event with that suffix. */
        {"_failed", MOOD_HAPPY, PET_EVENT_POSITIVE},
        {"failed", MOOD_IDLE, PET_EVENT_NONE},
        {"custom", MOOD_HAPPY, PET_EVENT_POSITIVE},
        {"custom", MOOD_EXCITED, PET_EVENT_POSITIVE},
        {"custom", MOOD_PROUD, PET_EVENT_POSITIVE},
        {"custom", MOOD_SAD, PET_EVENT_NEGATIVE},
        {"custom", MOOD_ANGRY, PET_EVENT_NEGATIVE},
        {"custom", MOOD_ANXIOUS, PET_EVENT_NONE},
        {"custom", MOOD_SLEEPING, PET_EVENT_NONE},
        {"custom", MOOD_IDLE, PET_EVENT_NONE},
        {"custom", MOOD_FOCUSED, PET_EVENT_NONE},
        {"custom", MOOD_WORKING, PET_EVENT_NONE},
        {"custom", MOOD_THINKING, PET_EVENT_NONE},
        {"", MOOD_IDLE, PET_EVENT_NONE},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TEST_ASSERT_EQUAL_MESSAGE(cases[i].cls, pet_event_class(cases[i].event, cases[i].mood), cases[i].event);
    }
    TEST_ASSERT_EQUAL(PET_EVENT_NONE, pet_event_class(NULL, MOOD_IDLE));
    TEST_ASSERT_EQUAL_STRING("attention", pet_event_class_name(PET_EVENT_ATTENTION));
    TEST_ASSERT_EQUAL_STRING("none", pet_event_class_name(PET_EVENT_NONE));
}

static void test_notify_text(void)
{
    char out[96];
    proto_notify_t n = notify("tests_failed", MOOD_SAD, "3 of 47 failing", "");
    pet_notify_text(out, sizeof(out), &n);
    TEST_ASSERT_EQUAL_STRING("tests failed: 3 of 47 failing", out);

    n = notify("deploy_complete", MOOD_HAPPY, "", "ci");
    pet_notify_text(out, sizeof(out), &n);
    TEST_ASSERT_EQUAL_STRING("ci: deploy complete", out);

    n = notify("task_complete", MOOD_HAPPY, "caf\xc3\xa9", "my_job");
    pet_notify_text(out, sizeof(out), &n);
    /* Only the event's underscores become spaces. */
    TEST_ASSERT_EQUAL_STRING("my_job: task complete: caf?", out);

    char small[8];
    n = notify("tests_failed", MOOD_SAD, "x", "");
    TEST_ASSERT_EQUAL(7, pet_notify_text(small, sizeof(small), &n));
    TEST_ASSERT_EQUAL_STRING("tests f", small);
}

static void test_recent_ring(void)
{
    pet_model_t m;
    pet_model_init(&m);
    TEST_ASSERT_NULL(pet_model_recent(&m, 0));
    pet_model_add_recent(&m, "", 1, 0);
    TEST_ASSERT_NULL(pet_model_recent(&m, 0));
    char text[16];
    for (int i = 0; i < 7; i++) {
        snprintf(text, sizeof(text), "line %d", i);
        pet_model_add_recent(&m, text, (uint32_t)i * 10, i);
    }
    pet_model_add_recent(&m, "line 6", 99, 99);
    TEST_ASSERT_EQUAL(PET_RECENT_MAX, m.recent_count);
    TEST_ASSERT_EQUAL_STRING("line 6", pet_model_recent(&m, 0)->text);
    TEST_ASSERT_EQUAL_UINT32(60, pet_model_recent(&m, 0)->at_ms);
    TEST_ASSERT_EQUAL_STRING("line 2", pet_model_recent(&m, 4)->text);
    TEST_ASSERT_NULL(pet_model_recent(&m, 5));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init);
    RUN_TEST(test_status_without_mood_keeps_mood_of_last_mood_message);
    RUN_TEST(test_notify_sets_mood_and_last_event);
    RUN_TEST(test_text_is_sanitised);
    RUN_TEST(test_event_class_table);
    RUN_TEST(test_notify_text);
    RUN_TEST(test_recent_ring);
    return UNITY_END();
}
