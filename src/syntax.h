#ifndef SYNTAX_H
#define SYNTAX_H

#include <stddef.h>

/* Syntax token types mapped to color pairs in display.h */
typedef enum {
    TOK_NORMAL = 0,
    TOK_KEYWORD,
    TOK_STRING,
    TOK_COMMENT,
    TOK_TYPE,
    TOK_FUNCTION,
    TOK_NUMBER,
    TOK_PREPROC,
    TOK_HEADING1,
    TOK_HEADING2,
    TOK_HEADING3,
    TOK_LINK,
    TOK_CODE,
    TOK_BOLD,
    TOK_ITALIC,
    TOK_DIFF_ADD,
    TOK_DIFF_DEL,
    TOK_DIFF_HEADER,
    TOK_MATH,
    TOK_MATH_DELIM,
} TokenType;

/* A colored span within a line */
typedef struct {
    int start;      /* column offset */
    int length;
    TokenType type;
} SyntaxSpan;

#define MAX_SPANS 128

typedef struct {
    SyntaxSpan spans[MAX_SPANS];
    int count;
    int state;      /* carry-over state for multiline constructs */
} LineHighlight;

/* Syntax language IDs */
typedef enum {
    LANG_NONE = 0,
    LANG_C,
    LANG_CLOJURE,
    LANG_BASH,
    LANG_MARKDOWN,
    LANG_DIFF,
    LANG_HELP,
    LANG_MAKEFILE,
} SyntaxLang;

/* Highlight a single line. state_in carries multiline state from previous line.
 * Returns highlight info including state_out for next line. */
LineHighlight syntax_highlight_line(SyntaxLang lang, const char *line, int len, int state_in);

/* Detect language from filename extension */
SyntaxLang syntax_detect(const char *filename);

#endif
