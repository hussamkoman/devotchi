#include "reqq.h"

#include "unity.h"

#include <stdio.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static reqq_t q;

static proto_permission_t perm(int n, uint32_t timeout_ms)
{
    proto_permission_t p = {0};
    snprintf(p.id, sizeof(p.id), "%032x", n);
    snprintf(p.tool, sizeof(p.tool), "Bash");
    snprintf(p.summary, sizeof(p.summary), "summary %d", n);
    snprintf(p.details, sizeof(p.details), "line one\nline two");
    snprintf(p.project, sizeof(p.project), "devotchi");
    snprintf(p.sid, sizeof(p.sid), "3fa9c1");
    p.risk = PROTO_RISK_MED;
    p.timeout_ms = timeout_ms;
    return p;
}

static const char *id_of(int n)
{
    static char id[PROTO_ID_SIZE];
    snprintf(id, sizeof(id), "%032x", n);
    return id;
}

static void test_add_copies_fields_and_sets_deadline(void)
{
    reqq_init(&q);
    TEST_ASSERT_NULL(reqq_head(&q));
    const proto_permission_t p = perm(1, 30000);
    TEST_ASSERT_EQUAL(REQQ_ADDED, reqq_add(&q, &p, 1000));
    TEST_ASSERT_EQUAL(1, reqq_count(&q));
    const reqq_entry_t *e = reqq_head(&q);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_STRING(id_of(1), e->id);
    TEST_ASSERT_EQUAL_STRING("Bash", e->tool);
    TEST_ASSERT_EQUAL_STRING("summary 1", e->summary);
    TEST_ASSERT_EQUAL_STRING("line one\nline two", e->cmd);
    TEST_ASSERT_FALSE(e->has_cmd);
    TEST_ASSERT_EQUAL_STRING("devotchi", e->project);
    TEST_ASSERT_EQUAL_STRING("3fa9c1", e->sid);
    TEST_ASSERT_EQUAL(PROTO_RISK_MED, e->risk);
    TEST_ASSERT_EQUAL_UINT32(31000, e->deadline_ms);
    TEST_ASSERT_EQUAL_UINT32(30000, e->timeout_ms);
    TEST_ASSERT_FALSE(e->acked);
    TEST_ASSERT_FALSE(e->answered);
}

static void test_add_copies_card_fields(void)
{
    reqq_init(&q);
    proto_permission_t p = perm(1, 30000);
    p.has_cmd = true;
    snprintf(p.cmd, sizeof(p.cmd), "pnpm test --filter server");
    snprintf(p.desc, sizeof(p.desc), "Run the tests");
    snprintf(p.reason, sizeof(p.reason), "Force push");
    p.has_grant = true;
    p.grant_prefix = true;
    snprintf(p.grant_rule, sizeof(p.grant_rule), "pnpm test");
    TEST_ASSERT_EQUAL(REQQ_ADDED, reqq_add(&q, &p, 0));
    const reqq_entry_t *e = reqq_head(&q);
    TEST_ASSERT_TRUE(e->has_cmd);
    TEST_ASSERT_EQUAL_STRING("pnpm test --filter server", e->cmd);
    TEST_ASSERT_EQUAL_STRING("Run the tests", e->desc);
    TEST_ASSERT_EQUAL_STRING("Force push", e->reason);
    TEST_ASSERT_TRUE(e->has_grant);
    TEST_ASSERT_TRUE(e->grant_prefix);
    TEST_ASSERT_EQUAL_STRING("pnpm test", e->grant_rule);
    TEST_ASSERT_FALSE(e->cut);

    /* A cut entry never carries a grant, whatever the message said. */
    proto_permission_t c = perm(2, 30000);
    c.has_cmd = true;
    c.cut = true;
    c.has_grant = true;
    snprintf(c.grant_rule, sizeof(c.grant_rule), "x");
    TEST_ASSERT_EQUAL(REQQ_ADDED, reqq_add(&q, &c, 0));
    e = reqq_find(&q, id_of(2));
    TEST_ASSERT_TRUE(e->cut);
    TEST_ASSERT_FALSE(e->has_grant);
    TEST_ASSERT_EQUAL_STRING("", e->grant_rule);
}

static void test_text_is_sanitised(void)
{
    reqq_init(&q);
    proto_permission_t p = perm(1, 5000);
    snprintf(p.summary, sizeof(p.summary), "caf\xc3\xa9 \x1b[1m");
    TEST_ASSERT_EQUAL(REQQ_ADDED, reqq_add(&q, &p, 0));
    TEST_ASSERT_EQUAL_STRING("caf? ?[1m", reqq_head(&q)->summary);
}

static void test_duplicate_keeps_deadline(void)
{
    reqq_init(&q);
    const proto_permission_t p = perm(7, 30000);
    TEST_ASSERT_EQUAL(REQQ_ADDED, reqq_add(&q, &p, 0));
    reqq_head(&q)->acked = true;
    /* A replay after a reconnect carries a shorter remaining time; it must not restart the countdown. */
    const proto_permission_t replay = perm(7, 12000);
    TEST_ASSERT_EQUAL(REQQ_DUPLICATE, reqq_add(&q, &replay, 20000));
    TEST_ASSERT_EQUAL(1, reqq_count(&q));
    TEST_ASSERT_EQUAL_UINT32(30000, reqq_head(&q)->deadline_ms);
    TEST_ASSERT_TRUE(reqq_head(&q)->acked);
}

