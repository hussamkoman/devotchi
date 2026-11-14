#include "ws_reasm.h"

#include "unity.h"

#include <string.h>

#define OP_CONT   0x0
#define OP_TEXT   0x1
#define OP_BIN    0x2
#define OP_CLOSE  0x8
#define OP_PING   0x9
#define OP_PONG   0xA

static ws_reasm_t r;
static char big[WS_REASM_CAPACITY + 16];

void setUp(void)
{
    memset(&r, 0xAA, sizeof(r));
    ws_reasm_reset(&r);
    for (size_t i = 0; i < sizeof(big); i++) {
        big[i] = (char)('a' + i % 26);
    }
}

void tearDown(void) {}

/* One read of a frame: the library reports the frame's payload length and the offset of this read. */
static ws_reasm_result_t feed(uint8_t op, bool fin, int frame_len, int offset, const char *data, int n)
{
    return ws_reasm_feed(&r, op, fin, frame_len, offset, data, n);
}

static ws_reasm_result_t feed_frame(uint8_t op, bool fin, const char *s)
{
    const int n = (int)strlen(s);
    return feed(op, fin, n, 0, s, n);
}

static void assert_message(const char *expected)
{
    TEST_ASSERT_EQUAL_size_t(strlen(expected), r.len);
    TEST_ASSERT_EQUAL_MEMORY(expected, r.buf, r.len);
}

static void test_single_frame(void)
{
    TEST_ASSERT_EQUAL(WS_REASM_MESSAGE, feed_frame(OP_TEXT, true, "{\"type\":\"ping\"}"));
    assert_message("{\"type\":\"ping\"}");
}

static void test_one_frame_over_three_reads(void)
{
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed(OP_TEXT, true, 9, 0, "abc", 3));
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed(OP_TEXT, true, 9, 3, "def", 3));
    TEST_ASSERT_EQUAL(WS_REASM_MESSAGE, feed(OP_TEXT, true, 9, 6, "ghi", 3));
    assert_message("abcdefghi");
}

static void test_text_and_two_continuation_frames(void)
{
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed_frame(OP_TEXT, false, "{\"type\":"));
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed_frame(OP_CONT, false, "\"pi"));
    TEST_ASSERT_EQUAL(WS_REASM_MESSAGE, feed_frame(OP_CONT, true, "ng\"}"));
    assert_message("{\"type\":\"ping\"}");
}

static void test_continuation_frames_each_over_two_reads(void)
{
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed(OP_TEXT, false, 4, 0, "ab", 2));
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed(OP_TEXT, false, 4, 2, "cd", 2));
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed(OP_CONT, false, 4, 0, "ef", 2));
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed(OP_CONT, false, 4, 2, "gh", 2));
    /* The last frame has fin set from its first read on; the message ends with the frame. */
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed(OP_CONT, true, 4, 0, "ij", 2));
    TEST_ASSERT_EQUAL(WS_REASM_MESSAGE, feed(OP_CONT, true, 4, 2, "kl", 2));
    assert_message("abcdefghijkl");
}

static void test_ping_between_fragments(void)
{
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed_frame(OP_TEXT, false, "hel"));
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed(OP_PING, true, 0, 0, NULL, 0));
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed_frame(OP_PONG, true, "xyz"));
    TEST_ASSERT_TRUE(r.in_msg);
    TEST_ASSERT_EQUAL_size_t(3, r.len);
    TEST_ASSERT_EQUAL(WS_REASM_MESSAGE, feed_frame(OP_CONT, true, "lo"));
    assert_message("hello");
}

static void test_exactly_capacity(void)
{
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed(OP_TEXT, true, WS_REASM_CAPACITY, 0, big, 1000));
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed(OP_TEXT, true, WS_REASM_CAPACITY, 1000, big + 1000, 2000));
    TEST_ASSERT_EQUAL(WS_REASM_MESSAGE,
                      feed(OP_TEXT, true, WS_REASM_CAPACITY, 3000, big + 3000, WS_REASM_CAPACITY - 3000));
    TEST_ASSERT_EQUAL_size_t(WS_REASM_CAPACITY, r.len);
    TEST_ASSERT_EQUAL_MEMORY(big, r.buf, WS_REASM_CAPACITY);
}

static void test_exactly_capacity_across_frames(void)
{
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed(OP_TEXT, false, 4000, 0, big, 4000));
    TEST_ASSERT_EQUAL(WS_REASM_MESSAGE, feed(OP_CONT, true, 96, 0, big + 4000, 96));
    TEST_ASSERT_EQUAL_size_t(WS_REASM_CAPACITY, r.len);
    TEST_ASSERT_EQUAL_MEMORY(big, r.buf, WS_REASM_CAPACITY);
}

static void test_oversize_dropped_and_next_message_intact(void)
{
    const int total = WS_REASM_CAPACITY + 1;
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed(OP_TEXT, true, total, 0, big, 4000));
    TEST_ASSERT_EQUAL(WS_REASM_DROPPED, feed(OP_TEXT, true, total, 4000, big + 4000, total - 4000));

    TEST_ASSERT_EQUAL(WS_REASM_MESSAGE, feed_frame(OP_TEXT, true, "{\"type\":\"pong\"}"));
    assert_message("{\"type\":\"pong\"}");
}

