#include "display.h"
#include "buffer.h"
#include "mode.h"
#include "syntax.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void display_init(void) {
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    start_color();
    use_default_colors();

    /* Set up color pairs */
    init_pair(COLOR_MODELINE, COLOR_BLACK, COLOR_WHITE);
    init_pair(COLOR_MODELINE_INACTIVE, COLOR_WHITE, COLOR_BLUE);
    init_pair(COLOR_MINIBUFFER, -1, -1);
    init_pair(COLOR_REGION, COLOR_BLACK, COLOR_CYAN);
    init_pair(COLOR_LINENUM, COLOR_YELLOW, -1);

    /* Syntax colors */
    init_pair(COLOR_KEYWORD, COLOR_YELLOW, -1);
    init_pair(COLOR_STRING, COLOR_GREEN, -1);
    init_pair(COLOR_COMMENT, COLOR_CYAN, -1);
    init_pair(COLOR_TYPE, COLOR_GREEN, -1);
    init_pair(COLOR_FUNCTION, COLOR_BLUE, -1);
    init_pair(COLOR_NUMBER, COLOR_MAGENTA, -1);
    init_pair(COLOR_PREPROC, COLOR_RED, -1);

    /* Markdown colors */
    init_pair(COLOR_HEADING1, COLOR_RED, -1);
    init_pair(COLOR_HEADING2, COLOR_GREEN, -1);
    init_pair(COLOR_HEADING3, COLOR_YELLOW, -1);
    init_pair(COLOR_LINK, COLOR_CYAN, -1);
    init_pair(COLOR_CODE, COLOR_GREEN, -1);
    init_pair(COLOR_BOLD_TEXT, COLOR_WHITE, -1);
    init_pair(COLOR_ITALIC_TEXT, COLOR_WHITE, -1);

    /* Diff colors */
    init_pair(COLOR_DIFF_ADD, COLOR_GREEN, -1);
    init_pair(COLOR_DIFF_DEL, COLOR_RED, -1);
    init_pair(COLOR_DIFF_HEADER, COLOR_CYAN, -1);

    /* Math (inline $...$ in markdown) */
    init_pair(COLOR_MATH, COLOR_MAGENTA, -1);
    init_pair(COLOR_MATH_DELIM, COLOR_BLACK, -1);

    curs_set(1);
    /* Request blinking block cursor via terminal escape sequence */
    printf("\033[1 q");
    fflush(stdout);
}

void display_end(void) {
    /* Restore default cursor and disable mouse tracking */
    printf("\033[0 q\033[?1006l\033[?1002l");
    fflush(stdout);
    endwin();
}

void display_clear(void) {
    clear();
}

/* Map TokenType to ncurses color pair */
static int token_color(TokenType type) {
    switch (type) {
        case TOK_KEYWORD:     return COLOR_KEYWORD;
        case TOK_STRING:      return COLOR_STRING;
        case TOK_COMMENT:     return COLOR_COMMENT;
        case TOK_TYPE:        return COLOR_TYPE;
        case TOK_FUNCTION:    return COLOR_FUNCTION;
        case TOK_NUMBER:      return COLOR_NUMBER;
        case TOK_PREPROC:     return COLOR_PREPROC;
        case TOK_HEADING1:    return COLOR_HEADING1;
        case TOK_HEADING2:    return COLOR_HEADING2;
        case TOK_HEADING3:    return COLOR_HEADING3;
        case TOK_LINK:        return COLOR_LINK;
        case TOK_CODE:        return COLOR_CODE;
        case TOK_BOLD:        return COLOR_BOLD_TEXT;
        case TOK_ITALIC:      return COLOR_ITALIC_TEXT;
        case TOK_DIFF_ADD:    return COLOR_DIFF_ADD;
        case TOK_DIFF_DEL:    return COLOR_DIFF_DEL;
        case TOK_DIFF_HEADER: return COLOR_DIFF_HEADER;
        case TOK_MATH:        return COLOR_MATH;
        case TOK_MATH_DELIM:  return COLOR_MATH_DELIM;
        default:              return 0;
    }
}