static void test_full_rejects_fifth(void)
{
    reqq_init(&q);
    for (int i = 1; i <= REQQ_CAP; i++) {
        const proto_permission_t p = perm(i, 30000);
        TEST_ASSERT_EQUAL(REQQ_ADDED, reqq_add(&q, &p, (uint32_t)i));
    }
    const proto_permission_t fifth = perm(5, 30000);
    TEST_ASSERT_EQUAL(REQQ_FULL, reqq_add(&q, &fifth, 10));
    TEST_ASSERT_EQUAL(REQQ_CAP, reqq_count(&q));
    TEST_ASSERT_NULL(reqq_find(&q, id_of(5)));
    /* A duplicate of a queued id is still a duplicate when the queue is full. */
    const proto_permission_t dup = perm(2, 30000);
    TEST_ASSERT_EQUAL(REQQ_DUPLICATE, reqq_add(&q, &dup, 10));
    char ids[8][PROTO_ID_SIZE];
    TEST_ASSERT_EQUAL(REQQ_CAP, reqq_ids(&q, ids, 8));
    for (int i = 0; i < REQQ_CAP; i++) {
        TEST_ASSERT_EQUAL_STRING(id_of(i + 1), ids[i]);
    }
    TEST_ASSERT_EQUAL(2, reqq_ids(&q, ids, 2));
}

static void test_remove_middle_keeps_order(void)
{
    reqq_init(&q);
    for (int i = 1; i <= 3; i++) {
        const proto_permission_t p = perm(i, 30000);
        reqq_add(&q, &p, 0);
    }
    TEST_ASSERT_FALSE(reqq_remove(&q, id_of(9)));
    TEST_ASSERT_FALSE(reqq_remove(&q, NULL));
    TEST_ASSERT_TRUE(reqq_remove(&q, id_of(2)));
    TEST_ASSERT_EQUAL(2, reqq_count(&q));
    TEST_ASSERT_EQUAL_STRING(id_of(1), q.entries[0].id);
    TEST_ASSERT_EQUAL_STRING(id_of(3), q.entries[1].id);
    TEST_ASSERT_EQUAL_STRING("", q.entries[2].id);
    TEST_ASSERT_FALSE(reqq_remove(&q, id_of(2)));
    TEST_ASSERT_TRUE(reqq_remove(&q, id_of(1)));
    TEST_ASSERT_EQUAL_STRING(id_of(3), reqq_head(&q)->id);
    TEST_ASSERT_TRUE(reqq_remove(&q, id_of(3)));
    TEST_ASSERT_NULL(reqq_head(&q));
}

static void test_expire_one_per_call_in_order(void)
{
    reqq_init(&q);
    const proto_permission_t a = perm(1, 30000); /* deadline 30000 */
    const proto_permission_t b = perm(2, 5000);  /* deadline 6000 */
    const proto_permission_t c = perm(3, 5000);  /* deadline 7000 */
    reqq_add(&q, &a, 0);
    reqq_add(&q, &b, 1000);
    reqq_add(&q, &c, 2000);

    char id[PROTO_ID_SIZE];
    uint32_t next = 0;
    TEST_ASSERT_TRUE(reqq_next_deadline(&q, 0, &next));
    TEST_ASSERT_EQUAL_UINT32(6000, next);
    TEST_ASSERT_FALSE(reqq_expire(&q, 5999, id));
    TEST_ASSERT_TRUE(reqq_expire(&q, 8000, id));
    TEST_ASSERT_EQUAL_STRING(id_of(2), id);
    TEST_ASSERT_TRUE(reqq_expire(&q, 8000, id));
    TEST_ASSERT_EQUAL_STRING(id_of(3), id);
    TEST_ASSERT_FALSE(reqq_expire(&q, 8000, id));
    TEST_ASSERT_EQUAL(1, reqq_count(&q));
    TEST_ASSERT_TRUE(reqq_expire(&q, 30000, id));
    TEST_ASSERT_EQUAL_STRING(id_of(1), id);
    TEST_ASSERT_FALSE(reqq_next_deadline(&q, 0, &next));
}

static void test_expire_across_clock_wrap(void)
{
    reqq_init(&q);
    const proto_permission_t p = perm(1, 5000);
    reqq_add(&q, &p, UINT32_MAX - 1000);
    char id[PROTO_ID_SIZE];
    TEST_ASSERT_FALSE(reqq_expire(&q, UINT32_MAX, id));
    TEST_ASSERT_FALSE(reqq_expire(&q, 3000, id));
    TEST_ASSERT_TRUE(reqq_expire(&q, 4000, id));
}

static void test_ack_tracking(void)
{
    reqq_init(&q);
    TEST_ASSERT_FALSE(reqq_has_unacked(&q));
    const proto_permission_t a = perm(1, 30000);
    const proto_permission_t b = perm(2, 30000);
    reqq_add(&q, &a, 0);
    TEST_ASSERT_TRUE(reqq_has_unacked(&q));
    reqq_mark_all_acked(&q);
    TEST_ASSERT_FALSE(reqq_has_unacked(&q));
    reqq_add(&q, &b, 10);
    TEST_ASSERT_TRUE(reqq_has_unacked(&q));
    TEST_ASSERT_TRUE(reqq_find(&q, id_of(1))->acked);
    TEST_ASSERT_FALSE(reqq_find(&q, id_of(2))->acked);
    reqq_mark_all_acked(&q);
    TEST_ASSERT_FALSE(reqq_has_unacked(&q));
    reqq_clear(&q);
    TEST_ASSERT_EQUAL(0, reqq_count(&q));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_add_copies_fields_and_sets_deadline);
    RUN_TEST(test_add_copies_card_fields);
    RUN_TEST(test_text_is_sanitised);
    RUN_TEST(test_duplicate_keeps_deadline);
    RUN_TEST(test_full_rejects_fifth);
    RUN_TEST(test_remove_middle_keeps_order);
    RUN_TEST(test_expire_one_per_call_in_order);
    RUN_TEST(test_expire_across_clock_wrap);
    RUN_TEST(test_ack_tracking);
    return UNITY_END();
}