static void test_oversize_across_continuation_frames(void)
{
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed(OP_TEXT, false, 3000, 0, big, 3000));
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed(OP_CONT, false, 1097, 0, big, 1097));
    /* Once a message overflowed, later fragments that would fit are still discarded. */
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed(OP_CONT, false, 2, 0, "ab", 2));
    TEST_ASSERT_EQUAL(WS_REASM_DROPPED, feed(OP_CONT, true, 2, 0, "cd", 2));
    TEST_ASSERT_EQUAL(WS_REASM_MESSAGE, feed_frame(OP_TEXT, true, "ok"));
    assert_message("ok");
}

static void test_binary_dropped(void)
{
    TEST_ASSERT_EQUAL(WS_REASM_DROPPED, feed_frame(OP_BIN, true, "\x01\x02\x03"));
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed_frame(OP_BIN, false, "ab"));
    TEST_ASSERT_EQUAL(WS_REASM_DROPPED, feed_frame(OP_CONT, true, "cd"));
    TEST_ASSERT_EQUAL(WS_REASM_MESSAGE, feed_frame(OP_TEXT, true, "text"));
    assert_message("text");
}

static void test_empty_text_frame(void)
{
    TEST_ASSERT_EQUAL(WS_REASM_MESSAGE, feed(OP_TEXT, true, 0, 0, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0, r.len);
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed(OP_TEXT, false, 0, 0, "", 0));
    TEST_ASSERT_EQUAL(WS_REASM_MESSAGE, feed_frame(OP_CONT, true, "x"));
    assert_message("x");
}

static void test_stray_continuation(void)
{
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed_frame(OP_CONT, true, "stray"));
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed_frame(OP_CONT, false, "stray"));
    TEST_ASSERT_FALSE(r.in_msg);
    /* The tail of a frame whose start was never seen. */
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed(OP_TEXT, true, 10, 5, "tail!", 5));
    TEST_ASSERT_EQUAL(WS_REASM_MESSAGE, feed_frame(OP_TEXT, true, "next"));
    assert_message("next");
}

static void test_new_text_frame_restarts_an_unfinished_message(void)
{
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed_frame(OP_TEXT, false, "lost"));
    TEST_ASSERT_EQUAL(WS_REASM_MESSAGE, feed_frame(OP_TEXT, true, "kept"));
    assert_message("kept");
}

static void test_close_codes(void)
{
    TEST_ASSERT_EQUAL(WS_REASM_CLOSE, feed(OP_CLOSE, true, 2, 0, "\x0f\xa3", 2));
    TEST_ASSERT_EQUAL_UINT16(4003, r.close_code);
    TEST_ASSERT_EQUAL(WS_REASM_CLOSE, feed(OP_CLOSE, true, 9, 0, "\x0f\xa2too old", 9));
    TEST_ASSERT_EQUAL_UINT16(4002, r.close_code);
    TEST_ASSERT_EQUAL(WS_REASM_CLOSE, feed(OP_CLOSE, true, 2, 0, "\x03\xe8", 2));
    TEST_ASSERT_EQUAL_UINT16(1000, r.close_code);
}

static void test_close_without_code(void)
{
    TEST_ASSERT_EQUAL(WS_REASM_CLOSE, feed(OP_CLOSE, true, 0, 0, NULL, 0));
    TEST_ASSERT_EQUAL_UINT16(WS_REASM_CLOSE_NO_CODE, r.close_code);
    TEST_ASSERT_EQUAL(WS_REASM_CLOSE, feed(OP_CLOSE, true, 1, 0, "\x0f", 1));
    TEST_ASSERT_EQUAL_UINT16(WS_REASM_CLOSE_NO_CODE, r.close_code);
    /* A later read of a long close reason carries no code. */
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed(OP_CLOSE, true, 20, 10, "0123456789", 10));
}

static void test_close_between_fragments_keeps_the_code(void)
{
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed_frame(OP_TEXT, false, "part"));
    TEST_ASSERT_EQUAL(WS_REASM_CLOSE, feed(OP_CLOSE, true, 2, 0, "\x0f\xa3", 2));
    TEST_ASSERT_EQUAL_UINT16(4003, r.close_code);
}

static void test_reset(void)
{
    feed_frame(OP_TEXT, false, "part");
    feed(OP_CLOSE, true, 2, 0, "\x0f\xa3", 2);
    ws_reasm_reset(&r);
    TEST_ASSERT_FALSE(r.in_msg);
    TEST_ASSERT_FALSE(r.dropping);
    TEST_ASSERT_EQUAL_size_t(0, r.len);
    TEST_ASSERT_EQUAL_UINT16(0, r.close_code);
    TEST_ASSERT_EQUAL(WS_REASM_NONE, feed_frame(OP_CONT, true, "rest"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_single_frame);
    RUN_TEST(test_one_frame_over_three_reads);
    RUN_TEST(test_text_and_two_continuation_frames);
    RUN_TEST(test_continuation_frames_each_over_two_reads);
    RUN_TEST(test_ping_between_fragments);
    RUN_TEST(test_exactly_capacity);
    RUN_TEST(test_exactly_capacity_across_frames);
    RUN_TEST(test_oversize_dropped_and_next_message_intact);
    RUN_TEST(test_oversize_across_continuation_frames);
    RUN_TEST(test_binary_dropped);
    RUN_TEST(test_empty_text_frame);
    RUN_TEST(test_stray_continuation);
    RUN_TEST(test_new_text_frame_restarts_an_unfinished_message);
    RUN_TEST(test_close_codes);
    RUN_TEST(test_close_without_code);
    RUN_TEST(test_close_between_fragments_keeps_the_code);
    RUN_TEST(test_reset);
    return UNITY_END();
}
