#include "paren.h"
#include "buffer.h"
#include "command.h"
#include "display.h"
#include "window.h"
#include <string.h>
#include <stdlib.h>

#define PAREN_STACK_MAX 1024

static char matching_opener(char closer) {
    switch (closer) {
        case ')': return '(';
        case ']': return '[';
        case '}': return '{';
        default:  return 0;
    }
}

static int is_opener(char c) { return c == '(' || c == '[' || c == '{'; }
static int is_closer(char c) { return c == ')' || c == ']' || c == '}'; }

ssize_t paren_scan_string(const char *s, size_t len, size_t closer_pos) {
    if (closer_pos >= len) return -1;
    char closer = s[closer_pos];
    char want_open = matching_opener(closer);
    if (!want_open) return -1;

    size_t stack[PAREN_STACK_MAX];
    char   kind [PAREN_STACK_MAX];
    int    top = 0;

    int in_string = 0;
    int in_comment = 0;

    for (size_t i = 0; i < closer_pos; i++) {
        char c = s[i];

        if (in_comment) {
            if (c == '\n') in_comment = 0;
            continue;
        }
        if (in_string) {
            if (c == '\\' && i + 1 < closer_pos) { i++; continue; }
            if (c == '"') in_string = 0;
            continue;
        }
        if (c == ';') { in_comment = 1; continue; }
        if (c == '"') { in_string = 1; continue; }
        if (c == '\\' && i + 1 < closer_pos) { i++; continue; } /* char literal */

        if (is_opener(c)) {
            if (top >= PAREN_STACK_MAX) return -1;
            stack[top] = i;
            kind [top] = c;
            top++;
        } else if (is_closer(c)) {
            if (top == 0) return -1;
            top--;
            if (kind[top] != matching_opener(c)) return -1;
        }
    }

    if (top == 0) return -1;
    top--;
    if (kind[top] != want_open) return -1;
    return (ssize_t)stack[top];
}

#ifndef PAREN_SCANNER_ONLY
void paren_flash_check(Buffer *buf, size_t closer_pos) {
    if (!buf || !buf->mode_name || strcmp(buf->mode_name, "Clojure") != 0) return;

    /* Copy buffer contents to a flat string for scanning.
     * Clojure files are small (~100KB max in practice); O(N) alloc is fine. */
    size_t len = buffer_length(buf);
    if (closer_pos >= len) return;
    char *s = malloc(len);
    if (!s) return;
    for (size_t i = 0; i < len; i++) s[i] = buffer_char_at(buf, i);

    ssize_t match = paren_scan_string(s, len, closer_pos);
    free(s);
    if (match < 0) return;

    /* Visibility: compute the byte offset of the window's top visible line
     * in the current buffer. If the match is before that, it's off-screen. */
    size_t top_pos = 0;
    for (int i = 0; i < current_window->top_line && top_pos < len; i++)
        top_pos = buffer_next_line_start(buf, top_pos);

    if ((size_t)match < top_pos) {
        /* Off-screen: echo the matching line in the minibuffer. */
        size_t line_start = (size_t)match;
        while (line_start > 0 && buffer_char_at(buf, line_start - 1) != '\n')
            line_start--;
        size_t line_end = buffer_line_end(buf, line_start);
        char line[256];
        /* trim leading whitespace */
        size_t j = 0;
        while (line_start + j < line_end && (buffer_char_at(buf, line_start + j) == ' '
               || buffer_char_at(buf, line_start + j) == '\t')) j++;
        size_t out = 0;
        for (size_t i = line_start + j; i < line_end && out < sizeof(line) - 1; i++)
            line[out++] = buffer_char_at(buf, i);
        line[out] = '\0';
        message("Matches %s", line);
    } else {
        display_flash_set(buf, (size_t)match, PAREN_FLASH_MS);
    }
}
#endif /* PAREN_SCANNER_ONLY */
