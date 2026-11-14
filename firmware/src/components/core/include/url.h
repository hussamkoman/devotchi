#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Longest accepted base URL in bytes (protocol section 10). */
#define URL_MAX_LEN     127
#define URL_BASE_SIZE   (URL_MAX_LEN + 1)
#define URL_HOST_SIZE   (URL_MAX_LEN + 1)
/** Room for a base URL turned into https:// plus the longest REST path the firmware uses. */
#define URL_FULL_SIZE   192

typedef struct {
    bool tls;
    uint16_t port;
    char host[URL_HOST_SIZE];
} url_t;

/**
 * Accepts only ws://host[:port] or wss://host[:port] (one trailing slash is
 * tolerated): no path, query, fragment or userinfo, at most URL_MAX_LEN bytes.
 * The port defaults to 80 or 443.
 */
bool url_parse(const char *s, url_t *out);

/** Writes the normalised base URL (default port omitted); returns its length or -1. */
int url_base(const url_t *u, char *buf, size_t cap);

/** Writes <base>/ws; returns its length or -1 when it does not fit. */
int url_ws(const url_t *u, char *buf, size_t cap);

/** Writes http(s)://host[:port]<path>; path starts with '/'. Returns the length or -1. */
int url_http(const url_t *u, const char *path, char *buf, size_t cap);
