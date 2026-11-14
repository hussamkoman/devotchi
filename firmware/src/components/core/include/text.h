#pragma once

#include <stdbool.h>
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

/**
 * Number of display lines src takes when wrapped to cols columns: '\n' ends a
 * line, a longer line breaks after its last space within cols, or at cols when
 * it has none. An empty src has no lines.
 */
size_t text_wrap_lines(const char *src, size_t cols);

/** Pages of lines_per_page wrapped lines; 0 for an empty src. */
size_t text_wrap_pages(const char *src, size_t cols, size_t lines_per_page);

/**
 * Writes the wrapped lines of page (0-based) into dst, joined by '\n' (always
 * NUL-terminated when cap > 0). Returns false for a page past the end or a
 * page that does not fit into cap.
 */
bool text_wrap_page(char *dst, size_t cap, const char *src, size_t cols, size_t lines_per_page, size_t page);
