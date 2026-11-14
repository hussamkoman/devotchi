#pragma once

#include <stdbool.h>
#include <stddef.h>

#define PAIR_CODE_LEN          6
#define PAIR_CODE_SIZE         (PAIR_CODE_LEN + 1)
/** "K7M 2QX" and its terminator. */
#define PAIR_CODE_DISPLAY_SIZE (PAIR_CODE_LEN + 2)

/** The 31 symbols a pairing code or a provisioning proof-of-possession is drawn from. */
extern const char PAIR_CODE_ALPHABET[];
#define PAIR_CODE_ALPHABET_LEN 31

/** True for exactly PAIR_CODE_LEN alphabet characters, in either case. */
bool pair_code_is_valid(const char *s);

/** Upper-cases s into out, skipping spaces and hyphens; false when the result is not a valid code. */
bool pair_code_normalise(const char *s, char out[PAIR_CODE_SIZE]);

/** Writes the code as two groups ("K7M 2QX"); false when code is not PAIR_CODE_LEN characters long. */
bool pair_code_format(const char *code, char out[PAIR_CODE_DISPLAY_SIZE]);
