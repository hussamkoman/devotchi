#pragma once

/**
 * Splits a command line in place into space-separated arguments. Double
 * quotes group words; inside quotes a backslash escapes the next character.
 * Returns argc, or -1 for an unterminated quote or more than max_argv
 * arguments.
 */
int cmdline_split(char *line, char **argv, int max_argv);

/** Returns the text after the first word of line with leading spaces skipped. */
const char *cmdline_rest(const char *line);
