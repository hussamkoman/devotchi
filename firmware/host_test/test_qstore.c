#include "qstore.h"

#include "unity.h"

#include <stdio.h>
#include <string.h>

#define ID_A  "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define ID_B  "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define ID_C  "cccccccccccccccccccccccccccccccc"

static qstore_slot_t s_slots[QSTORE_SLOTS];
static qstore_t s_store;
static proto_question_t s_q;
static proto_plan_t s_p;

void setUp(void)
{
    memset(s_slots, 0x5A, sizeof(s_slots));
    qstore_init(&s_store, s_slots);
}

void tearDown(void) {}

static const proto_question_t *question(const char *id, uint8_t index, uint8_t count)
{
    memset(&s_q, 0, sizeof(s_q));
    snprintf(s_q.id, sizeof(s_q.id), "%s", id);
    s_q.index = index;
    s_q.count = count;
    snprintf(s_q.text, sizeof(s_q.text), "Question %u?", index);
    s_q.n_options = 2;
    snprintf(s_q.options[0].label, sizeof(s_q.options[0].label), "Yes %u", index);
    snprintf(s_q.options[1].label, sizeof(s_q.options[1].label), "No %u", index);
    snprintf(s_q.project, sizeof(s_q.project), "devotchi");
    s_q.timeout_ms = 30000;
    return &s_q;
}

static const proto_plan_t *plan(const char *id)
{
    memset(&s_p, 0, sizeof(s_p));
    snprintf(s_p.id, sizeof(s_p.id), "%s", id);
    snprintf(s_p.title, sizeof(s_p.title), "Add pet packs");
    s_p.n_steps = 2;
    snprintf(s_p.steps[0], sizeof(s_p.steps[0]), "Define the pack format");
    snprintf(s_p.steps[1], sizeof(s_p.steps[1]), "Port Devo");
    s_p.timeout_ms = 30000;
    return &s_p;
}

static void test_starts_empty(void)
{
    TEST_ASSERT_EQUAL_size_t(0, qstore_used(&s_store));
    TEST_ASSERT_NULL(qstore_find(&s_store, ID_A));
    uint32_t at;
    TEST_ASSERT_FALSE(qstore_next_expiry(&s_store, 0, &at));
}

static void test_single_question_is_complete_at_once(void)
{
    TEST_ASSERT_EQUAL(QSTORE_COMPLETE, qstore_add_question(&s_store, question(ID_A, 0, 1), 100));
    const qstore_slot_t *slot = qstore_find(&s_store, ID_A);
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_EQUAL(QSTORE_QUESTION, slot->kind);
    TEST_ASSERT_TRUE(slot->complete);
    TEST_ASSERT_EQUAL_STRING("Question 0?", slot->u.q[0].text);
    TEST_ASSERT_EQUAL_STRING("No 0", slot->u.q[0].options[1].label);
    TEST_ASSERT_EQUAL_UINT32(30100, slot->deadline_ms);
}

static void test_frames_in_any_order_and_replays(void)
{
    TEST_ASSERT_EQUAL(QSTORE_PARTIAL, qstore_add_question(&s_store, question(ID_A, 2, 3), 0));
    TEST_ASSERT_EQUAL(QSTORE_DUPLICATE, qstore_add_question(&s_store, question(ID_A, 2, 3), 10));
    TEST_ASSERT_EQUAL(QSTORE_PARTIAL, qstore_add_question(&s_store, question(ID_A, 0, 3), 20));
    TEST_ASSERT_EQUAL_size_t(1, qstore_used(&s_store));
    uint32_t at;
    TEST_ASSERT_TRUE(qstore_next_expiry(&s_store, 20, &at));
    TEST_ASSERT_EQUAL_UINT32(QSTORE_ASSEMBLE_MS, at);
    TEST_ASSERT_EQUAL(QSTORE_COMPLETE, qstore_add_question(&s_store, question(ID_A, 1, 3), 30));
    TEST_ASSERT_FALSE(qstore_next_expiry(&s_store, 30, &at));
    /* A replay of a complete call changes nothing. */
    TEST_ASSERT_EQUAL(QSTORE_DUPLICATE, qstore_add_question(&s_store, question(ID_A, 1, 3), 40));
    const qstore_slot_t *slot = qstore_find(&s_store, ID_A);
    TEST_ASSERT_EQUAL_STRING("Question 1?", slot->u.q[1].text);
    TEST_ASSERT_EQUAL_STRING("Question 2?", slot->u.q[2].text);
    TEST_ASSERT_EQUAL_UINT32(30030, slot->deadline_ms);
}

static void test_count_mismatch_is_dropped(void)
{
    qstore_add_question(&s_store, question(ID_A, 0, 2), 0);
    TEST_ASSERT_EQUAL(QSTORE_MISMATCH, qstore_add_question(&s_store, question(ID_A, 1, 3), 0));
    TEST_ASSERT_EQUAL(QSTORE_MISMATCH, qstore_add_plan(&s_store, plan(ID_A), 0));
    TEST_ASSERT_EQUAL(QSTORE_MISMATCH, qstore_add_question(&s_store, question(ID_B, 3, 2), 0));
}

