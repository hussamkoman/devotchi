#include "pair_code.h"

#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_alphabet(void)
{
    TEST_ASSERT_EQUAL_size_t(PAIR_CODE_ALPHABET_LEN, strlen(PAIR_CODE_ALPHABET));
    TEST_ASSERT_NULL(strpbrk(PAIR_CODE_ALPHABET, "0OIL1"));
}

static void test_is_valid(void)
{
    TEST_ASSERT_TRUE(pair_code_is_valid("K7M2QX"));
    TEST_ASSERT_TRUE(pair_code_is_valid("k7m2qx"));
    TEST_ASSERT_TRUE(pair_code_is_valid("FBMNDP"));
    TEST_ASSERT_TRUE(pair_code_is_valid("234567"));

    TEST_ASSERT_FALSE(pair_code_is_valid(NULL));
    TEST_ASSERT_FALSE(pair_code_is_valid(""));
    TEST_ASSERT_FALSE(pair_code_is_valid("K7M2Q"));
    TEST_ASSERT_FALSE(pair_code_is_valid("K7M2QXA"));
    TEST_ASSERT_FALSE(pair_code_is_valid("K7M 2QX"));
    TEST_ASSERT_FALSE(pair_code_is_valid("K7M2Q0"));
    TEST_ASSERT_FALSE(pair_code_is_valid("K7M2QO"));
    TEST_ASSERT_FALSE(pair_code_is_valid("K7M2QI"));
    TEST_ASSERT_FALSE(pair_code_is_valid("K7M2QL"));
    TEST_ASSERT_FALSE(pair_code_is_valid("K7M2Q1"));
    TEST_ASSERT_FALSE(pair_code_is_valid("K7M2Ql"));
    TEST_ASSERT_FALSE(pair_code_is_valid("K7M2Q!"));
    TEST_ASSERT_FALSE(pair_code_is_valid("K7M2Q\xc3"));
}

static void test_normalise(void)
{
    char out[PAIR_CODE_SIZE];
    TEST_ASSERT_TRUE(pair_code_normalise("k7m2qx", out));
    TEST_ASSERT_EQUAL_STRING("K7M2QX", out);
    TEST_ASSERT_TRUE(pair_code_normalise("K7M 2QX", out));
    TEST_ASSERT_EQUAL_STRING("K7M2QX", out);
    TEST_ASSERT_TRUE(pair_code_normalise(" k7m-2qx ", out));
    TEST_ASSERT_EQUAL_STRING("K7M2QX", out);

    TEST_ASSERT_FALSE(pair_code_normalise("K7M2Q", out));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_FALSE(pair_code_normalise("K7M2QXZ", out));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_FALSE(pair_code_normalise("K7M2Q0", out));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_FALSE(pair_code_normalise("", out));
    TEST_ASSERT_FALSE(pair_code_normalise(NULL, out));
}

static void test_format(void)
{
    char out[PAIR_CODE_DISPLAY_SIZE];
    TEST_ASSERT_TRUE(pair_code_format("K7M2QX", out));
    TEST_ASSERT_EQUAL_STRING("K7M 2QX", out);
    TEST_ASSERT_FALSE(pair_code_format("K7M2Q", out));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_FALSE(pair_code_format("K7M2QXA", out));
    TEST_ASSERT_FALSE(pair_code_format(NULL, out));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_alphabet);
    RUN_TEST(test_is_valid);
    RUN_TEST(test_normalise);
    RUN_TEST(test_format);
    return UNITY_END();
}
