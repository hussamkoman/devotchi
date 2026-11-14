#include "text.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Length of the well-formed UTF-8 sequence at s (RFC 3629: no overlongs, no
 * surrogates, nothing above U+10FFFF), or 0 when the bytes at s are not one.
 */
static size_t utf8_sequence_len(const unsigned char *s)
{
    const unsigned char b0 = s[0];
    size_t len;
    unsigned char lo = 0x80;
    unsigned char hi = 0xBF;

    if (b0 < 0x80) {
        return 1;
    } else if (b0 >= 0xC2 && b0 <= 0xDF) {
        len = 2;
    } else if (b0 >= 0xE0 && b0 <= 0xEF) {
        len = 3;
        if (b0 == 0xE0) {
            lo = 0xA0;
        } else if (b0 == 0xED) {
            hi = 0x9F;
        }
    } else if (b0 >= 0xF0 && b0 <= 0xF4) {
        len = 4;
        if (b0 == 0xF0) {
            lo = 0x90;
        } else if (b0 == 0xF4) {
            hi = 0x8F;
        }
    } else {
        return 0;
    }

    if (s[1] < lo || s[1] > hi) {
        return 0;
    }
    for (size_t i = 2; i < len; i++) {
        if (s[i] < 0x80 || s[i] > 0xBF) {
            return 0;
        }
    }
    return len;
}

static bool is_continuation(unsigned char b)
{
    return (b & 0xC0) == 0x80;
}

size_t text_sanitize_ascii(char *dst, size_t cap, const char *src)
{
    if (dst == NULL || cap == 0) {
        return 0;
    }
    const unsigned char *s = (const unsigned char *)(src != NULL ? src : "");
    size_t out = 0;

    while (*s != '\0' && out + 1 < cap) {
        const size_t len = utf8_sequence_len(s);
        if (len == 1) {
            const bool printable = (*s >= 0x20 && *s < 0x7F) || *s == '\n';
            dst[out++] = printable ? (char)*s : '?';
            s++;
        } else if (len > 1) {
            dst[out++] = '?';
            s += len;
        } else {
            /* One '?' for the whole malformed run, as a decoder would show it. */
            dst[out++] = '?';
            s++;
            while (is_continuation(*s)) {
                s++;
            }
        }
    }
    dst[out] = '\0';
    return out;
}

size_t text_utf8_truncate(char *dst, size_t cap, const char *src)
{
    if (dst == NULL || cap == 0) {
        return 0;
    }
    const unsigned char *s = (const unsigned char *)(src != NULL ? src : "");
    size_t out = 0;

    while (*s != '\0') {
        size_t len = utf8_sequence_len(s);
        if (len == 0) {
            /* Malformed bytes are copied one at a time; they cannot be split further. */
            len = 1;
        }
        if (out + len >= cap) {
            break;
        }
        for (size_t i = 0; i < len; i++) {
            dst[out++] = (char)s[i];
        }
        s += len;
    }
    dst[out] = '\0';
    return out;
}

/*
 * Finds the wrapped line starting at p: *len is its length without the break,
 * the return value is where the next line starts (or NULL after the last one).
 */
static const char *wrap_line(const char *p, size_t cols, size_t *len)
{
    if (p == NULL || *p == '\0' || cols == 0) {
        *len = 0;
        return NULL;
    }
    size_t n = 0;
    while (n < cols && p[n] != '\0' && p[n] != '\n') {
        n++;
    }
    if (p[n] == '\0') {
        *len = n;
        return NULL;
    }
    if (p[n] == '\n') {
        *len = n;
        return p[n + 1] != '\0' ? p + n + 1 : NULL;
    }
    /* The line is longer than cols: a space right at the limit is a clean break. */
    if (p[n] == ' ') {
        *len = n;
    } else {
        size_t space = n;
        while (space > 0 && p[space - 1] != ' ') {
            space--;
        }
        /* Only leading spaces before the limit: breaking there would waste a line. */
        if (space > 1) {
            n = space - 1;
        }
        *len = n;
    }
    const char *next = p + n;
    while (*next == ' ') {
        next++;
    }
    return *next != '\0' ? next : NULL;
}

size_t text_wrap_lines(const char *src, size_t cols)
{
    size_t lines = 0;
    const char *p = src;
    while (p != NULL && *p != '\0' && cols > 0) {
        size_t len;
        p = wrap_line(p, cols, &len);
        lines++;
    }
    return lines;
}

size_t text_wrap_pages(const char *src, size_t cols, size_t lines_per_page)
{
    if (lines_per_page == 0) {
        return 0;
    }
    return (text_wrap_lines(src, cols) + lines_per_page - 1) / lines_per_page;
}

bool text_wrap_page(char *dst, size_t cap, const char *src, size_t cols, size_t lines_per_page, size_t page)
{
    if (dst == NULL || cap == 0) {
        return false;
    }
    dst[0] = '\0';
    if (page >= text_wrap_pages(src, cols, lines_per_page)) {
        return false;
    }
    const char *p = src;
    for (size_t skip = page * lines_per_page; skip > 0 && p != NULL; skip--) {
        size_t len;
        p = wrap_line(p, cols, &len);
    }
    size_t used = 0;
    for (size_t i = 0; i < lines_per_page && p != NULL && *p != '\0'; i++) {
        size_t len;
        const char *line = p;
        p = wrap_line(p, cols, &len);
        const size_t need = len + (i > 0 ? 1 : 0);
        if (used + need >= cap) {
            dst[0] = '\0';
            return false;
        }
        if (i > 0) {
            dst[used++] = '\n';
        }
        for (size_t k = 0; k < len; k++) {
            dst[used++] = line[k];
        }
        dst[used] = '\0';
    }
    return true;
}
