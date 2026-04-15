/* Pure C unit tests for font_lock engine. No SCI or ncurses.
 * Build: make font-lock-test  →  ./build/font_lock_test */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "../src/font_lock.h"
#include "../src/syntax.h"

static int failures = 0;

static void expect_spans(const char *label, LineHighlight got,
                         int expected_count, const SyntaxSpan *expected) {
    int ok = got.count == expected_count;
    for (int i = 0; ok && i < expected_count; i++) {
        if (got.spans[i].start  != expected[i].start)  ok = 0;
        if (got.spans[i].length != expected[i].length) ok = 0;
        if (got.spans[i].type   != expected[i].type)   ok = 0;
    }
    if (ok) {
        printf("  PASS %s\n", label);
    } else {
        printf("  FAIL %s\n", label);
        printf("    expected %d spans:\n", expected_count);
        for (int i = 0; i < expected_count; i++)
            printf("      [%d] start=%d len=%d type=%d\n",
                   i, expected[i].start, expected[i].length, expected[i].type);
        printf("    got %d spans:\n", got.count);
        for (int i = 0; i < got.count; i++)
            printf("      [%d] start=%d len=%d type=%d\n",
                   i, got.spans[i].start, got.spans[i].length, got.spans[i].type);
        failures++;
    }
}

/* Minimal mode table for testing, written as EDN and passed via the loader. */
static const char *MINI_MODES_EDN =
    "[{:name \"T_C\""
    " :syntax {:syntax-table {:comment-line \"//\" :comment-block [\"/*\" \"*/\"]"
    "                         :string-delims [\\\"] :string-escape \\\\}"
    "          :font-lock-keywords [{:words [\"if\" \"else\"] :face :keyword}"
    "                               [\"[[:<:]][[:digit:]]+[[:>:]]\" :number]"
    "                               [\"([[:alpha:]_][[:alnum:]_]*)[[:space:]]*\\\\(\" [:function]]]}}]";

static void test_line_comment(void) {
    font_lock_load_from_edn_string(MINI_MODES_EDN);
    const char *line = "int x; // comment";
    LineHighlight hl = font_lock_highlight_named("T_C", line, (int)strlen(line), 0);
    SyntaxSpan expect[] = { {7, 10, TOK_COMMENT} };
    expect_spans("line-comment", hl, 1, expect);
}

static void test_string_with_escape(void) {
    font_lock_load_from_edn_string(MINI_MODES_EDN);
    const char *line = "x = \"hi\\\"yo\";";
    LineHighlight hl = font_lock_highlight_named("T_C", line, (int)strlen(line), 0);
    /* line chars: x=0 ' '=1 '='=2 ' '=3 '"'=4 h=5 i=6 \\=7 "=8 y=9 o=10 "=11 ;=12
     * string starts at 4 and ends after the closing " at position 11.
     * Length = 11 - 4 + 1 = 8. */
    SyntaxSpan expect[] = { {4, 8, TOK_STRING} };
    expect_spans("string-with-escape", hl, 1, expect);
}

static void test_block_comment_single_line(void) {
    font_lock_load_from_edn_string(MINI_MODES_EDN);
    const char *line = "a /*x*/ b";
    LineHighlight hl = font_lock_highlight_named("T_C", line, (int)strlen(line), 0);
    SyntaxSpan expect[] = { {2, 5, TOK_COMMENT} };
    expect_spans("block-comment-single-line", hl, 1, expect);
}

static void test_block_comment_opens_state(void) {
    font_lock_load_from_edn_string(MINI_MODES_EDN);
    const char *line = "a /* open";
    LineHighlight hl = font_lock_highlight_named("T_C", line, (int)strlen(line), 0);
    SyntaxSpan expect[] = { {2, 7, TOK_COMMENT} };
    expect_spans("block-comment-opens-state", hl, 1, expect);
    if ((hl.state & FL_STATE_BLOCK_COMMENT) == 0) {
        printf("  FAIL block-comment-opens-state: state not set\n");
        failures++;
    }
}

static void test_block_comment_closes_state(void) {
    font_lock_load_from_edn_string(MINI_MODES_EDN);
    const char *line = "close */ then";
    LineHighlight hl = font_lock_highlight_named("T_C", line, (int)strlen(line),
                                                 FL_STATE_BLOCK_COMMENT);
    SyntaxSpan expect[] = { {0, 8, TOK_COMMENT} };
    expect_spans("block-comment-closes-state", hl, 1, expect);
    if (hl.state & FL_STATE_BLOCK_COMMENT) {
        printf("  FAIL block-comment-closes-state: state still set\n");
        failures++;
    }
}

static void test_words_keyword(void) {
    font_lock_load_from_edn_string(MINI_MODES_EDN);
    const char *line = "if (x)";
    LineHighlight hl = font_lock_highlight_named("T_C", line, (int)strlen(line), 0);
    SyntaxSpan expect[] = { {0, 2, TOK_KEYWORD} };
    expect_spans("words-keyword", hl, 1, expect);
}

static void test_pattern_face(void) {
    font_lock_load_from_edn_string(MINI_MODES_EDN);
    const char *line = "x = 42";
    LineHighlight hl = font_lock_highlight_named("T_C", line, (int)strlen(line), 0);
    SyntaxSpan expect[] = { {4, 2, TOK_NUMBER} };
    expect_spans("pattern-face", hl, 1, expect);
}