/* LaTeX-like source → Unicode substitutions for inline math ($...$). */
static const struct { const char *src; const char *dst; } math_subs[] = {
    /* Operators with longer names first (longest-match wins) */
    {"\\rightarrow", "→"},
    {"\\leftarrow",  "←"},
    {"\\epsilon",    "ε"},
    {"\\Epsilon",    "Ε"},
    {"\\upsilon",    "υ"},
    {"\\Upsilon",    "Υ"},
    {"\\lambda",     "λ"},
    {"\\Lambda",     "Λ"},
    {"\\approx",     "≈"},
    {"\\exists",     "∃"},
    {"\\forall",     "∀"},
    {"\\gamma",      "γ"},
    {"\\Gamma",      "Γ"},
    {"\\delta",      "δ"},
    {"\\Delta",      "Δ"},
    {"\\theta",      "θ"},
    {"\\Theta",      "Θ"},
    {"\\kappa",      "κ"},
    {"\\Kappa",      "Κ"},
    {"\\sigma",      "σ"},
    {"\\Sigma",      "Σ"},
    {"\\omega",      "ω"},
    {"\\Omega",      "Ω"},
    {"\\alpha",      "α"},
    {"\\Alpha",      "Α"},
    {"\\infty",      "∞"},
    {"\\times",      "×"},
    {"\\nabla",      "∇"},
    {"\\sqrt",       "√"},
    {"\\beta",       "β"},
    {"\\Beta",       "Β"},
    {"\\zeta",       "ζ"},
    {"\\Zeta",       "Ζ"},
    {"\\iota",       "ι"},
    {"\\Iota",       "Ι"},
    {"\\sum",        "∑"},
    {"\\int",        "∫"},
    {"\\prod",       "∏"},
    {"\\leq",        "≤"},
    {"\\geq",        "≥"},
    {"\\neq",        "≠"},
    {"\\div",        "÷"},
    {"\\cdot",       "·"},
    {"\\partial",    "∂"},
    {"\\eta",        "η"},
    {"\\Eta",        "Η"},
    {"\\mu",         "μ"},
    {"\\Mu",         "Μ"},
    {"\\nu",         "ν"},
    {"\\Nu",         "Ν"},
    {"\\xi",         "ξ"},
    {"\\Xi",         "Ξ"},
    {"\\pi",         "π"},
    {"\\Pi",         "Π"},
    {"\\rho",        "ρ"},
    {"\\Rho",        "Ρ"},
    {"\\tau",        "τ"},
    {"\\Tau",        "Τ"},
    {"\\phi",        "φ"},
    {"\\Phi",        "Φ"},
    {"\\chi",        "χ"},
    {"\\Chi",        "Χ"},
    {"\\psi",        "ψ"},
    {"\\Psi",        "Ψ"},
    {"\\pm",         "±"},
    {"\\to",         "→"},
    {"\\in",         "∈"},
    /* Superscripts: ^0..^9 */
    {"^0", "⁰"}, {"^1", "¹"}, {"^2", "²"}, {"^3", "³"}, {"^4", "⁴"},
    {"^5", "⁵"}, {"^6", "⁶"}, {"^7", "⁷"}, {"^8", "⁸"}, {"^9", "⁹"},
    /* Subscripts: _0.._9 */
    {"_0", "₀"}, {"_1", "₁"}, {"_2", "₂"}, {"_3", "₃"}, {"_4", "₄"},
    {"_5", "₅"}, {"_6", "₆"}, {"_7", "₇"}, {"_8", "₈"}, {"_9", "₉"},
};

static const int math_subs_count = (int)(sizeof(math_subs) / sizeof(math_subs[0]));

/* Look up a math substitution at line[i..len]. On match, returns the UTF-8
 * replacement and sets *consumed to the number of source chars matched.
 * Returns NULL on no match. */
static const char *math_sub_at(const char *line, int len, int i, int *consumed) {
    const char *best = NULL;
    int best_len = 0;
    for (int k = 0; k < math_subs_count; k++) {
        int slen = (int)strlen(math_subs[k].src);
        if (slen > len - i) continue;
        if (memcmp(line + i, math_subs[k].src, slen) == 0 && slen > best_len) {
            best = math_subs[k].dst;
            best_len = slen;
        }
    }
    if (best) { *consumed = best_len; return best; }
    return NULL;
}

/* Get extra attributes for token type */
static int token_attr(TokenType type) {
    switch (type) {
        case TOK_HEADING1:  return A_BOLD;
        case TOK_HEADING2:  return A_BOLD;
        case TOK_BOLD:      return A_BOLD;
        case TOK_ITALIC:    return A_UNDERLINE;
        case TOK_KEYWORD:   return A_BOLD;
        case TOK_MATH_DELIM: return A_DIM;
        default:            return 0;
    }
}

