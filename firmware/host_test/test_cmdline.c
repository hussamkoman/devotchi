#include "cmdline.h"

#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define MAX_ARGV 6

typedef struct {
    const char *name;
    const char *line;
    int argc;
    const char *argv[MAX_ARGV];
} split_case_t;

static void test_split_table(void)
{
    static const split_case_t cases[] = {
        {"empty", "", 0, {0}},
        {"spaces only", "   \t ", 0, {0}},
        {"one word", "state", 1, {"state"}},
        {"surrounding spaces", "  state  ", 1, {"state"}},
        {"three words", "btn approve press", 3, {"btn", "approve", "press"}},
        {"repeated spaces", "btn   approve\tpress", 3, {"btn", "approve", "press"}},
        {"quoted", "wifi \"my net\" secret", 3, {"wifi", "my net", "secret"}},
        {"quote inside word", "a\"b c\"d e", 2, {"ab cd", "e"}},
        {"empty quoted", "wifi \"\" x", 3, {"wifi", "", "x"}},
        {"escaped quote", "wifi \"a\\\"b\" p", 3, {"wifi", "a\"b", "p"}},
        {"escaped backslash", "x \"a\\\\b\"", 2, {"x", "a\\b"}},
        {"backslash outside quotes", "x a\\b", 2, {"x", "a\\b"}},
        {"unterminated", "wifi \"abc", -1, {0}},
        {"escape at end of quote", "wifi \"abc\\", -1, {0}},
        {"max args", "1 2 3 4 5 6", 6, {"1", "2", "3", "4", "5", "6"}},
        {"too many", "1 2 3 4 5 6 7", -1, {0}},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char line[64];
        char *argv[MAX_ARGV] = {0};
        TEST_ASSERT_LESS_THAN_size_t(sizeof(line), strlen(cases[i].line));
        strcpy(line, cases[i].line);
        const int argc = cmdline_split(line, argv, MAX_ARGV);
        TEST_ASSERT_EQUAL_INT_MESSAGE(cases[i].argc, argc, cases[i].name);
        for (int a = 0; a < argc; a++) {
            TEST_ASSERT_EQUAL_STRING_MESSAGE(cases[i].argv[a], argv[a], cases[i].name);
        }
    }
}

static void test_rest(void)
{
    TEST_ASSERT_EQUAL_STRING("", cmdline_rest(""));
    TEST_ASSERT_EQUAL_STRING("", cmdline_rest("state"));
    TEST_ASSERT_EQUAL_STRING("", cmdline_rest("  state  "));
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"notify\", \"t\":\"a  b\"}",
                             cmdline_rest("msg  {\"type\":\"notify\", \"t\":\"a  b\"}"));
    TEST_ASSERT_EQUAL_STRING("b c ", cmdline_rest(" a\tb c "));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_split_table);
    RUN_TEST(test_rest);
    return UNITY_END();
}