static void test_subgroup(void) {
    font_lock_load_from_edn_string(MINI_MODES_EDN);
    const char *line = "call(x)";
    LineHighlight hl = font_lock_highlight_named("T_C", line, (int)strlen(line), 0);
    SyntaxSpan expect[] = { {0, 4, TOK_FUNCTION} };
    expect_spans("subgroup", hl, 1, expect);
}

static void test_bad_regex_skipped(void) {
    const char *edn =
        "[{:name \"T_BAD\""
        " :syntax {:syntax-table {}"
        "          :font-lock-keywords [[\"[unclosed\" :keyword]"
        "                               [\"good\" :number]]}}]";
    font_lock_load_from_edn_string(edn);
    const char *line = "good stuff";
    LineHighlight hl = font_lock_highlight_named("T_BAD", line, (int)strlen(line), 0);
    SyntaxSpan expect[] = { {0, 4, TOK_NUMBER} };
    expect_spans("bad-regex-skipped", hl, 1, expect);
}

static void test_unknown_mode(void) {
    font_lock_load_from_edn_string(MINI_MODES_EDN);
    LineHighlight hl = font_lock_highlight_named("NOPE", "stuff", 5, 0);
    if (hl.count != 0) {
        printf("  FAIL unknown-mode: got %d spans, expected 0\n", hl.count);
        failures++;
    } else {
        printf("  PASS unknown-mode\n");
    }
}

static const char *MD_WITH_C_EDN =
    "[{:name \"C\""
    " :syntax {:syntax-table {:comment-line \"//\" :string-delims [\\\"] :string-escape \\\\}"
    "          :font-lock-keywords [{:words [\"int\"] :face :keyword}]}}"
    " {:name \"Markdown\""
    "  :syntax {:syntax-table {}"
    "           :font-lock-keywords [[\"^#.*$\" :heading1]]"
    "           :fence {:open \"^```([[:alnum:]]*)$\""
    "                   :close \"^```$\""
    "                   :lang-group 1"
    "                   :langs {\"c\" \"C\"}}}}]";

static void test_fence_open(void) {
    font_lock_load_from_edn_string(MD_WITH_C_EDN);
    const char *line = "```c";
    LineHighlight hl = font_lock_highlight_named("Markdown", line, (int)strlen(line), 0);
    if (!(hl.state & FL_STATE_CODE_FENCE)) {
        printf("  FAIL fence-open: state not set\n"); failures++;
    } else {
        printf("  PASS fence-open\n");
    }
    int idx = (hl.state & FL_FENCE_LANG_MASK) >> FL_FENCE_LANG_SHIFT;
    if (idx != LANG_C) {
        printf("  FAIL fence-open-lang: idx=%d expected LANG_C=%d\n", idx, LANG_C);
        failures++;
    } else {
        printf("  PASS fence-open-lang\n");
    }
}

static void test_fence_inner_highlight(void) {
    font_lock_load_from_edn_string(MD_WITH_C_EDN);
    int state = FL_STATE_CODE_FENCE | (LANG_C << FL_FENCE_LANG_SHIFT);
    const char *line = "int x;";
    LineHighlight hl = font_lock_highlight_named("Markdown", line, (int)strlen(line), state);
    bool found = false;
    for (int i = 0; i < hl.count; i++)
        if (hl.spans[i].type == TOK_KEYWORD) { found = true; break; }
    if (found) printf("  PASS fence-inner-highlight\n");
    else { printf("  FAIL fence-inner-highlight: no keyword span\n"); failures++; }
}

static void test_fence_close(void) {
    font_lock_load_from_edn_string(MD_WITH_C_EDN);
    int state = FL_STATE_CODE_FENCE | (LANG_C << FL_FENCE_LANG_SHIFT);
    const char *line = "```";
    LineHighlight hl = font_lock_highlight_named("Markdown", line, (int)strlen(line), state);
    if (hl.state & FL_STATE_CODE_FENCE) {
        printf("  FAIL fence-close: state still set\n"); failures++;
    } else {
        printf("  PASS fence-close\n");
    }
}

static void test_help_fallback(void) {
    const char *edn =
        "[{:name \"Help\" :syntax {:engine :builtin-help}}]";
    font_lock_load_from_edn_string(edn);
    const char *line = "clojure.core/map";
    LineHighlight hl = font_lock_highlight_named("Help", line, (int)strlen(line), 0);
    /* highlight_help_fallback recognises ns/name: non-indented line with dots and slashes.
     * It emits a single TOK_FUNCTION span covering the whole string. */
    bool found = false;
    for (int i = 0; i < hl.count; i++)
        if (hl.spans[i].type == TOK_FUNCTION) { found = true; break; }
    if (found) printf("  PASS help-fallback\n");
    else { printf("  FAIL help-fallback: no function span (got %d spans)\n", hl.count); failures++; }
}

int main(void) {
    puts("font_lock engine tests");
    test_line_comment();
    test_string_with_escape();
    test_block_comment_single_line();
    test_block_comment_opens_state();
    test_block_comment_closes_state();
    test_words_keyword();
    test_pattern_face();
    test_subgroup();
    test_bad_regex_skipped();
    test_unknown_mode();
    test_fence_open();
    test_fence_inner_highlight();
    test_fence_close();
    test_help_fallback();
    printf("\n%d failure(s)\n", failures);
    return failures > 0 ? 1 : 0;
}
