#include "pair_code.h"

#include <string.h>

const char PAIR_CODE_ALPHABET[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";

_Static_assert(sizeof(PAIR_CODE_ALPHABET) - 1 == PAIR_CODE_ALPHABET_LEN, "alphabet size");

static char upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool in_alphabet(char c)
{
    return c != '\0' && strchr(PAIR_CODE_ALPHABET, c) != NULL;
}

bool pair_code_is_valid(const char *s)
{
    if (s == NULL || strlen(s) != PAIR_CODE_LEN) {
        return false;
    }
    for (size_t i = 0; i < PAIR_CODE_LEN; i++) {
        if (!in_alphabet(upper(s[i]))) {
            return false;
        }
    }
    return true;
}

bool pair_code_normalise(const char *s, char out[PAIR_CODE_SIZE])
{
    out[0] = '\0';
    if (s == NULL) {
        return false;
    }
    size_t n = 0;
    for (; *s != '\0'; s++) {
        if (*s == ' ' || *s == '-') {
            continue;
        }
        const char c = upper(*s);
        if (n >= PAIR_CODE_LEN || !in_alphabet(c)) {
            out[0] = '\0';
            return false;
        }
        out[n++] = c;
    }
    out[n] = '\0';
    if (n != PAIR_CODE_LEN) {
        out[0] = '\0';
        return false;
    }
    return true;
}

bool pair_code_format(const char *code, char out[PAIR_CODE_DISPLAY_SIZE])
{
    out[0] = '\0';
    if (code == NULL || strlen(code) != PAIR_CODE_LEN) {
        return false;
    }
    memcpy(out, code, 3);
    out[3] = ' ';
    memcpy(out + 4, code + 3, 3);
    out[7] = '\0';
    return true;
}
