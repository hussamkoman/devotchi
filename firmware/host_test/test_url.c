#include "url.h"

#include "unity.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_accepts_ipv4_and_hostnames(void)
{
    static const struct {
        const char *in;
        bool tls;
        uint16_t port;
        const char *host;
    } CASES[] = {
        {"ws://10.0.0.120:8080", false, 8080, "10.0.0.120"},
        {"ws://10.0.0.120", false, 80, "10.0.0.120"},
        {"wss://devotchi.example.com", true, 443, "devotchi.example.com"},
        {"wss://devotchi.example.com:8443", true, 8443, "devotchi.example.com"},
        {"WSS://Devotchi.Example.COM", true, 443, "devotchi.example.com"},
        {"ws://localhost:1", false, 1, "localhost"},
        {"ws://my-host:65535", false, 65535, "my-host"},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        url_t u;
        TEST_ASSERT_TRUE_MESSAGE(url_parse(CASES[i].in, &u), CASES[i].in);
        TEST_ASSERT_EQUAL_MESSAGE(CASES[i].tls, u.tls, CASES[i].in);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(CASES[i].port, u.port, CASES[i].in);
        TEST_ASSERT_EQUAL_STRING(CASES[i].host, u.host);
    }
}

static void test_trailing_slash_is_stripped(void)
{
    url_t u;
    char buf[URL_FULL_SIZE];
    TEST_ASSERT_TRUE(url_parse("ws://10.0.0.120:8080/", &u));
    TEST_ASSERT_EQUAL_STRING("10.0.0.120", u.host);
    TEST_ASSERT_EQUAL_UINT16(8080, u.port);
    TEST_ASSERT_EQUAL_INT(20, url_base(&u, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("ws://10.0.0.120:8080", buf);
    TEST_ASSERT_TRUE(url_parse("wss://example.com/", &u));
    TEST_ASSERT_FALSE(url_parse("ws://example.com//", &u));
}

static void test_rejects(void)
{
    static const char *const BAD[] = {
        "",
        "ws://",
        "wss://",
        "ws:///",
        "http://example.com",
        "https://example.com",
        "ftp://example.com",
        "example.com",
        "ws:/example.com",
        "ws//example.com",
        " ws://example.com",
        "ws://example.com ",
        "ws://example.com/ws",
        "ws://example.com/a/",
        "ws://example.com?token=abc",
        "ws://example.com/?x=1",
        "ws://example.com#frag",
        "ws://user@example.com",
        "ws://user:pass@example.com",
        "ws://example.com:",
        "ws://example.com:0",
        "ws://example.com:65536",
        "ws://example.com:123456",
        "ws://example.com:80a",
        "ws://example.com:-1",
        "ws://:8080",
        "ws://.example.com",
        "ws://example.com.",
        "ws://exa..mple.com",
        "ws://-example.com",
        "ws://example.com-",
        "ws://exam_ple.com",
        "ws://[::1]:8080",
        "ws://ex\tample.com",
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++) {
        url_t u;
        TEST_ASSERT_FALSE_MESSAGE(url_parse(BAD[i], &u), BAD[i]);
    }
    url_t u;
    TEST_ASSERT_FALSE(url_parse(NULL, &u));
    TEST_ASSERT_FALSE(url_parse("ws://example.com", NULL));
}

static void test_length_limit(void)
{
    char s[URL_MAX_LEN + 3];
    url_t u;

    memset(s, 'a', sizeof(s));
    memcpy(s, "ws://", 5);
    s[URL_MAX_LEN] = '\0';
    TEST_ASSERT_EQUAL_size_t(URL_MAX_LEN, strlen(s));
    TEST_ASSERT_TRUE(url_parse(s, &u));
    TEST_ASSERT_EQUAL_size_t(URL_MAX_LEN - 5, strlen(u.host));

    s[URL_MAX_LEN] = 'a';
    s[URL_MAX_LEN + 1] = '\0';
    TEST_ASSERT_FALSE(url_parse(s, &u));
}

static void test_derived_urls(void)
{
    url_t u;
    char buf[URL_FULL_SIZE];

    TEST_ASSERT_TRUE(url_parse("ws://10.0.0.120:18090", &u));
    TEST_ASSERT_GREATER_THAN(0, url_ws(&u, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("ws://10.0.0.120:18090/ws", buf);
    TEST_ASSERT_GREATER_THAN(0, url_http(&u, "/api/pair/claim", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("http://10.0.0.120:18090/api/pair/claim", buf);

    TEST_ASSERT_TRUE(url_parse("wss://example.com", &u));
    url_ws(&u, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("wss://example.com/ws", buf);
    url_http(&u, "/health", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("https://example.com/health", buf);

    TEST_ASSERT_TRUE(url_parse("wss://example.com:443", &u));
    url_base(&u, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("wss://example.com", buf);
    TEST_ASSERT_TRUE(url_parse("ws://example.com:443", &u));
    url_base(&u, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("ws://example.com:443", buf);
    TEST_ASSERT_TRUE(url_parse("wss://example.com:80", &u));
    url_http(&u, "/health", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("https://example.com:80/health", buf);
}

static void test_derived_url_errors(void)
{
    url_t u;
    char small[12];
    char buf[URL_FULL_SIZE];
    TEST_ASSERT_TRUE(url_parse("ws://example.com", &u));
    TEST_ASSERT_EQUAL_INT(-1, url_ws(&u, small, sizeof(small)));
    TEST_ASSERT_EQUAL_STRING("", small);
    TEST_ASSERT_EQUAL_INT(-1, url_http(&u, "health", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, url_http(&u, NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, url_ws(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, url_ws(&u, buf, 0));
}

static void test_longest_url_fits_full_size(void)
{
    char s[URL_BASE_SIZE];
    url_t u;
    char buf[URL_FULL_SIZE];
    memset(s, 'a', sizeof(s));
    memcpy(s, "wss://", 6);
    memcpy(s + URL_MAX_LEN - 6, ":65535", 6);
    s[URL_MAX_LEN] = '\0';
    TEST_ASSERT_TRUE(url_parse(s, &u));
    TEST_ASSERT_GREATER_THAN(0, url_http(&u, "/api/device/pair/start", buf, sizeof(buf)));
    TEST_ASSERT_GREATER_THAN(0, url_ws(&u, buf, sizeof(buf)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_accepts_ipv4_and_hostnames);
    RUN_TEST(test_trailing_slash_is_stripped);
    RUN_TEST(test_rejects);
    RUN_TEST(test_length_limit);
    RUN_TEST(test_derived_urls);
    RUN_TEST(test_derived_url_errors);
    RUN_TEST(test_longest_url_fits_full_size);
    return UNITY_END();
}
