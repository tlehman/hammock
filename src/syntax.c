#include "syntax.h"
#include "util.h"
#include <string.h>
#include <ctype.h>

/* Multiline state flags */
#define STATE_NORMAL        0
#define STATE_BLOCK_COMMENT 1
#define STATE_STRING        2
#define STATE_CODE_FENCE    4
/* Bits 3-5 store the fenced code language (SyntaxLang enum, 0-6) */
#define FENCE_LANG_SHIFT    3
#define FENCE_LANG_MASK     0x38  /* bits 3-5 */

static void add_span(LineHighlight *hl, int start, int length, TokenType type) {
    if (hl->count >= MAX_SPANS) return;
    hl->spans[hl->count].start = start;
    hl->spans[hl->count].length = length;
    hl->spans[hl->count].type = type;
    hl->count++;
}

static bool is_word_boundary(char c) {
    return !isalnum(c) && c != '_';
}

static bool match_keyword(const char *line, int pos, int len, const char *kw) {
    int kwlen = (int)strlen(kw);
    if (pos + kwlen > len) return false;
    if (memcmp(line + pos, kw, (size_t)kwlen) != 0) return false;
    /* Check word boundary before */
    if (pos > 0 && !is_word_boundary(line[pos - 1])) return false;
    /* Check word boundary after */
    if (pos + kwlen < len && !is_word_boundary(line[pos + kwlen])) return false;
    return true;
}

/* ---- C syntax ---- */

static const char *c_keywords[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "inline", "int", "long", "register", "return", "short", "signed",
    "sizeof", "static", "struct", "switch", "typedef", "typeof", "union",
    "unsigned", "void", "volatile", "while",
    "bool", "true", "false", "NULL",
    "int8_t", "int16_t", "int32_t", "int64_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "size_t", "ssize_t", "pid_t",
    NULL
};

static const char *c_types[] = {
    "FILE", "DIR", "va_list",
    NULL
};