static void test_two_slots_then_full(void)
{
    TEST_ASSERT_EQUAL(QSTORE_COMPLETE, qstore_add_question(&s_store, question(ID_A, 0, 1), 0));
    TEST_ASSERT_EQUAL(QSTORE_COMPLETE, qstore_add_plan(&s_store, plan(ID_B), 0));
    TEST_ASSERT_EQUAL(QSTORE_FULL, qstore_add_question(&s_store, question(ID_C, 0, 2), 0));
    TEST_ASSERT_EQUAL(QSTORE_FULL, qstore_add_plan(&s_store, plan(ID_C), 0));
    TEST_ASSERT_NULL(qstore_find(&s_store, ID_C));
    TEST_ASSERT_TRUE(qstore_release(&s_store, ID_A));
    TEST_ASSERT_FALSE(qstore_release(&s_store, ID_A));
    TEST_ASSERT_EQUAL(QSTORE_PARTIAL, qstore_add_question(&s_store, question(ID_C, 0, 2), 0));
    TEST_ASSERT_EQUAL_STRING("Add pet packs", qstore_find(&s_store, ID_B)->u.plan.title);
}

static void test_incomplete_expires_after_assembly_time(void)
{
    qstore_add_question(&s_store, question(ID_A, 0, 2), 1000);
    qstore_add_question(&s_store, question(ID_B, 0, 1), 1000);
    char id[PROTO_ID_SIZE];
    TEST_ASSERT_FALSE(qstore_expire(&s_store, 1000 + QSTORE_ASSEMBLE_MS - 1, id));
    TEST_ASSERT_TRUE(qstore_expire(&s_store, 1000 + QSTORE_ASSEMBLE_MS, id));
    TEST_ASSERT_EQUAL_STRING(ID_A, id);
    TEST_ASSERT_FALSE(qstore_expire(&s_store, 99999, id));
    TEST_ASSERT_NOT_NULL(qstore_find(&s_store, ID_B));
    TEST_ASSERT_EQUAL_size_t(1, qstore_used(&s_store));
}

static void test_expiry_across_clock_wrap(void)
{
    qstore_add_question(&s_store, question(ID_A, 0, 2), UINT32_MAX - 100);
    char id[PROTO_ID_SIZE];
    TEST_ASSERT_FALSE(qstore_expire(&s_store, 50, id));
    TEST_ASSERT_TRUE(qstore_expire(&s_store, QSTORE_ASSEMBLE_MS, id));
}

static void test_text_is_device_text(void)
{
    question(ID_A, 0, 1);
    snprintf(s_q.text, sizeof(s_q.text), "D\xC3\xA9j\xC3\xA0 vu \xE2\x80\x94 na\xC3\xAFve caf\xC3\xA9? \xF0\x9F\x98\x80");
    s_q.has_header = true;
    snprintf(s_q.header, sizeof(s_q.header), "Caf\xC3\xA9");
    qstore_add_question(&s_store, &s_q, 0);
    const qstore_slot_t *slot = qstore_find(&s_store, ID_A);
    TEST_ASSERT_EQUAL_STRING("D\xC3\xA9j\xC3\xA0 vu \xE2\x80\x94 na\xC3\xAFve caf\xC3\xA9? ?", slot->u.q[0].text);
    TEST_ASSERT_EQUAL_STRING("Caf\xC3\xA9", slot->u.q[0].header);
    TEST_ASSERT_TRUE(slot->u.q[0].has_header);
}

static void test_plan_cut_follows_more(void)
{
    plan(ID_A);
    s_p.more = 3;
    qstore_add_plan(&s_store, &s_p, 0);
    TEST_ASSERT_TRUE(qstore_find(&s_store, ID_A)->u.plan.cut);
    TEST_ASSERT_EQUAL_UINT16(3, qstore_find(&s_store, ID_A)->u.plan.more);
    TEST_ASSERT_EQUAL(QSTORE_DUPLICATE, qstore_add_plan(&s_store, plan(ID_A), 0));
}

static void test_clear_and_names(void)
{
    qstore_add_plan(&s_store, plan(ID_A), 0);
    qstore_clear(&s_store);
    TEST_ASSERT_EQUAL_size_t(0, qstore_used(&s_store));
    TEST_ASSERT_EQUAL_STRING("complete", qstore_rc_name(QSTORE_COMPLETE));
    TEST_ASSERT_EQUAL_STRING("full", qstore_rc_name(QSTORE_FULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_starts_empty);
    RUN_TEST(test_single_question_is_complete_at_once);
    RUN_TEST(test_frames_in_any_order_and_replays);
    RUN_TEST(test_count_mismatch_is_dropped);
    RUN_TEST(test_two_slots_then_full);
    RUN_TEST(test_incomplete_expires_after_assembly_time);
    RUN_TEST(test_expiry_across_clock_wrap);
    RUN_TEST(test_text_is_device_text);
    RUN_TEST(test_plan_cut_follows_more);
    RUN_TEST(test_clear_and_names);
    return UNITY_END();
}
