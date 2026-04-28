#define _POSIX_C_SOURCE 200809L
#include "display.h"
#include "buffer.h"
#include "mode.h"
#include "syntax.h"
#include "perf.h"
#include "sci.h"
#include "util.h"
#include "effects.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static Buffer *flash_buf = NULL;
static size_t  flash_pos = 0;
static struct timespec flash_deadline;

/* Cursor screen coordinates captured by the most recent
 * display_refresh_window(is_current=true). -1 = off-screen. */
static int last_cursor_y = -1;
static int last_cursor_x = -1;

bool display_last_cursor(int *sy, int *sx) {
    if (last_cursor_y < 0) return false;
    *sy = last_cursor_y;
    *sx = last_cursor_x;
    return true;
}

static long ts_to_ms(struct timespec t) {
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

void display_flash_set(Buffer *buf, size_t pos, int ms) {
    flash_buf = buf;
    flash_pos = pos;
    clock_gettime(CLOCK_MONOTONIC, &flash_deadline);
    flash_deadline.tv_sec  += ms / 1000;
    flash_deadline.tv_nsec += (ms % 1000) * 1000000L;
    if (flash_deadline.tv_nsec >= 1000000000L) {
        flash_deadline.tv_sec++;
        flash_deadline.tv_nsec -= 1000000000L;
    }
}

void display_flash_clear(void) {
    flash_buf = NULL;
}

int display_flash_active(void) {
    if (!flash_buf) return 0;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (ts_to_ms(now) >= ts_to_ms(flash_deadline)) {
        flash_buf = NULL;
        return 0;
    }
    return 1;
}

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

/* Math span cache (LaTeX → Unicode via the latex2unicode SCI library).
 * Keyed by span source bytes. rendered == NULL means SCI failed for this
 * source — we still cache the negative result so we don't retry every
 * redraw. Cached width avoids per-frame mbsrtowcs+wcswidth allocs. */
#define MATH_CACHE_SIZE 512

typedef struct {
    char *src;          /* span source, NULL = empty slot */
    char *rendered;     /* SCI result, NULL = SCI failed for this src */
    int   width;        /* cached display width of rendered (-1 if unknown) */
    uint64_t last_use;
} MathCacheEntry;

static MathCacheEntry math_cache[MATH_CACHE_SIZE];
static uint64_t math_cache_clock = 0;

/* Returns the cached MathCacheEntry slot index for `src`, computing on miss.
 * The slot's `rendered` is NULL if SCI failed (caller falls back to source
 * bytes). Returns -1 only on allocation failure. */
static int math_cache_get_or_compute(const char *src, size_t len) {
    int empty_slot = -1;
    int oldest_slot = 0;
    uint64_t oldest_clock = math_cache[0].last_use;
    for (int i = 0; i < MATH_CACHE_SIZE; i++) {
        if (math_cache[i].src == NULL) {
            if (empty_slot < 0) empty_slot = i;
            continue;
        }
        if (strlen(math_cache[i].src) == len &&
            memcmp(math_cache[i].src, src, len) == 0) {
            math_cache[i].last_use = ++math_cache_clock;
            return i;
        }
        if (math_cache[i].last_use < oldest_clock) {
            oldest_clock = math_cache[i].last_use;
            oldest_slot = i;
        }
    }

    int slot = empty_slot >= 0 ? empty_slot : oldest_slot;
    char *src_copy = (char *)malloc(len + 1);
    if (!src_copy) return -1;
    memcpy(src_copy, src, len);
    src_copy[len] = '\0';

    if (math_cache[slot].src) {
        free(math_cache[slot].src);
        free(math_cache[slot].rendered);
    }

    /* Build (latex2unicode/latex2unicode "...") with EDN-escaped argument. */
    size_t quoted_cap = len * 2 + 8;
    char *quoted = (char *)malloc(quoted_cap);
    if (!quoted) { free(src_copy); return -1; }
    edn_escape_string(quoted, quoted_cap, src_copy);

    size_t form_cap = strlen(quoted) + 64;
    char *form = (char *)malloc(form_cap);
    if (!form) { free(quoted); free(src_copy); return -1; }
    snprintf(form, form_cap, "(latex2unicode/latex2unicode %s)", quoted);
    free(quoted);

    char *raw = sci_eval(form);
    free(form);

    char *rendered = NULL;
    if (raw) {
        size_t consumed = 0;
        EdnVal *v = edn_parse(raw, strlen(raw), &consumed);
        if (v && v->type == EDN_STRING) {
            rendered = (char *)malloc(strlen(v->str) + 1);
            if (rendered) strcpy(rendered, v->str);
        }
        edn_free(v);
        free(raw);
    }

    math_cache[slot].src = src_copy;
    math_cache[slot].rendered = rendered;
    math_cache[slot].width = -1;  /* lazily computed on first render */
    math_cache[slot].last_use = ++math_cache_clock;
    return slot;
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

    if (is_current) {
        last_cursor_y = -1;
        last_cursor_x = -1;
    }

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
            uint64_t t0 = perf_now_ns();
            LineHighlight lh = syntax_highlight_line(lang, ltext, llen, syntax_state);
            perf_record("highlight-line", perf_now_ns() - t0);
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

    /* Cursor screen position is captured during rendering so math spans
     * (whose source bytes != rendered Unicode width) don't desync the
     * blinking cursor. -1 = not yet seen; falls back to window_point_to_screen. */
    int cursor_screen_y = -1;
    int cursor_screen_x = -1;

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
            uint64_t t0 = perf_now_ns();
            hl = syntax_highlight_line(lang, line_text, line_len, syntax_state);
            perf_record("highlight-line", perf_now_ns() - t0);
            syntax_state = hl.state;
        } else if (lang != LANG_NONE) {
            /* Empty line, still update state */
            uint64_t t0 = perf_now_ns();
            hl = syntax_highlight_line(lang, "", 0, syntax_state);
            perf_record("highlight-line", perf_now_ns() - t0);
            syntax_state = hl.state;
        }

        /* Render the line character by character, wrapping at window edge */
        int col = 0;
        bool row_overflow = false;
        bool hit_newline = false;
        while (pos < buffer_length(buf)) {
            char ch = buffer_char_at(buf, pos);
            if (ch == '\n') {
                if (is_current && cursor_screen_y < 0 &&
                    pos == (size_t)buf->point) {
                    cursor_screen_y = win->y + row;
                    cursor_screen_x = win->x + col;
                }
                pos++;
                hit_newline = true;
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

            /* Capture cursor screen position before rendering the byte at
             * `pos`. The math-span path advances `col` by the rendered Unicode
             * width, so capturing here keeps the cursor in sync. */
            if (is_current && cursor_screen_y < 0 &&
                pos == (size_t)buf->point) {
                cursor_screen_y = win->y + row;
                cursor_screen_x = win->x + col;
            }

            /* Determine syntax color for this position */
            int color_pair = 0;
            int extra_attr = 0;
            TokenType token_type = TOK_NORMAL;
            int char_offset = (int)(pos - line_start_pos);
            int matched_span = -1;
            for (int s = 0; s < hl.count; s++) {
                if (char_offset >= hl.spans[s].start &&
                    char_offset < hl.spans[s].start + hl.spans[s].length) {
                    color_pair = token_color(hl.spans[s].type);
                    extra_attr = token_attr(hl.spans[s].type);
                    token_type = hl.spans[s].type;
                    matched_span = s;
                    break;
                }
            }

            /* Inline math: at a TOK_MATH span start, render the cached
             * latex2unicode output atomically and skip past the source bytes.
             * When the cursor is inside the span (or on a surrounding $),
             * fall through to per-byte source render so the user can edit. */
            if (token_type == TOK_MATH && line_text && matched_span >= 0 &&
                char_offset == hl.spans[matched_span].start) {
                int span_start = char_offset;
                int span_end   = span_start + hl.spans[matched_span].length;

                int cursor_off = -1;
                if ((size_t)buf->point >= line_start_pos &&
                    (size_t)buf->point <= line_end_pos) {
                    cursor_off = (int)((size_t)buf->point - line_start_pos);
                }
                /* Inside-span check includes the surrounding $...$ so the
                 * user can sit on a delimiter and still edit. */
                bool cursor_inside =
                    is_current &&
                    cursor_off >= span_start - 1 &&
                    cursor_off <= span_end + 1;

                if (!cursor_inside) {
                    int slot = math_cache_get_or_compute(
                        line_text + span_start,
                        (size_t)(span_end - span_start));
                    const char *rendered = slot >= 0 ? math_cache[slot].rendered : NULL;
                    if (rendered) {
                        if (math_cache[slot].width < 0)
                            math_cache[slot].width = utf8_display_width(rendered, 0);
                        int width = math_cache[slot].width;
                        if (has_region && pos >= region_start && pos < region_end) {
                            attron(COLOR_PAIR(COLOR_REGION));
                            mvaddstr(win->y + row, win->x + col, rendered);
                            attroff(COLOR_PAIR(COLOR_REGION));
                        } else if (color_pair) {
                            attron(COLOR_PAIR(color_pair) | extra_attr);
                            mvaddstr(win->y + row, win->x + col, rendered);
                            attroff(COLOR_PAIR(color_pair) | extra_attr);
                        } else {
                            mvaddstr(win->y + row, win->x + col, rendered);
                        }
                        col += width;
                        pos += (size_t)(span_end - span_start);
                        continue;
                    }
                    /* SCI failed; fall through to source-byte render. */
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

            /* Paren-flash: check once per byte, shared by UTF-8 and single-byte paths. */
            int is_flash = (display_flash_active() && flash_buf == buf && pos == flash_pos);

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

                if (is_flash) {
                    attron(A_REVERSE);
                    mvaddstr(win->y + row, win->x + col, utf8);
                    attroff(A_REVERSE);
                } else if (has_region && pos >= region_start && pos < region_end) {
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
            if (is_flash) {
                attron(A_REVERSE);
                mvaddch(win->y + row, win->x + col, ch);
                attroff(A_REVERSE);
            } else if (has_region && pos >= region_start && pos < region_end) {
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

        /* Catch point at end-of-buffer or end-of-line without trailing newline.
         * Skip after a `\n` break — pos has already advanced past the newline,
         * and `point` matching the next line's start should be captured by
         * the next outer iteration, not this line's stale (row, col). */
        if (!hit_newline && is_current && cursor_screen_y < 0 &&
            pos == (size_t)buf->point) {
            cursor_screen_y = win->y + row;
            cursor_screen_x = win->x + col;
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
        if (cursor_screen_y >= 0) {
            sy = cursor_screen_y;
            sx = cursor_screen_x;
        } else {
            window_point_to_screen(win, &sy, &sx);
        }
        if (sx >= win->x + win->cols) sx = win->x + win->cols - 1;
        if (sy >= win->y + visible_rows) sy = win->y + visible_rows - 1;
        move(sy, sx);
        last_cursor_y = sy;
        last_cursor_x = sx;
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
