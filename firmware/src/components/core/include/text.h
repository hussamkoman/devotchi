#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Rewrites src as printable ASCII into dst (always NUL-terminated when cap > 0):
 * every non-ASCII code point or malformed byte becomes one '?', control
 * characters except '\n' become '?'. Returns the length written.
 */
size_t text_sanitize_ascii(char *dst, size_t cap, const char *src);

/**
 * Rewrites src into dst as UTF-8 that the device fonts can draw (always
 * NUL-terminated when cap > 0): '\n', U+0020..U+007E, U+00A0..U+00FF and
 * the marks of TEXT_DEVICE_EXTRA are kept; every other code point and every
 * malformed run becomes one '?'. U+00AD (soft hyphen) is invisible and has no
 * glyph, so it is dropped. A code point that does not fit ends the output.
 * Returns the length written in bytes.
 */
size_t text_sanitize_device(char *dst, size_t cap, const char *src);

/** True for a code point text_sanitize_device() keeps. */
bool text_device_has(uint32_t cp);

/** Number of code points in src; each malformed byte counts as one. */
size_t text_utf8_len(const char *src);

/**
 * Upper-cases ASCII and Latin-1 letters (U+00E0..U+00FE except U+00F7) into
 * dst, for the caps text style; everything else is copied unchanged.
 * Returns the length written in bytes.
 */
size_t text_upper_device(char *dst, size_t cap, const char *src);

/**
 * Copies src into dst; when it has more than max_cols code points it keeps
 * the start and the end and puts one U+2026 (ellipsis) between them, so a
 * host or a path keeps both ends. Returns the length written in bytes.
 */
size_t text_truncate_middle(char *dst, size_t cap, const char *src, size_t max_cols);

/**
 * Copies as many whole UTF-8 code points of src as fit into dst (always
 * NUL-terminated when cap > 0). Returns the number of bytes written.
 */
size_t text_utf8_truncate(char *dst, size_t cap, const char *src);

/**
 * Number of display lines src takes when wrapped to cols columns: '\n' ends a
 * line, a longer line breaks after its last space within cols, or at cols when
 * it has none. Columns count code points (a monospace font draws each in one
 * cell) and a break never splits a UTF-8 sequence. An empty src has no lines.
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
