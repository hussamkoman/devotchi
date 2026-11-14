#pragma once

#include <stddef.h>

/**
 * Rewrites src as printable ASCII into dst (always NUL-terminated when cap > 0):
 * every non-ASCII code point or malformed byte becomes one '?', control
 * characters except '\n' become '?'. Returns the length written.
 */
size_t text_sanitize_ascii(char *dst, size_t cap, const char *src);

/**
 * Copies as many whole UTF-8 code points of src as fit into dst (always
 * NUL-terminated when cap > 0). Returns the number of bytes written.
 */
size_t text_utf8_truncate(char *dst, size_t cap, const char *src);
