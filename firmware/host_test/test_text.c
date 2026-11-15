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

static void test_sanitize_device_table(void)
{
    static const sanitize_case_t cases[] = {
        {"empty", "", ""},
        {"ascii", "Edit src/main.c", "Edit src/main.c"},
        {"newline kept", "a\nb", "a\nb"},
        {"controls", "a\tb\rc\x7f", "a?b?c?"},
        {"latin-1 kept", "caf\xc3\xa9 \xc3\xbc\xc3\x9f \xc2\xa0x", "caf\xc3\xa9 \xc3\xbc\xc3\x9f \xc2\xa0x"},
        {"c1 controls", "\xc2\x85x", "?x"},
        {"soft hyphen dropped", "co\xc2\xad" "op", "coop"},
        {"dashes and quotes", "\xe2\x80\x93\xe2\x80\x94\xe2\x80\x98\xe2\x80\x99\xe2\x80\x9c\xe2\x80\x9d",
         "\xe2\x80\x93\xe2\x80\x94\xe2\x80\x98\xe2\x80\x99\xe2\x80\x9c\xe2\x80\x9d"},
        {"bullet ellipsis", "\xe2\x80\xa2 a\xe2\x80\xa6", "\xe2\x80\xa2 a\xe2\x80\xa6"},
        {"arrows", "\xe2\x86\x90\xe2\x86\x91\xe2\x86\x92\xe2\x86\x93", "\xe2\x86\x90\xe2\x86\x91\xe2\x86\x92\xe2\x86\x93"},
        {"check times minus", "\xe2\x9c\x93 \xc3\x97 \xe2\x88\x92", "\xe2\x9c\x93 \xc3\x97 \xe2\x88\x92"},
        {"euro replaced", "\xe2\x82\xac" "5", "?5"},
        {"emoji replaced", "ok \xf0\x9f\x98\x80!", "ok ?!"},
        {"cjk replaced", "\xe4\xb8\xad\xe6\x96\x87", "??"},
        {"private use replaced", "\xee\x80\x80", "?"},
        {"lone continuation", "a\x80z", "a?z"},
        {"continuation run", "a\x80\x80\x80z", "a?z"},
        {"truncated two-byte", "a\xc3", "a?"},
        {"truncated three-byte", "a\xe2\x82", "a?"},
        {"overlong slash", "\xc0\xaf", "?"},
        {"overlong e9", "\xc1\xa9", "?"},
        {"surrogate", "\xed\xa0\x80", "?"},
        {"above U+10FFFF", "\xf4\x90\x80\x80", "?"},
        {"invalid ff fe", "\xff\xfe", "??"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char buf[64];
        memset(buf, 'x', sizeof(buf));
        const size_t n = text_sanitize_device(buf, sizeof(buf), cases[i].in);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(cases[i].out, buf, cases[i].name);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(strlen(cases[i].out), n, cases[i].name);
    }
}

static void test_sanitize_device_bounds(void)
{
    char buf[5];
    TEST_ASSERT_EQUAL_size_t(4, text_sanitize_device(buf, sizeof(buf), "abcdefgh"));
    TEST_ASSERT_EQUAL_STRING("abcd", buf);
    /* A kept sequence that does not fit whole is not split. */
    TEST_ASSERT_EQUAL_size_t(3, text_sanitize_device(buf, sizeof(buf), "abc\xc3\xa9"));
    TEST_ASSERT_EQUAL_STRING("abc", buf);
    TEST_ASSERT_EQUAL_size_t(4, text_sanitize_device(buf, sizeof(buf), "abc\xe2\x82\xac"));
    TEST_ASSERT_EQUAL_STRING("abc?", buf);
    char guard[2] = {'x', 'y'};
    TEST_ASSERT_EQUAL_size_t(0, text_sanitize_device(guard, 0, "abc"));
    TEST_ASSERT_EQUAL_CHAR('x', guard[0]);
    TEST_ASSERT_EQUAL_size_t(0, text_sanitize_device(NULL, 8, "abc"));
    TEST_ASSERT_EQUAL_size_t(0, text_sanitize_device(buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_STRING("", buf);

    char in_place[24] = "h\xc3\xa9\xf0\x9f\x98\x80llo\t\x80\x80!";
    TEST_ASSERT_EQUAL_size_t(10, text_sanitize_device(in_place, sizeof(in_place), in_place));
    TEST_ASSERT_EQUAL_STRING("h\xc3\xa9?llo?" "?!", in_place);
}

static void test_device_charset(void)
{
    TEST_ASSERT_TRUE(text_device_has(' '));
    TEST_ASSERT_TRUE(text_device_has('~'));
    TEST_ASSERT_FALSE(text_device_has(0x7F));
    TEST_ASSERT_FALSE(text_device_has(0x9F));
    TEST_ASSERT_TRUE(text_device_has(0xA0));
    TEST_ASSERT_FALSE(text_device_has(0xAD));
    TEST_ASSERT_TRUE(text_device_has(0xFF));
    TEST_ASSERT_FALSE(text_device_has(0x100));
    TEST_ASSERT_TRUE(text_device_has(0x2713));
    TEST_ASSERT_FALSE(text_device_has(0x20AC));
}

static void test_utf8_len_and_upper(void)
{
    TEST_ASSERT_EQUAL_size_t(0, text_utf8_len(""));
    TEST_ASSERT_EQUAL_size_t(0, text_utf8_len(NULL));
    TEST_ASSERT_EQUAL_size_t(4, text_utf8_len("caf\xc3\xa9"));
    TEST_ASSERT_EQUAL_size_t(3, text_utf8_len("a\x80z"));
    TEST_ASSERT_EQUAL_size_t(2, text_utf8_len("\xe2\x80\xa6\xf0\x9f\x98\x80"));

    char buf[32];
    TEST_ASSERT_EQUAL_size_t(15, text_upper_device(buf, sizeof(buf), "about 1/2 caf\xc3\xa9"));
    TEST_ASSERT_EQUAL_STRING("ABOUT 1/2 CAF\xc3\x89", buf);
    text_upper_device(buf, sizeof(buf), "\xc3\xb7\xc3\xbf\xc3\xbe\xc3\x9f");
    TEST_ASSERT_EQUAL_STRING("\xc3\xb7\xc3\xbf\xc3\x9e\xc3\x9f", buf);
    char small[3];
    TEST_ASSERT_EQUAL_size_t(1, text_upper_device(small, sizeof(small), "a\xc3\xa9"));
    TEST_ASSERT_EQUAL_STRING("A", small);
}

static void test_truncate_middle(void)
{
    char buf[48];
    TEST_ASSERT_EQUAL_size_t(5, text_truncate_middle(buf, sizeof(buf), "short", 10));
    TEST_ASSERT_EQUAL_STRING("short", buf);
    text_truncate_middle(buf, sizeof(buf), "devotchi.example.com:8080", 10);
    TEST_ASSERT_EQUAL_STRING("devot\xe2\x80\xa6" "8080", buf);
    TEST_ASSERT_EQUAL_size_t(10, text_utf8_len(buf));
    text_truncate_middle(buf, sizeof(buf), "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9", 5);
    TEST_ASSERT_EQUAL_STRING("\xc3\xa9\xc3\xa9\xe2\x80\xa6\xc3\xa9\xc3\xa9", buf);
    /* Too small a buffer for the ellipsis form: falls back to a whole-code-point cut. */
    char tiny[6];
    text_truncate_middle(tiny, sizeof(tiny), "abcdefghijkl", 8);
    TEST_ASSERT_EQUAL_STRING("abcde", tiny);
    TEST_ASSERT_EQUAL_size_t(0, text_truncate_middle(NULL, 4, "abc", 2));
}

static void test_wrap_counts_code_points(void)
{
    /* Five two-byte letters fit five columns; the sixth starts a new line. */
    TEST_ASSERT_EQUAL_size_t(1, text_wrap_lines("\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9", 5));
    TEST_ASSERT_EQUAL_size_t(2, text_wrap_lines("\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9", 5));
    char page[32];
    TEST_ASSERT_TRUE(text_wrap_page(page, sizeof(page), "\xe2\x86\x92\xe2\x86\x92\xe2\x86\x92\xe2\x86\x92", 3, 1, 1));
    TEST_ASSERT_EQUAL_STRING("\xe2\x86\x92", page);
    TEST_ASSERT_TRUE(text_wrap_page(page, sizeof(page), "ab\xc3\xa9 cd", 3, 2, 0));
    TEST_ASSERT_EQUAL_STRING("ab\xc3\xa9\ncd", page);
    /* A malformed byte takes one column and is never merged into a neighbour. */
    TEST_ASSERT_TRUE(text_wrap_page(page, sizeof(page), "a\xe2\x82" "bcd", 3, 1, 0));
    TEST_ASSERT_EQUAL_STRING("a\xe2\x82", page);
}

static void test_wrap_lines_first(void)
{
    TEST_ASSERT_EQUAL_size_t(0, text_wrap_lines_first("", 10));
    TEST_ASSERT_EQUAL_size_t(1, text_wrap_lines_first("short\nsecond\nthird", 10));
    TEST_ASSERT_EQUAL_size_t(1, text_wrap_lines_first("exactly10c\nnext", 10));
    TEST_ASSERT_EQUAL_size_t(2, text_wrap_lines_first("one two three\nnext", 10));
    TEST_ASSERT_EQUAL_size_t(3, text_wrap_lines_first("aaaaaaaaaabbbbbbbbbbcc", 10));
    TEST_ASSERT_EQUAL_size_t(1, text_wrap_lines_first("\nsecond", 10));
    TEST_ASSERT_EQUAL_size_t(0, text_wrap_lines_first("abc", 0));
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
    RUN_TEST(test_sanitize_device_table);
    RUN_TEST(test_sanitize_device_bounds);
    RUN_TEST(test_device_charset);
    RUN_TEST(test_utf8_len_and_upper);
    RUN_TEST(test_truncate_middle);
    RUN_TEST(test_wrap_counts_code_points);
    RUN_TEST(test_wrap_lines_first);
    return UNITY_END();
}