static LineHighlight highlight_c(const char *line, int len, int state_in) {
    LineHighlight hl = {.count = 0, .state = state_in};
    int i = 0;

    /* Continue block comment from previous line */
    if (state_in & STATE_BLOCK_COMMENT) {
        int start = 0;
        while (i < len - 1) {
            if (line[i] == '*' && line[i + 1] == '/') {
                add_span(&hl, start, i + 2 - start, TOK_COMMENT);
                i += 2;
                hl.state &= ~STATE_BLOCK_COMMENT;
                goto normal;
            }
            i++;
        }
        add_span(&hl, start, len - start, TOK_COMMENT);
        return hl;
    }

normal:
    while (i < len) {
        /* Preprocessor directive */
        if (line[i] == '#' && (i == 0 || (i > 0 && line[i-1] == '\0'))) {
            /* Check if line starts with # (skip whitespace) */
            int j = 0;
            while (j < i && isspace(line[j])) j++;
            if (j == i) {
                add_span(&hl, i, len - i, TOK_PREPROC);
                return hl;
            }
        }

        /* Line comment */
        if (i < len - 1 && line[i] == '/' && line[i + 1] == '/') {
            add_span(&hl, i, len - i, TOK_COMMENT);
            return hl;
        }

        /* Block comment */
        if (i < len - 1 && line[i] == '/' && line[i + 1] == '*') {
            int start = i;
            i += 2;
            while (i < len - 1) {
                if (line[i] == '*' && line[i + 1] == '/') {
                    add_span(&hl, start, i + 2 - start, TOK_COMMENT);
                    i += 2;
                    goto normal;
                }
                i++;
            }
            add_span(&hl, start, len - start, TOK_COMMENT);
            hl.state |= STATE_BLOCK_COMMENT;
            return hl;
        }

        /* String */
        if (line[i] == '"') {
            int start = i;
            i++;
            while (i < len && line[i] != '"') {
                if (line[i] == '\\') i++;
                i++;
            }
            if (i < len) i++;
            add_span(&hl, start, i - start, TOK_STRING);
            continue;
        }

        /* Character literal */
        if (line[i] == '\'') {
            int start = i;
            i++;
            if (i < len && line[i] == '\\') i++;
            if (i < len) i++;
            if (i < len && line[i] == '\'') i++;
            add_span(&hl, start, i - start, TOK_STRING);
            continue;
        }

        /* Number */
        if (isdigit(line[i]) || (line[i] == '.' && i + 1 < len && isdigit(line[i + 1]))) {
            if (i == 0 || is_word_boundary(line[i - 1])) {
                int start = i;
                if (line[i] == '0' && i + 1 < len && (line[i + 1] == 'x' || line[i + 1] == 'X'))
                    i += 2;
                while (i < len && (isxdigit(line[i]) || line[i] == '.' ||
                       line[i] == 'e' || line[i] == 'E' ||
                       line[i] == 'u' || line[i] == 'U' ||
                       line[i] == 'l' || line[i] == 'L'))
                    i++;
                add_span(&hl, start, i - start, TOK_NUMBER);
                continue;
            }
        }

        /* Keywords */
        if (isalpha(line[i]) || line[i] == '_') {
            bool found = false;
            for (const char **kw = c_keywords; *kw; kw++) {
                if (match_keyword(line, i, len, *kw)) {
                    int kwlen = (int)strlen(*kw);
                    add_span(&hl, i, kwlen, TOK_KEYWORD);
                    i += kwlen;
                    found = true;
                    break;
                }
            }
            if (!found) {
                for (const char **tp = c_types; *tp; tp++) {
                    if (match_keyword(line, i, len, *tp)) {
                        int tplen = (int)strlen(*tp);
                        add_span(&hl, i, tplen, TOK_TYPE);
                        i += tplen;
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                /* Check if followed by ( for function call */
                int start = i;
                while (i < len && (isalnum(line[i]) || line[i] == '_')) i++;
                if (i < len && line[i] == '(') {
                    add_span(&hl, start, i - start, TOK_FUNCTION);
                }
                continue;
            }
            continue;
        }

        i++;
    }
    return hl;
}

/* ---- Clojure syntax ---- */

static const char *clj_keywords[] = {
    "def", "defn", "defn-", "defmacro", "defonce", "defmulti", "defmethod",
    "defprotocol", "defrecord", "deftype", "defstruct",
    "fn", "let", "loop", "recur", "do", "if", "if-let", "if-not",
    "when", "when-let", "when-not", "when-first",
    "cond", "condp", "case", "cond->", "cond->>",
    "for", "doseq", "dotimes", "while",
    "try", "catch", "finally", "throw",
    "ns", "require", "import", "use", "refer",
    "atom", "deref", "swap!", "reset!", "compare-and-set!",
    "apply", "map", "filter", "reduce", "into", "comp", "partial",
    "first", "rest", "cons", "conj", "assoc", "dissoc", "get",
    "str", "println", "prn", "pr-str",
    "nil", "true", "false",
    "->", "->>", "as->", "some->", "some->>",
    NULL
};

static LineHighlight highlight_clojure(const char *line, int len, int state_in) {
    LineHighlight hl = {.count = 0, .state = state_in};
    int i = 0;

    /* Continue string from previous line */
    if (state_in & STATE_STRING) {
        int start = 0;
        while (i < len) {
            if (line[i] == '"') {
                add_span(&hl, start, i + 1 - start, TOK_STRING);
                hl.state &= ~STATE_STRING;
                i++;
                goto clj_normal;
            }
            if (line[i] == '\\') i++;
            i++;
        }
        add_span(&hl, start, len, TOK_STRING);
        return hl;
    }

clj_normal:
    while (i < len) {
        /* Comment */
        if (line[i] == ';') {
            add_span(&hl, i, len - i, TOK_COMMENT);
            return hl;
        }

        /* String */
        if (line[i] == '"') {
            int start = i;
            i++;
            while (i < len && line[i] != '"') {
                if (line[i] == '\\') i++;
                i++;
            }
            if (i < len) {
                i++;
                add_span(&hl, start, i - start, TOK_STRING);
            } else {
                add_span(&hl, start, len - start, TOK_STRING);
                hl.state |= STATE_STRING;
            }
            continue;
        }

        /* Regex literal #"..." */
        if (line[i] == '#' && i + 1 < len && line[i + 1] == '"') {
            int start = i;
            i += 2;
            while (i < len && line[i] != '"') {
                if (line[i] == '\\') i++;
                i++;
            }
            if (i < len) i++;
            add_span(&hl, start, i - start, TOK_STRING);
            continue;
        }

        /* Keyword :something */
        if (line[i] == ':' && i + 1 < len && (isalpha(line[i + 1]) || line[i + 1] == '_')) {
            int start = i;
            i++;
            while (i < len && (isalnum(line[i]) || line[i] == '-' || line[i] == '_' ||
                   line[i] == '.' || line[i] == '/' || line[i] == ':'))
                i++;
            add_span(&hl, start, i - start, TOK_TYPE);
            continue;
        }

        /* Number */
        if (isdigit(line[i]) || (line[i] == '-' && i + 1 < len && isdigit(line[i + 1]) &&
            (i == 0 || is_word_boundary(line[i - 1])))) {
            int start = i;
            if (line[i] == '-') i++;
            while (i < len && (isdigit(line[i]) || line[i] == '.' ||
                   line[i] == 'e' || line[i] == 'E' ||
                   line[i] == 'N' || line[i] == 'M' ||
                   line[i] == 'r' || line[i] == 'x'))
                i++;
            add_span(&hl, start, i - start, TOK_NUMBER);
            continue;
        }

        /* Symbols - check for keywords */
        if (isalpha(line[i]) || line[i] == '_' || line[i] == '-' ||
            line[i] == '+' || line[i] == '*' || line[i] == '!') {
            bool found = false;
            for (const char **kw = clj_keywords; *kw; kw++) {
                if (match_keyword(line, i, len, *kw)) {
                    int kwlen = (int)strlen(*kw);
                    /* Clojure word boundaries include - */
                    if (i + kwlen < len && (isalnum(line[i + kwlen]) ||
                        line[i + kwlen] == '-' || line[i + kwlen] == '_' ||
                        line[i + kwlen] == '!' || line[i + kwlen] == '?')) {
                        /* Not a boundary, skip */
                    } else {
                        add_span(&hl, i, kwlen, TOK_KEYWORD);
                        i += kwlen;
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                while (i < len && !isspace(line[i]) && line[i] != ')' &&
                       line[i] != ']' && line[i] != '}' && line[i] != '(' &&
                       line[i] != '[' && line[i] != '{')
                    i++;
            }
            continue;
        }

        /* Set literal #{, deref @, quote ' ` */
        i++;
    }
    return hl;
}

/* ---- Bash syntax ---- */

static const char *bash_keywords[] = {
    "if", "then", "else", "elif", "fi",
    "for", "in", "do", "done",
    "while", "until",
    "case", "esac",
    "function", "return", "exit",
    "local", "export", "declare", "readonly", "typeset",
    "source", "eval", "exec",
    "echo", "printf", "read",
    "true", "false",
    "break", "continue", "shift",
    NULL
};

static LineHighlight highlight_bash(const char *line, int len, int state_in) {
    LineHighlight hl = {.count = 0, .state = state_in};
    int i = 0;

    while (i < len) {
        /* Comment */
        if (line[i] == '#' && (i == 0 || isspace(line[i - 1]))) {
            add_span(&hl, i, len - i, TOK_COMMENT);
            return hl;
        }

        /* Double-quoted string */
        if (line[i] == '"') {
            int start = i;
            i++;
            while (i < len && line[i] != '"') {
                if (line[i] == '\\') i++;
                i++;
            }
            if (i < len) i++;
            add_span(&hl, start, i - start, TOK_STRING);
            continue;
        }

        /* Single-quoted string */
        if (line[i] == '\'') {
            int start = i;
            i++;
            while (i < len && line[i] != '\'')
                i++;
            if (i < len) i++;
            add_span(&hl, start, i - start, TOK_STRING);
            continue;
        }

        /* Variable $VAR or ${VAR} */
        if (line[i] == '$') {
            int start = i;
            i++;
            if (i < len && line[i] == '{') {
                while (i < len && line[i] != '}') i++;
                if (i < len) i++;
            } else if (i < len && line[i] == '(') {
                /* $(command) - don't highlight specially */
                i++;
            } else {
                while (i < len && (isalnum(line[i]) || line[i] == '_'))
                    i++;
            }
            add_span(&hl, start, i - start, TOK_TYPE);
            continue;
        }

        /* Keywords */
        if (isalpha(line[i]) || line[i] == '_') {
            bool found = false;
            for (const char **kw = bash_keywords; *kw; kw++) {
                if (match_keyword(line, i, len, *kw)) {
                    int kwlen = (int)strlen(*kw);
                    add_span(&hl, i, kwlen, TOK_KEYWORD);
                    i += kwlen;
                    found = true;
                    break;
                }
            }
            if (!found) {
                while (i < len && (isalnum(line[i]) || line[i] == '_'))
                    i++;
            }
            continue;
        }

        /* Number */
        if (isdigit(line[i]) && (i == 0 || is_word_boundary(line[i - 1]))) {
            int start = i;
            while (i < len && isdigit(line[i])) i++;
            add_span(&hl, start, i - start, TOK_NUMBER);
            continue;
        }

        i++;
    }
    return hl;
}

/* ---- Markdown syntax ---- */

/* Detect language from fence tag like ```bash or ```c */
static SyntaxLang fence_detect_lang(const char *line, int len) {
    int i = 3; /* skip ``` */
    while (i < len && isspace(line[i])) i++;
    if (i >= len) return LANG_NONE;

    const char *tag = line + i;
    int taglen = 0;
    while (i + taglen < len && !isspace(line[i + taglen])) taglen++;

    if (taglen == 1 && tag[0] == 'c') return LANG_C;
    if (taglen == 2 && memcmp(tag, "sh", 2) == 0) return LANG_BASH;
    if (taglen == 3 && memcmp(tag, "zsh", 3) == 0) return LANG_BASH;
    if (taglen == 4 && memcmp(tag, "bash", 4) == 0) return LANG_BASH;
    if (taglen == 7 && memcmp(tag, "clojure", 7) == 0) return LANG_CLOJURE;
    if (taglen == 3 && memcmp(tag, "clj", 3) == 0) return LANG_CLOJURE;
    if (taglen == 4 && memcmp(tag, "diff", 4) == 0) return LANG_DIFF;
    if (taglen == 8 && memcmp(tag, "markdown", 8) == 0) return LANG_MARKDOWN;
    if (taglen == 2 && memcmp(tag, "md", 2) == 0) return LANG_MARKDOWN;
    if (taglen == 3 && memcmp(tag, "cpp", 3) == 0) return LANG_C;

    return LANG_NONE;
}

static LineHighlight highlight_markdown(const char *line, int len, int state_in) {
    LineHighlight hl = {.count = 0, .state = state_in};
    int i = 0;

    /* Code fence open/close */
    if (len >= 3 && line[0] == '`' && line[1] == '`' && line[2] == '`') {
        add_span(&hl, 0, len, TOK_CODE);
        if (state_in & STATE_CODE_FENCE) {
            /* Closing fence */
            hl.state = state_in & ~(STATE_CODE_FENCE | FENCE_LANG_MASK);
        } else {
            /* Opening fence: detect language and store in state */
            SyntaxLang flang = fence_detect_lang(line, len);
            hl.state = STATE_CODE_FENCE | ((int)flang << FENCE_LANG_SHIFT);
        }
        return hl;
    }

    /* Inside code fence: delegate to detected language */
    if (state_in & STATE_CODE_FENCE) {
        SyntaxLang flang = (SyntaxLang)((state_in & FENCE_LANG_MASK) >> FENCE_LANG_SHIFT);
        if (flang != LANG_NONE && flang != LANG_MARKDOWN) {
            LineHighlight inner = syntax_highlight_line(flang, line, len, 0);
            /* Copy spans, preserve our markdown state */
            for (int s = 0; s < inner.count; s++) {
                add_span(&hl, inner.spans[s].start, inner.spans[s].length, inner.spans[s].type);
            }
        } else {
            add_span(&hl, 0, len, TOK_CODE);
        }
        return hl;
    }

    /* Heading */
    if (len > 0 && line[0] == '#') {
        int level = 0;
        while (level < len && line[level] == '#') level++;
        TokenType ht = level <= 1 ? TOK_HEADING1 : level == 2 ? TOK_HEADING2 : TOK_HEADING3;
        add_span(&hl, 0, len, ht);
        return hl;
    }

    /* Blockquote */
    if (len > 0 && line[0] == '>') {
        add_span(&hl, 0, len, TOK_COMMENT);
        return hl;
    }

    /* Horizontal rule */
    if (len >= 3) {
        bool is_hr = true;
        char hr_char = line[0];
        if (hr_char == '-' || hr_char == '*' || hr_char == '_') {
            for (int j = 0; j < len; j++) {
                if (line[j] != hr_char && !isspace(line[j])) { is_hr = false; break; }
            }
            if (is_hr) {
                add_span(&hl, 0, len, TOK_COMMENT);
                return hl;
            }
        }
    }

    while (i < len) {
        /* Inline code */
        if (line[i] == '`') {
            int start = i;
            i++;
            while (i < len && line[i] != '`') i++;
            if (i < len) i++;
            add_span(&hl, start, i - start, TOK_CODE);
            continue;
        }

        /* Inline math $...$ (single-line, not preceded by backslash, non-space
         * after opening $). Emits three spans: open delim, content, close delim. */
        if (line[i] == '$' && (i == 0 || line[i - 1] != '\\')) {
            int open = i;
            if (i + 1 < len && !isspace((unsigned char)line[i + 1]) && line[i + 1] != '$') {
                int j = i + 1;
                while (j < len && line[j] != '$') {
                    if (line[j] == '\\' && j + 1 < len) { j += 2; continue; }
                    j++;
                }
                if (j < len && line[j] == '$') {
                    /* Matched: open at open, close at j */
                    add_span(&hl, open, 1, TOK_MATH_DELIM);
                    if (j - open - 1 > 0)
                        add_span(&hl, open + 1, j - open - 1, TOK_MATH);
                    add_span(&hl, j, 1, TOK_MATH_DELIM);
                    i = j + 1;
                    continue;
                }
            }
        }

        /* Bold **text** or __text__ */
        if (i + 1 < len && ((line[i] == '*' && line[i + 1] == '*') ||
                            (line[i] == '_' && line[i + 1] == '_'))) {
            char marker = line[i];
            int start = i;
            i += 2;
            while (i + 1 < len && !(line[i] == marker && line[i + 1] == marker)) i++;
            if (i + 1 < len) i += 2;
            add_span(&hl, start, i - start, TOK_BOLD);
            continue;
        }

        /* Italic *text* or _text_ (single) */
        if ((line[i] == '*' || line[i] == '_') &&
            (i + 1 < len && line[i + 1] != line[i]) &&
            (i + 1 < len && !isspace(line[i + 1]))) {
            char marker = line[i];
            int start = i;
            i++;
            while (i < len && line[i] != marker) i++;
            if (i < len) i++;
            add_span(&hl, start, i - start, TOK_ITALIC);
            continue;
        }

        /* Link [text](url) */
        if (line[i] == '[') {
            int start = i;
            /* Check for [[bidir]] */
            if (i + 1 < len && line[i + 1] == '[') {
                i += 2;
                while (i + 1 < len && !(line[i] == ']' && line[i + 1] == ']')) i++;
                if (i + 1 < len) i += 2;
                add_span(&hl, start, i - start, TOK_LINK);
                continue;
            }
            /* Standard markdown link */
            i++;
            while (i < len && line[i] != ']') i++;
            if (i < len) i++;
            if (i < len && line[i] == '(') {
                while (i < len && line[i] != ')') i++;
                if (i < len) i++;
            }
            add_span(&hl, start, i - start, TOK_LINK);
            continue;
        }

        /* Image ![alt](url) */
        if (line[i] == '!' && i + 1 < len && line[i + 1] == '[') {
            int start = i;
            i += 2;
            while (i < len && line[i] != ']') i++;
            if (i < len) i++;
            if (i < len && line[i] == '(') {
                while (i < len && line[i] != ')') i++;
                if (i < len) i++;
            }
            add_span(&hl, start, i - start, TOK_LINK);
            continue;
        }

        i++;
    }
    return hl;
}

/* ---- Diff syntax ---- */

static LineHighlight highlight_diff(const char *line, int len, int state_in) {
    LineHighlight hl = {.count = 0, .state = state_in};
    if (len == 0) return hl;

    if (line[0] == '+') {
        if (len >= 3 && line[1] == '+' && line[2] == '+') {
            add_span(&hl, 0, len, TOK_DIFF_HEADER);
        } else {
            add_span(&hl, 0, len, TOK_DIFF_ADD);
        }
    } else if (line[0] == '-') {
        if (len >= 3 && line[1] == '-' && line[2] == '-') {
            add_span(&hl, 0, len, TOK_DIFF_HEADER);
        } else {
            add_span(&hl, 0, len, TOK_DIFF_DEL);
        }
    } else if (line[0] == '@' && len >= 2 && line[1] == '@') {
        add_span(&hl, 0, len, TOK_DIFF_HEADER);
    } else if (len >= 4 && strncmp(line, "diff", 4) == 0) {
        add_span(&hl, 0, len, TOK_DIFF_HEADER);
    } else if (len >= 5 && strncmp(line, "index", 5) == 0) {
        add_span(&hl, 0, len, TOK_DIFF_HEADER);
    }

    return hl;
}

/* ---- Help syntax ---- */

static LineHighlight highlight_help(const char *line, int len, int state_in) {
    LineHighlight hl = {.count = 0, .state = state_in};
    if (len == 0) return hl;

    /* Separator lines (find-doc output: "-------------------------") */
    if (len >= 3 && line[0] == '-') {
        bool is_sep = true;
        for (int i = 0; i < len; i++) {
            if (line[i] != '-') { is_sep = false; break; }
        }
        if (is_sep) {
            add_span(&hl, 0, len, TOK_COMMENT);
            return hl;
        }
    }

    /* Section headers: lines ending with ":" that don't start with space */
    if (line[0] != ' ' && len > 1 && line[len - 1] == ':') {
        /* Highlight the whole line as heading, but pick out quoted strings */
        int i = 0;
        while (i < len) {
            if (line[i] == '"') {
                int start = i;
                i++;
                while (i < len && line[i] != '"') i++;
                if (i < len) i++; /* skip closing quote */
                add_span(&hl, start, i - start, TOK_STRING);
            } else {
                int start = i;
                while (i < len && line[i] != '"') i++;
                add_span(&hl, start, i - start, TOK_HEADING2);
            }
        }
        return hl;
    }

    /* Namespace-qualified Clojure names (e.g., "clojure.core/map") on non-indented lines */
    if (line[0] != ' ') {
        /* Check for ns/name pattern: word chars, dots, slashes */
        bool has_slash = false;
        bool has_dot = false;
        int end = 0;
        while (end < len && (isalnum(line[end]) || line[end] == '.' ||
               line[end] == '/' || line[end] == '-' || line[end] == '_' ||
               line[end] == '?' || line[end] == '!' || line[end] == '*'))
            end++;
        for (int j = 0; j < end; j++) {
            if (line[j] == '/') has_slash = true;
            if (line[j] == '.') has_dot = true;
        }
        if (has_slash && has_dot && end > 0) {
            add_span(&hl, 0, end, TOK_FUNCTION);
            return hl;
        }
    }

    /* Command table rows: "  command-name      C-x k    docstring" */
    if (len > 4 && line[0] == ' ' && line[1] == ' ' && line[2] != ' ') {
        /* Command name: from col 2 to first run of 2+ spaces */
        int name_start = 2;
        int i = name_start;
        while (i < len && !(line[i] == ' ' && i + 1 < len && line[i + 1] == ' '))
            i++;
        int name_end = i;
        add_span(&hl, name_start, name_end - name_start, TOK_FUNCTION);

        /* Skip whitespace to find keybinding */
        while (i < len && line[i] == ' ') i++;
        if (i < len) {
            int bind_start = i;
            /* Keybinding: next token before another run of 2+ spaces */
            while (i < len && !(line[i] == ' ' && i + 1 < len && line[i + 1] == ' '))
                i++;
            int bind_end = i;
            /* Only color as keybinding if it looks like one (contains C- or M- or is short) */
            if (bind_end - bind_start > 0 && bind_end - bind_start < 20) {
                add_span(&hl, bind_start, bind_end - bind_start, TOK_KEYWORD);
            }
        }
        return hl;
    }

    /* Indented text from find-doc */
    if (len > 2 && line[0] == ' ' && line[1] == ' ') {
        int i = 2;
        while (i < len && line[i] == ' ') i++;

        /* Lines starting with ( — delegate to Clojure highlighter */
        if (i < len && line[i] == '(') {
            LineHighlight clj = syntax_highlight_line(LANG_CLOJURE, line + i, len - i, 0);
            for (int s = 0; s < clj.count; s++) {
                add_span(&hl, clj.spans[s].start + i, clj.spans[s].length, clj.spans[s].type);
            }
            return hl;
        }

        /* Docstring body: highlight :keywords inline */
        while (i < len) {
            if (line[i] == ':' && i + 1 < len && isalpha(line[i + 1])) {
                int start = i;
                i++;
                while (i < len && (isalnum(line[i]) || line[i] == '-' ||
                       line[i] == '_' || line[i] == '/' || line[i] == '?'))
                    i++;
                add_span(&hl, start, i - start, TOK_TYPE);
            } else {
                i++;
            }
        }
        return hl;
    }

    /* "key-desc runs the command cmd-name" from describe-key */
    if (len > 5) {
        const char *runs = strstr(line, " runs the command ");
        if (runs) {
            int key_end = (int)(runs - line);
            add_span(&hl, 0, key_end, TOK_KEYWORD);
            int cmd_start = key_end + 18; /* length of " runs the command " */
            if (cmd_start < len) {
                add_span(&hl, cmd_start, len - cmd_start, TOK_FUNCTION);
            }
            return hl;
        }
    }

    /* "  Key bindings: ..." or "  Implemented in: ..." or "  Dispatch: ..." labels */
    if (len > 4 && line[0] == ' ' && line[1] == ' ') {
        const char *colon = strchr(line + 2, ':');
        if (colon && colon - line < 20) {
            int label_end = (int)(colon - line) + 1;
            add_span(&hl, 2, label_end - 2, TOK_HEADING3);
            /* Value after the colon */
            int val_start = label_end;
            while (val_start < len && line[val_start] == ' ') val_start++;
            if (val_start < len) {
                add_span(&hl, val_start, len - val_start, TOK_KEYWORD);
            }
            return hl;
        }
    }

    return hl;
}

/* ---- Public API ---- */

LineHighlight syntax_highlight_line(SyntaxLang lang, const char *line, int len, int state_in) {
    switch (lang) {
        case LANG_C:        return highlight_c(line, len, state_in);
        case LANG_CLOJURE:  return highlight_clojure(line, len, state_in);
        case LANG_BASH:     return highlight_bash(line, len, state_in);
        case LANG_MARKDOWN: return highlight_markdown(line, len, state_in);
        case LANG_DIFF:     return highlight_diff(line, len, state_in);
        case LANG_HELP:     return highlight_help(line, len, state_in);
        default: {
            LineHighlight hl = {.count = 0, .state = 0};
            return hl;
        }
    }
}

SyntaxLang syntax_detect(const char *filename) {
    if (!filename) return LANG_NONE;

    if (str_ends_with(filename, ".c") || str_ends_with(filename, ".h") ||
        str_ends_with(filename, ".cc") || str_ends_with(filename, ".cpp") ||
        str_ends_with(filename, ".hpp"))
        return LANG_C;

    if (str_ends_with(filename, ".clj") || str_ends_with(filename, ".cljs") ||
        str_ends_with(filename, ".cljc") || str_ends_with(filename, ".edn") ||
        str_ends_with(filename, ".bb"))
        return LANG_CLOJURE;

    if (str_ends_with(filename, ".sh") || str_ends_with(filename, ".bash") ||
        str_ends_with(filename, ".zsh"))
        return LANG_BASH;

    if (str_ends_with(filename, ".md") || str_ends_with(filename, ".markdown"))
        return LANG_MARKDOWN;

    return LANG_NONE;
}
