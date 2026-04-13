/* Standalone unit test for paren.c scanner. Does not link ncurses/sci.
 * Tests paren_find_match() against in-memory strings via a minimal
 * buffer-shim. Since paren.c uses Buffer from buffer.h, we instead
 * expose an internal string-scanning entry point for testing. */
#include "../src/paren.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Expose the pure string scanner from paren.c for testing. */
ssize_t paren_scan_string(const char *s, size_t len, size_t closer_pos);

static void expect(const char *s, size_t closer, ssize_t want, const char *label) {
    ssize_t got = paren_scan_string(s, strlen(s), closer);
    if (got != want) {
        fprintf(stderr, "FAIL %s: want %zd got %zd (input=%s closer=%zu)\n",
                label, want, got, s, closer);
        exit(1);
    }
    printf("ok %s\n", label);
}

int main(void) {
    /* Basic match */
    expect("()",       1, 0,  "basic-paren");
    expect("[]",       1, 0,  "basic-bracket");
    expect("{}",       1, 0,  "basic-brace");
    expect("(a(b)c)",  6, 0,  "nested-outer");
    expect("(a(b)c)",  4, 2,  "nested-inner");

    /* Mismatched kinds */
    expect("(]",       1, -1, "mismatch-kind");
    expect("[)",       1, -1, "mismatch-kind2");

    /* Unbalanced */
    expect(")",        0, -1, "no-opener");
    expect("a)",       1, -1, "no-opener2");

    /* String-aware: brackets inside "..." must be ignored */
    expect("\"(\")",    3, -1, "paren-in-string-ignored");
    expect("(\")\")",   4, 0,  "close-paren-in-string-ignored");

    /* Escaped quote in string */
    expect("(\"\\\")\")",  6, 0, "escaped-quote-in-string");

    /* Comment-aware: ; to end-of-line is ignored */
    expect("(;)\n)",    4, 0,  "comment-paren-ignored");
    expect("(a ; )\n)", 7, 0,  "comment-trailing-ignored");

    /* Char literal \( should not be an opener */
    expect("\\()",      2, -1, "char-literal-paren");
    expect("(\\))",     3, 0,  "char-literal-close-paren");

    printf("all paren tests passed\n");
    return 0;
}
