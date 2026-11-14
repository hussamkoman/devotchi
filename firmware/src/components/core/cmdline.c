#include "cmdline.h"

#include <stdbool.h>
#include <stddef.h>

static bool is_space(char c)
{
    return c == ' ' || c == '\t';
}

int cmdline_split(char *line, char **argv, int max_argv)
{
    int argc = 0;
    char *in = line;
    char *out = line;

    for (;;) {
        while (is_space(*in)) {
            in++;
        }
        if (*in == '\0') {
            break;
        }
        if (argc >= max_argv) {
            return -1;
        }
        /* out never passes in: unquoting only removes characters. */
        argv[argc++] = out;

        bool quoted = false;
        while (*in != '\0' && (quoted || !is_space(*in))) {
            if (*in == '"') {
                quoted = !quoted;
                in++;
            } else if (quoted && *in == '\\' && in[1] != '\0') {
                *out++ = in[1];
                in += 2;
            } else {
                *out++ = *in++;
            }
        }
        if (quoted) {
            return -1;
        }
        const bool at_end = (*in == '\0');
        *out++ = '\0';
        if (at_end) {
            break;
        }
        in++;
    }
    return argc;
}

const char *cmdline_rest(const char *line)
{
    while (is_space(*line)) {
        line++;
    }
    while (*line != '\0' && !is_space(*line)) {
        line++;
    }
    while (is_space(*line)) {
        line++;
    }
    return line;
}
