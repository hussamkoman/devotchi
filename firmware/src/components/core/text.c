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