void display_refresh_window(Window *win, bool is_current) {
    Buffer *buf = win->buffer;
    int visible_rows = win->rows - 1;  /* reserve last row for modeline */

    /* Determine region for highlighting */
    size_t region_start = 0, region_end = 0;
    bool has_region = buf->mark_active;
    if (has_region) {
        region_start = buf->mark < buf->point ? buf->mark : buf->point;
        region_end = buf->mark > buf->point ? buf->mark : buf->point;
    }

    /* Get syntax language for this buffer */
    SyntaxLang lang = mode_syntax_for(buf->mode_name);

    /* We need to compute syntax state from the beginning of the file up to top_line.
     * For efficiency, just compute from start for now (fine for normal-sized files). */
    int syntax_state = 0;
    if (lang != LANG_NONE) {
        size_t p = 0;
        for (int i = 0; i < win->top_line && p < buffer_length(buf); i++) {
            /* Get this line's text */
            size_t lstart = p;
            size_t lend = buffer_line_end(buf, p);
            int llen = (int)(lend - lstart);
            char *ltext = buffer_region(buf, lstart, lend);
            LineHighlight lh = syntax_highlight_line(lang, ltext, llen, syntax_state);
            syntax_state = lh.state;
            free(ltext);
            p = buffer_next_line_start(buf, p);
        }
    }

    /* Render buffer content line by line */
    size_t pos = 0;
    int cur_line = 0;
    int total_lines = (int)buffer_line_count(buf);

    /* Advance to top_line */
    for (int i = 0; i < win->top_line && pos < buffer_length(buf); i++) {
        pos = buffer_next_line_start(buf, pos);
        cur_line++;
    }

    int row = 0;
    while (row < visible_rows) {
        move(win->y + row, win->x);

        if (cur_line >= total_lines && pos >= buffer_length(buf)) {
            for (int c = 0; c < win->cols; c++)
                mvaddch(win->y + row, win->x + c, ' ');
            row++;
            cur_line++;
            continue;
        }

        /* Extract line text for syntax highlighting */
        size_t line_start_pos = pos;
        size_t line_end_pos = buffer_line_end(buf, pos);
        int line_len = (int)(line_end_pos - line_start_pos);

        LineHighlight hl = {.count = 0, .state = syntax_state};
        char *line_text = NULL;

        if (lang != LANG_NONE && line_len > 0) {
            line_text = buffer_region(buf, line_start_pos, line_end_pos);
            hl = syntax_highlight_line(lang, line_text, line_len, syntax_state);
            syntax_state = hl.state;
        } else if (lang != LANG_NONE) {
            /* Empty line, still update state */
            hl = syntax_highlight_line(lang, "", 0, syntax_state);
            syntax_state = hl.state;
        }

        /* Render the line character by character, wrapping at window edge */
        int col = 0;
        bool row_overflow = false;
        while (pos < buffer_length(buf)) {
            char ch = buffer_char_at(buf, pos);
            if (ch == '\n') {
                pos++;
                break;
            }

            /* Wrap to next screen row if needed */
            if (col >= win->cols) {
                for (int c = col; c < win->cols; c++)
                    mvaddch(win->y + row, win->x + c, ' ');
                col = 0;
                row++;
                if (row >= visible_rows) { row_overflow = true; break; }
                move(win->y + row, win->x);
            }

            /* Determine syntax color for this position */
            int color_pair = 0;
            int extra_attr = 0;
            TokenType token_type = TOK_NORMAL;
            int char_offset = (int)(pos - line_start_pos);
            for (int s = 0; s < hl.count; s++) {
                if (char_offset >= hl.spans[s].start &&
                    char_offset < hl.spans[s].start + hl.spans[s].length) {
                    color_pair = token_color(hl.spans[s].type);
                    extra_attr = token_attr(hl.spans[s].type);
                    token_type = hl.spans[s].type;
                    break;
                }
            }

            /* Inline math: try to substitute a LaTeX-like sequence with Unicode. */
            if (token_type == TOK_MATH && line_text) {
                int consumed = 0;
                const char *sub = math_sub_at(line_text, line_len, char_offset, &consumed);
                if (sub && consumed > 0) {
                    if (has_region && pos >= region_start && pos < region_end) {
                        attron(COLOR_PAIR(COLOR_REGION));
                        mvaddstr(win->y + row, win->x + col, sub);
                        attroff(COLOR_PAIR(COLOR_REGION));
                    } else if (color_pair) {
                        attron(COLOR_PAIR(color_pair) | extra_attr);
                        mvaddstr(win->y + row, win->x + col, sub);
                        attroff(COLOR_PAIR(color_pair) | extra_attr);
                    } else {
                        mvaddstr(win->y + row, win->x + col, sub);
                    }
                    col++;
                    pos += consumed;
                    continue;
                }
            }

            if (ch == '\t') {
                int spaces = 4 - (col % 4);
                for (int s = 0; s < spaces; s++) {
                    if (col >= win->cols) {
                        col = 0;
                        row++;
                        if (row >= visible_rows) { row_overflow = true; break; }
                        move(win->y + row, win->x);
                    }
                    if (has_region && pos >= region_start && pos < region_end)
                        attron(COLOR_PAIR(COLOR_REGION));
                    else if (color_pair)
                        attron(COLOR_PAIR(color_pair) | extra_attr);
                    mvaddch(win->y + row, win->x + col, ' ');
                    if (has_region && pos >= region_start && pos < region_end)
                        attroff(COLOR_PAIR(COLOR_REGION));
                    else if (color_pair)
                        attroff(COLOR_PAIR(color_pair) | extra_attr);
                    col++;
                }
                if (row_overflow) break;
                pos++;
                continue;
            }

            /* UTF-8 multi-byte: emit the whole sequence in one write so
             * the terminal sees valid UTF-8 instead of byte fragments
             * split by cursor-move escapes. */
            unsigned char lead = (unsigned char)ch;
            if (lead >= 0xC0) {
                int nbytes = 1;
                if ((lead & 0xE0) == 0xC0) nbytes = 2;
                else if ((lead & 0xF0) == 0xE0) nbytes = 3;
                else if ((lead & 0xF8) == 0xF0) nbytes = 4;
                if ((size_t)nbytes > buffer_length(buf) - pos) {
                    nbytes = (int)(buffer_length(buf) - pos);
                }
                char utf8[5] = {0};
                for (int i = 0; i < nbytes; i++) {
                    utf8[i] = buffer_char_at(buf, pos + i);
                }

                if (has_region && pos >= region_start && pos < region_end) {
                    attron(COLOR_PAIR(COLOR_REGION));
                    mvaddstr(win->y + row, win->x + col, utf8);
                    attroff(COLOR_PAIR(COLOR_REGION));
                } else if (color_pair) {
                    attron(COLOR_PAIR(color_pair) | extra_attr);
                    mvaddstr(win->y + row, win->x + col, utf8);
                    attroff(COLOR_PAIR(color_pair) | extra_attr);
                } else {
                    mvaddstr(win->y + row, win->x + col, utf8);
                }

                col++;
                pos += nbytes;
                continue;
            }

            /* Apply region highlight (overrides syntax) or syntax color */
            if (has_region && pos >= region_start && pos < region_end) {
                attron(COLOR_PAIR(COLOR_REGION));
                mvaddch(win->y + row, win->x + col, ch);
                attroff(COLOR_PAIR(COLOR_REGION));
            } else if (color_pair) {
                attron(COLOR_PAIR(color_pair) | extra_attr);
                mvaddch(win->y + row, win->x + col, ch);
                attroff(COLOR_PAIR(color_pair) | extra_attr);
            } else {
                mvaddch(win->y + row, win->x + col, ch);
            }

            col++;
            pos++;
        }

        free(line_text);

        /* Clear rest of current screen row */
        if (!row_overflow) {
            for (int c = col; c < win->cols; c++)
                mvaddch(win->y + row, win->x + c, ' ');
        }

        row++;
        cur_line++;
    }

    /* Draw modeline */
    display_modeline(win, is_current);

    /* Position cursor */
    if (is_current) {
        int sy, sx;
        window_point_to_screen(win, &sy, &sx);
        if (sx >= win->x + win->cols) sx = win->x + win->cols - 1;
        if (sy >= win->y + visible_rows) sy = win->y + visible_rows - 1;
        move(sy, sx);
    }
}

void display_modeline(Window *win, bool is_current) {
    Buffer *buf = win->buffer;
    int modeline_y = win->y + win->rows - 1;
    int pair = is_current ? COLOR_MODELINE : COLOR_MODELINE_INACTIVE;

    attron(COLOR_PAIR(pair));

    char line[512];
    int l, c;
    buffer_point_to_line_col(buf, buf->point, &l, &c);

    snprintf(line, sizeof(line), " %s%s  %s  (%d,%d)  %s",
             buf->modified ? "**" : "--",
             buf->read_only ? "%%" : "--",
             buf->name,
             l + 1, c,
             buf->mode_name ? buf->mode_name : "Fundamental");

    int len = (int)strlen(line);
    move(modeline_y, win->x);
    addstr(line);
    for (int i = len; i < win->cols; i++)
        addch(' ');

    attroff(COLOR_PAIR(pair));
}

void display_minibuffer(const char *msg) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    move(rows - 1, 0);
    clrtoeol();
    if (msg) {
        addnstr(msg, cols);
    }
}
