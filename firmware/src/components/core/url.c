#include "url.h"

#include <stdio.h>
#include <string.h>

#define PORT_WS   80
#define PORT_WSS  443

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool has_prefix_nocase(const char *s, const char *prefix)
{
    for (; *prefix != '\0'; s++, prefix++) {
        if (lower(*s) != *prefix) {
            return false;
        }
    }
    return true;
}

static bool is_host_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

bool url_parse(const char *s, url_t *out)
{
    if (s == NULL || out == NULL) {
        return false;
    }
    size_t len = strlen(s);
    if (len > URL_MAX_LEN) {
        return false;
    }

    bool tls;
    const char *p;
    if (has_prefix_nocase(s, "wss://")) {
        tls = true;
        p = s + 6;
    } else if (has_prefix_nocase(s, "ws://")) {
        tls = false;
        p = s + 5;
    } else {
        return false;
    }
    const char *end = s + len;
    if (end > p && end[-1] == '/') {
        end--;
    }

    const char *host_end = p;
    while (host_end < end && is_host_char(*host_end)) {
        host_end++;
    }
    const size_t host_len = (size_t)(host_end - p);
    if (host_len == 0 || p[0] == '.' || p[0] == '-' || host_end[-1] == '.' || host_end[-1] == '-') {
        return false;
    }
    for (const char *c = p; c + 1 < host_end; c++) {
        if (c[0] == '.' && c[1] == '.') {
            return false;
        }
    }

    uint32_t port = tls ? PORT_WSS : PORT_WS;
    if (host_end < end) {
        if (*host_end != ':') {
            return false;
        }
        const char *d = host_end + 1;
        if (d == end || end - d > 5) {
            return false;
        }
        port = 0;
        for (; d < end; d++) {
            if (*d < '0' || *d > '9') {
                return false;
            }
            port = port * 10 + (uint32_t)(*d - '0');
        }
        if (port == 0 || port > 65535) {
            return false;
        }
    }

    out->tls = tls;
    out->port = (uint16_t)port;
    for (size_t i = 0; i < host_len; i++) {
        out->host[i] = lower(p[i]);
    }
    out->host[host_len] = '\0';
    return true;
}

static int format(const url_t *u, const char *scheme, const char *path, char *buf, size_t cap)
{
    if (u == NULL || buf == NULL || cap == 0) {
        return -1;
    }
    int n;
    if (u->port == (u->tls ? PORT_WSS : PORT_WS)) {
        n = snprintf(buf, cap, "%s://%s%s", scheme, u->host, path);
    } else {
        n = snprintf(buf, cap, "%s://%s:%u%s", scheme, u->host, (unsigned)u->port, path);
    }
    if (n < 0 || (size_t)n >= cap) {
        buf[0] = '\0';
        return -1;
    }
    return n;
}

int url_base(const url_t *u, char *buf, size_t cap)
{
    return format(u, (u != NULL && u->tls) ? "wss" : "ws", "", buf, cap);
}

int url_ws(const url_t *u, char *buf, size_t cap)
{
    return format(u, (u != NULL && u->tls) ? "wss" : "ws", "/ws", buf, cap);
}

int url_http(const url_t *u, const char *path, char *buf, size_t cap)
{
    if (path == NULL || path[0] != '/') {
        return -1;
    }
    return format(u, (u != NULL && u->tls) ? "https" : "http", path, buf, cap);
}
