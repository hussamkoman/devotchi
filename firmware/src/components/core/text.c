#include "text.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

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

/* Device charset beyond ASCII and Latin-1 (protocol 14.1). */
static const uint16_t DEVICE_EXTRA[] = {
    0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026,
    0x2190, 0x2191, 0x2192, 0x2193, 0x2212, 0x2713,
};

#define SOFT_HYPHEN 0xADu

static uint32_t utf8_decode(const unsigned char *s, size_t len)
{
    switch (len) {
    case 1:
        return s[0];
    case 2:
        return ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F);
    case 3:
        return ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    default:
        return ((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) | ((uint32_t)(s[2] & 0x3F) << 6) |
               (s[3] & 0x3F);
    }
}

/* Length of the next code point or malformed run at s: what one '?' replaces. */
static size_t next_len(const unsigned char *s, bool *malformed)
{
    const size_t len = utf8_sequence_len(s);
    if (len > 0) {
        *malformed = false;
        return len;
    }
    *malformed = true;
    size_t n = 1;
    while (is_continuation(s[n])) {
        n++;
    }
    return n;
}

bool text_device_has(uint32_t cp)
{
    if (cp == '\n' || (cp >= 0x20 && cp < 0x7F) || (cp >= 0xA0 && cp <= 0xFF && cp != SOFT_HYPHEN)) {
        return true;
    }
    for (size_t i = 0; i < sizeof(DEVICE_EXTRA) / sizeof(DEVICE_EXTRA[0]); i++) {
        if (DEVICE_EXTRA[i] == cp) {
            return true;
        }
    }
    return false;
}

size_t text_sanitize_device(char *dst, size_t cap, const char *src)
{
    if (dst == NULL || cap == 0) {
        return 0;
    }
    /* src may be dst: the output never runs ahead of the input. */
    const unsigned char *s = (const unsigned char *)(src != NULL ? src : "");
    size_t out = 0;
    while (*s != '\0') {
        bool malformed;
        const size_t len = next_len(s, &malformed);
        const uint32_t cp = malformed ? 0 : utf8_decode(s, len);
        if (!malformed && cp == SOFT_HYPHEN) {
            s += len;
            continue;
        }
        const bool keep = !malformed && text_device_has(cp);
        const size_t need = keep ? len : 1;
        if (out + need >= cap) {
            break;
        }
        if (keep) {
            memmove(dst + out, s, len);
        } else {
            dst[out] = '?';
        }
        out += need;
        s += len;
    }
    dst[out] = '\0';
    return out;
}

size_t text_utf8_len(const char *src)
{
    const unsigned char *s = (const unsigned char *)(src != NULL ? src : "");
    size_t n = 0;
    while (*s != '\0') {
        const size_t len = utf8_sequence_len(s);
        s += len > 0 ? len : 1;
        n++;
    }
    return n;
}

size_t text_upper_device(char *dst, size_t cap, const char *src)
{
    if (dst == NULL || cap == 0) {
        return 0;
    }
    const unsigned char *s = (const unsigned char *)(src != NULL ? src : "");
    size_t out = 0;
    while (*s != '\0') {
        size_t len = utf8_sequence_len(s);
        if (len == 0) {
            len = 1;
        }
        if (out + len >= cap) {
            break;
        }
        for (size_t i = 0; i < len; i++) {
            dst[out + i] = (char)s[i];
        }
        if (len == 1 && s[0] >= 'a' && s[0] <= 'z') {
            dst[out] = (char)(s[0] - 'a' + 'A');
        } else if (len == 2 && s[0] == 0xC3 && s[1] >= 0xA0 && s[1] <= 0xBE && s[1] != 0xB7) {
            /* U+00E0..U+00FE map to U+00C0..U+00DE; U+00F7 is the division sign. */
            dst[out + 1] = (char)(s[1] - 0x20);
        }
        out += len;
        s += len;
    }
    dst[out] = '\0';
    return out;
}

/* Byte offset of code point index n in s (or the length of s when it is shorter). */
static size_t offset_of(const unsigned char *s, size_t n)
{
    size_t off = 0;
    while (n > 0 && s[off] != '\0') {
        const size_t len = utf8_sequence_len(s + off);
        off += len > 0 ? len : 1;
        n--;
    }
    return off;
}

size_t text_truncate_middle(char *dst, size_t cap, const char *src, size_t max_cols)
{
    if (dst == NULL || cap == 0) {
        return 0;
    }
    const char *in = src != NULL ? src : "";
    const size_t cols = text_utf8_len(in);
    if (cols <= max_cols || max_cols < 3) {
        return text_utf8_truncate(dst, cap, in);
    }
    static const char ELLIPSIS[] = "\xE2\x80\xA6";
    const size_t head_cols = max_cols / 2;
    const size_t tail_cols = max_cols - 1 - head_cols;
    const unsigned char *s = (const unsigned char *)in;
    const size_t head = offset_of(s, head_cols);
    const size_t tail = offset_of(s, cols - tail_cols);
    const size_t tail_len = strlen(in + tail);
    if (head + sizeof(ELLIPSIS) - 1 + tail_len >= cap) {
        return text_utf8_truncate(dst, cap, in);
    }
    memmove(dst, in, head);
    memcpy(dst + head, ELLIPSIS, sizeof(ELLIPSIS) - 1);
    memmove(dst + head + sizeof(ELLIPSIS) - 1, in + tail, tail_len + 1);
    return head + sizeof(ELLIPSIS) - 1 + tail_len;
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
    const unsigned char *u = (const unsigned char *)p;
    size_t n = 0;
    for (size_t col = 0; col < cols && p[n] != '\0' && p[n] != '\n'; col++) {
        const size_t step = utf8_sequence_len(u + n);
        n += step > 0 ? step : 1;
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

size_t text_wrap_lines_first(const char *src, size_t cols)
{
    size_t lines = 0;
    const char *p = src;
    while (p != NULL && *p != '\0' && cols > 0) {
        size_t len;
        const char *line = p;
        p = wrap_line(p, cols, &len);
        lines++;
        if (line[len] == '\n' || line[len] == '\0') {
            break;
        }
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
