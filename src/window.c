#include "window.h"
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>

Window *window_list = NULL;
Window *current_window = NULL;

#define MAX_SPLITS 16
static SplitBoundary splits[MAX_SPLITS];
static int split_count = 0;

Window *window_create(Buffer *buf, int y, int x, int rows, int cols) {
    Window *win = hmalloc(sizeof(Window));
    memset(win, 0, sizeof(Window));
    win->buffer = buf;
    win->y = y;
    win->x = x;
    win->rows = rows;
    win->cols = cols;
    win->split_ratio = 0.5f;

    /* Add to window list */
    win->next = window_list;
    window_list = win;

    return win;
}

void window_destroy(Window *win) {
    if (!win) return;

    /* Remove from window list */
    Window **pp = &window_list;
    while (*pp && *pp != win) pp = &(*pp)->next;
    if (*pp) *pp = win->next;

    free(win);
}

/* Count visual columns for a buffer range, handling tabs.
 * Matches display_refresh_window: each UTF-8 sequence occupies 1 column
 * (continuation bytes 0x80-0xBF are skipped). */
static int visual_cols(Buffer *buf, size_t from, size_t to) {
    int vcol = 0;
    for (size_t p = from; p < to; p++) {
        unsigned char ch = (unsigned char)buffer_char_at(buf, p);
        if (ch == '\t')
            vcol += 4 - (vcol % 4);
        else if ((ch & 0xC0) != 0x80)
            vcol++;
    }
    return vcol;
}

/* Count screen rows a buffer line occupies with wrapping */
static int line_screen_rows(Buffer *buf, size_t line_start, int win_cols) {
    size_t line_end = buffer_line_end(buf, line_start);
    int vcol = visual_cols(buf, line_start, line_end);
    if (vcol == 0) return 1;
    return (vcol + win_cols - 1) / win_cols;
}

void window_scroll_to_point(Window *win) {
    Buffer *buf = win->buffer;
    int point_line, point_col;
    buffer_point_to_line_col(buf, buf->point, &point_line, &point_col);
    int visible_rows = win->rows - 1;
    if (visible_rows < 1) visible_rows = 1;

    /* Scroll up if cursor line is above viewport */
    if (point_line < win->top_line)
        win->top_line = point_line;

    /* Scroll down until cursor's screen row fits in visible area */
    while (win->top_line <= point_line) {
        /* Count screen rows from top_line to cursor position */
        int screen_row = 0;
        size_t pos = 0;
        for (int i = 0; i < win->top_line && pos < buffer_length(buf); i++)
            pos = buffer_next_line_start(buf, pos);

        for (int line = win->top_line; line < point_line && pos < buffer_length(buf); line++) {
            screen_row += line_screen_rows(buf, pos, win->cols);
            pos = buffer_next_line_start(buf, pos);
        }

        /* Add wrap offset within cursor's line */
        size_t ls = buffer_line_start(buf, buf->point);
        int vcol = visual_cols(buf, ls, buf->point);
        screen_row += vcol / win->cols;

        if (screen_row < visible_rows) break;
        win->top_line++;
    }
}

void window_point_to_screen(Window *win, int *sy, int *sx) {
    Buffer *buf = win->buffer;
    int point_line, point_col;
    buffer_point_to_line_col(buf, buf->point, &point_line, &point_col);

    /* Count screen rows from top_line to point's line */
    int screen_row = 0;
    size_t pos = 0;
    for (int i = 0; i < win->top_line && pos < buffer_length(buf); i++)
        pos = buffer_next_line_start(buf, pos);

    for (int line = win->top_line; line < point_line && pos < buffer_length(buf); line++) {
        screen_row += line_screen_rows(buf, pos, win->cols);
        pos = buffer_next_line_start(buf, pos);
    }

    /* Count visual column of point within its line */
    size_t ls = buffer_line_start(buf, buf->point);
    int vcol = visual_cols(buf, ls, buf->point);

    *sy = win->y + screen_row + (vcol / win->cols);
    *sx = win->x + (vcol % win->cols);
}

/* Split operations */

Window *window_split_below(Window *win) {
    int half = win->rows / 2;
    if (half < 3) return NULL;  /* too small to split */

    Window *new_win = window_create(win->buffer, win->y + half, win->x,
                                     win->rows - half, win->cols);
    win->rows = half;

    new_win->parent = win->parent;
    new_win->top_line = win->top_line;

    /* Register split boundary */
    if (split_count < MAX_SPLITS) {
        splits[split_count].win_before = win;
        splits[split_count].win_after = new_win;
        splits[split_count].is_vertical = false;
        split_count++;
    }

    return new_win;
}

Window *window_split_right(Window *win) {
    int half = win->cols / 2;
    if (half < 10) return NULL;  /* too small to split */

    int orig_right = win->x + win->cols;

    /* Left window loses 1 col for separator */
    win->cols = half - 1;

    /* Right window starts after the separator column */
    Window *new_win = window_create(win->buffer, win->y, win->x + half,
                                     win->rows, orig_right - (win->x + half));

    new_win->parent = win->parent;
    new_win->top_line = win->top_line;

    /* Register split boundary */
    if (split_count < MAX_SPLITS) {
        splits[split_count].win_before = win;
        splits[split_count].win_after = new_win;
        splits[split_count].is_vertical = true;
        split_count++;
    }

    return new_win;
}

