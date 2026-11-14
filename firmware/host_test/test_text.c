#include "text.h"

#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

typedef struct {
    const char *name;
    const char *in;
    const char *out;
} sanitize_case_t;

static void test_sanitize_table(void)
{
    static const sanitize_case_t cases[] = {
        {"empty", "", ""},
        {"plain", "Edit src/main.c", "Edit src/main.c"},
        {"newline kept", "a\nb", "a\nb"},
        {"tab cr del", "a\tb\rc\x7f", "a?b?c?"},
        {"escape", "\x1b[31mred", "?[31mred"},
        {"two-byte", "caf\xc3\xa9", "caf?"},
        {"three-byte", "\xe2\x82\xac" "5", "?5"},
        {"four-byte emoji", "ok \xf0\x9f\x98\x80!", "ok ?!"},
        {"two code points", "\xc3\xa9\xc3\xa8", "??"},
        {"lone continuation", "a\x80z", "a?z"},
        {"continuation run", "a\x80\x80\x80z", "a?z"},
        {"truncated two-byte", "a\xc3", "a?"},
        {"truncated three-byte", "a\xe2\x82", "a?"},
        {"truncated then ascii", "\xe2\x82z", "?z"},
        {"overlong slash", "\xc0\xaf", "?"},
        {"overlong three-byte", "\xe0\x80\xaf", "?"},
        {"surrogate", "\xed\xa0\x80", "?"},
        {"above U+10FFFF", "\xf4\x90\x80\x80", "?"},
        {"invalid lead f8", "\xf8\x88\x80\x80\x80", "?"},
        {"invalid ff fe", "\xff\xfe", "??"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char buf[64];
        memset(buf, 'x', sizeof(buf));
        const size_t n = text_sanitize_ascii(buf, sizeof(buf), cases[i].in);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(cases[i].out, buf, cases[i].name);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(strlen(cases[i].out), n, cases[i].name);
    }
}

static void test_sanitize_bounds(void)
{
    char buf[5];
    TEST_ASSERT_EQUAL_size_t(4, text_sanitize_ascii(buf, sizeof(buf), "abcdefgh"));
    TEST_ASSERT_EQUAL_STRING("abcd", buf);

    TEST_ASSERT_EQUAL_size_t(4, text_sanitize_ascii(buf, sizeof(buf), "abc\xc3\xa9" "fg"));
    TEST_ASSERT_EQUAL_STRING("abc?", buf);

    char one[1] = {'x'};
    TEST_ASSERT_EQUAL_size_t(0, text_sanitize_ascii(one, sizeof(one), "abc"));
    TEST_ASSERT_EQUAL_CHAR('\0', one[0]);

    char guard[2] = {'x', 'y'};
    TEST_ASSERT_EQUAL_size_t(0, text_sanitize_ascii(guard, 0, "abc"));
    TEST_ASSERT_EQUAL_CHAR('x', guard[0]);
    TEST_ASSERT_EQUAL_size_t(0, text_sanitize_ascii(NULL, 8, "abc"));

    TEST_ASSERT_EQUAL_size_t(0, text_sanitize_ascii(buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

static void test_sanitize_in_place_is_safe(void)
{
    char buf[16] = "h\xc3\xa9llo\t!";
    TEST_ASSERT_EQUAL_size_t(7, text_sanitize_ascii(buf, sizeof(buf), buf));
    TEST_ASSERT_EQUAL_STRING("h?llo?!", buf);
}

typedef struct {
    const char *name;
    const char *in;
    size_t cap;
    const char *out;
} truncate_case_t;

static void test_truncate_table(void)
{
    static const truncate_case_t cases[] = {
        {"fits", "hello", 16, "hello"},
        {"exact", "hello", 6, "hello"},
        {"ascii cut", "hello", 4, "hel"},
        {"cap one", "hello", 1, ""},
        {"two-byte fits", "caf\xc3\xa9", 6, "caf\xc3\xa9"},
        {"two-byte cut", "caf\xc3\xa9", 5, "caf"},
        {"three-byte cut after 1", "a\xe2\x82\xac", 3, "a"},
        {"three-byte cut after 2", "a\xe2\x82\xac", 4, "a"},
        {"three-byte fits", "a\xe2\x82\xac", 5, "a\xe2\x82\xac"},
        {"four-byte cut", "\xf0\x9f\x98\x80\xf0\x9f\x98\x80", 8, "\xf0\x9f\x98\x80"},
        {"four-byte none", "\xf0\x9f\x98\x80", 4, ""},
        {"malformed copied bytewise", "a\xff\x80" "b", 16, "a\xff\x80" "b"},
        {"malformed cut", "a\xff\x80" "b", 3, "a\xff"},
        {"truncated lead at end", "ab\xe2\x82", 16, "ab\xe2\x82"},
        {"empty", "", 4, ""},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char buf[32];
        memset(buf, 'x', sizeof(buf));
        TEST_ASSERT_LESS_OR_EQUAL_size_t(sizeof(buf), cases[i].cap);
        const size_t n = text_utf8_truncate(buf, cases[i].cap, cases[i].in);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(cases[i].out, buf, cases[i].name);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(strlen(cases[i].out), n, cases[i].name);
        TEST_ASSERT_EQUAL_CHAR_MESSAGE('x', buf[cases[i].cap], cases[i].name);
    }
}

static void test_truncate_bounds(void)
{
    char guard[2] = {'x', 'y'};
    TEST_ASSERT_EQUAL_size_t(0, text_utf8_truncate(guard, 0, "abc"));
    TEST_ASSERT_EQUAL_CHAR('x', guard[0]);
    TEST_ASSERT_EQUAL_size_t(0, text_utf8_truncate(NULL, 4, "abc"));
    TEST_ASSERT_EQUAL_size_t(0, text_utf8_truncate(guard, sizeof(guard), NULL));
    TEST_ASSERT_EQUAL_CHAR('\0', guard[0]);
}

typedef struct {
    const char *name;
    const char *in;
    size_t cols;
    size_t lines;
} wrap_case_t;

static void test_wrap_lines_table(void)
{
    static const wrap_case_t cases[] = {
        {"empty", "", 10, 0},
        {"short", "abc", 10, 1},
        {"exact", "abcdefghij", 10, 1},
        {"newline", "ab\ncd", 10, 2},
        {"trailing newline", "ab\n", 10, 1},
        {"blank line", "ab\n\ncd", 10, 3},
        {"word wrap", "aaaa bbbb cccc", 10, 2},
        {"hard break", "abcdefghijklmnopqrstuvwxy", 10, 3},
        {"space at limit", "abcdefghij klm", 10, 2},
        {"exact then newline", "abcdefghij\nk", 10, 2},
        {"leading spaces then long word", " abcdefghijklm", 10, 2},
        {"zero cols", "abc", 0, 0},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TEST_ASSERT_EQUAL_MESSAGE(cases[i].lines, text_wrap_lines(cases[i].in, cases[i].cols), cases[i].name);
    }
}

static void test_wrap_page_content(void)
{
    char out[64];
    const char *src = "aaaa bbbb cccc\n$ rm -rf dist\nxyz";
    /* Lines at 10 columns: "aaaa bbbb", "cccc", "$ rm -rf", "dist", "xyz". */
    TEST_ASSERT_EQUAL(5, text_wrap_lines(src, 10));
    TEST_ASSERT_EQUAL(3, text_wrap_pages(src, 10, 2));
    TEST_ASSERT_TRUE(text_wrap_page(out, sizeof(out), src, 10, 2, 0));
    TEST_ASSERT_EQUAL_STRING("aaaa bbbb\ncccc", out);
    TEST_ASSERT_TRUE(text_wrap_page(out, sizeof(out), src, 10, 2, 1));
    TEST_ASSERT_EQUAL_STRING("$ rm -rf\ndist", out);
    TEST_ASSERT_TRUE(text_wrap_page(out, sizeof(out), src, 10, 2, 2));
    TEST_ASSERT_EQUAL_STRING("xyz", out);
    TEST_ASSERT_FALSE(text_wrap_page(out, sizeof(out), src, 10, 2, 3));
    TEST_ASSERT_EQUAL_STRING("", out);

    TEST_ASSERT_TRUE(text_wrap_page(out, sizeof(out), "abcdefghijklmnopqrstuvwxy", 10, 6, 0));
    TEST_ASSERT_EQUAL_STRING("abcdefghij\nklmnopqrst\nuvwxy", out);
    TEST_ASSERT_TRUE(text_wrap_page(out, sizeof(out), "ab\n\ncd", 10, 6, 0));
    TEST_ASSERT_EQUAL_STRING("ab\n\ncd", out);
}

static void test_wrap_bounds(void)
{
    char out[8];
    TEST_ASSERT_EQUAL(0, text_wrap_pages("", 38, 6));
    TEST_ASSERT_EQUAL(0, text_wrap_pages("abc", 38, 0));
    TEST_ASSERT_FALSE(text_wrap_page(out, sizeof(out), "", 38, 6, 0));
    /* "abcde\nfghij" needs 12 bytes; a page that does not fit is refused, not cut. */
    TEST_ASSERT_FALSE(text_wrap_page(out, sizeof(out), "abcde\nfghij", 38, 6, 0));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_FALSE(text_wrap_page(NULL, 0, "abc", 38, 6, 0));

    /* A full details field: 600 characters without spaces at 38 x 6 is 16 lines, 3 pages. */
    char big[601];
    memset(big, 'x', 600);
    big[600] = '\0';
    char page[256];
    TEST_ASSERT_EQUAL(16, text_wrap_lines(big, 38));
    TEST_ASSERT_EQUAL(3, text_wrap_pages(big, 38, 6));
    TEST_ASSERT_TRUE(text_wrap_page(page, sizeof(page), big, 38, 6, 0));
    TEST_ASSERT_EQUAL(6 * 38 + 5, strlen(page));
    TEST_ASSERT_TRUE(text_wrap_page(page, sizeof(page), big, 38, 6, 2));
    TEST_ASSERT_EQUAL(38 * 3 + 30 + 3, strlen(page));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sanitize_table);
    RUN_TEST(test_sanitize_bounds);
    RUN_TEST(test_sanitize_in_place_is_safe);
    RUN_TEST(test_truncate_table);
    RUN_TEST(test_truncate_bounds);
    RUN_TEST(test_wrap_lines_table);
    RUN_TEST(test_wrap_page_content);
    RUN_TEST(test_wrap_bounds);
    return UNITY_END();
}