void window_delete(Window *win) {
    if (window_count() <= 1) return;  /* can't delete last window */

    /* Find another window to give the space to */
    Window *other = window_next(win);
    if (other == win) return;

    /* Remove any split boundaries involving this window and give space back */
    for (int i = 0; i < split_count; i++) {
        if (splits[i].win_before == win || splits[i].win_after == win) {
            /* Give space to the surviving window */
            if (splits[i].win_before == win) {
                Window *survivor = splits[i].win_after;
                if (splits[i].is_vertical) {
                    int old_right = survivor->x + survivor->cols;
                    survivor->x = win->x;
                    survivor->cols = old_right - win->x;
                } else {
                    int old_bottom = survivor->y + survivor->rows;
                    survivor->y = win->y;
                    survivor->rows = old_bottom - win->y;
                }
            } else {
                Window *survivor = splits[i].win_before;
                if (splits[i].is_vertical) {
                    survivor->cols = (win->x + win->cols) - survivor->x;
                } else {
                    survivor->rows = (win->y + win->rows) - survivor->y;
                }
            }
            /* Remove this boundary */
            splits[i] = splits[split_count - 1];
            split_count--;
            i--;  /* re-check this index */
        }
    }

    if (current_window == win)
        current_window = other;

    window_destroy(win);
}

void window_delete_others(Window *win) {
    Window *w = window_list;
    while (w) {
        Window *next = w->next;
        if (w != win) {
            window_destroy(w);
        }
        w = next;
    }
    current_window = win;
    split_count = 0;  /* all boundaries gone */
}

Window *window_next(Window *win) {
    if (win->next) return win->next;
    return window_list;  /* wrap around */
}

Window *window_at_pos(int y, int x) {
    for (Window *w = window_list; w; w = w->next) {
        if (y >= w->y && y < w->y + w->rows &&
            x >= w->x && x < w->x + w->cols)
            return w;
    }
    return current_window;
}

int window_count(void) {
    int count = 0;
    for (Window *w = window_list; w; w = w->next) count++;
    return count;
}

void window_resize_all(int rows, int cols) {
    int nwin = window_count();
    if (nwin == 0) return;

    if (nwin == 1) {
        Window *w = window_list;
        w->y = 0;
        w->x = 0;
        w->rows = rows;
        w->cols = cols;
        return;
    }

    /* Use split boundaries to proportionally resize */
    for (int i = 0; i < split_count; i++) {
        SplitBoundary *sb = &splits[i];
        if (sb->is_vertical) {
            /* Compute proportional position */
            int total = sb->win_before->cols + 1 + sb->win_after->cols;
            float ratio = (float)sb->win_before->cols / (float)(total - 1);
            int new_total = cols;
            /* Adjust if windows share the same vertical span */
            if (sb->win_before->x == 0 &&
                sb->win_after->x + sb->win_after->cols >= cols) {
                int left_cols = (int)(ratio * (new_total - 1));
                if (left_cols < 5) left_cols = 5;
                if (left_cols > new_total - 6) left_cols = new_total - 6;
                sb->win_before->cols = left_cols;
                sb->win_after->x = sb->win_before->x + left_cols + 1;
                sb->win_after->cols = new_total - left_cols - 1;
            }
            sb->win_before->rows = rows;
            sb->win_after->rows = rows;
            sb->win_before->y = 0;
            sb->win_after->y = 0;
        } else {
            /* Horizontal split - proportional row division */
            int total = sb->win_before->rows + sb->win_after->rows;
            float ratio = (float)sb->win_before->rows / (float)total;
            int new_before = (int)(ratio * rows);
            if (new_before < 3) new_before = 3;
            if (new_before > rows - 3) new_before = rows - 3;
            sb->win_before->rows = new_before;
            sb->win_after->y = sb->win_before->y + new_before;
            sb->win_after->rows = rows - sb->win_after->y;
            sb->win_before->cols = cols;
            sb->win_after->cols = cols;
            sb->win_before->x = 0;
            sb->win_after->x = 0;
        }
    }

    /* Fallback: if no boundaries tracked, do equal division */
    if (split_count == 0) {
        int per_win = rows / nwin;
        int y = 0;
        for (Window *w = window_list; w; w = w->next) {
            w->y = y;
            w->x = 0;
            w->cols = cols;
            if (w->next) {
                w->rows = per_win;
                y += per_win;
            } else {
                w->rows = rows - y;
            }
        }
    }
}

SplitBoundary *window_boundary_at_pos(int y, int x) {
    for (int i = 0; i < split_count; i++) {
        SplitBoundary *sb = &splits[i];
        if (sb->is_vertical) {
            /* Separator column is at win_before->x + win_before->cols */
            int sep_col = sb->win_before->x + sb->win_before->cols;
            if (x == sep_col &&
                y >= sb->win_before->y &&
                y < sb->win_before->y + sb->win_before->rows)
                return sb;
        } else {
            /* Boundary is the modeline of win_before (its last row) */
            int modeline_row = sb->win_before->y + sb->win_before->rows - 1;
            if (y == modeline_row &&
                x >= sb->win_before->x &&
                x < sb->win_before->x + sb->win_before->cols)
                return sb;
        }
    }
    return NULL;
}

void window_draw_separators(void) {
    for (int i = 0; i < split_count; i++) {
        SplitBoundary *sb = &splits[i];
        if (!sb->is_vertical) continue;
        int sep_col = sb->win_before->x + sb->win_before->cols;
        for (int row = sb->win_before->y;
             row < sb->win_before->y + sb->win_before->rows; row++) {
            mvaddch(row, sep_col, ACS_VLINE);
        }
    }
}
