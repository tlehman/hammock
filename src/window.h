#ifndef WINDOW_H
#define WINDOW_H

#include "buffer.h"

typedef struct Window {
    Buffer *buffer;
    int top_line;       /* first visible line */
    int target_col;     /* desired column for vertical movement */

    /* Screen geometry */
    int y, x;
    int rows, cols;

    /* Split tree */
    struct Window *parent;
    struct Window *child1, *child2;
    bool is_vertical_split;  /* true = side by side, false = top/bottom */
    float split_ratio;

    struct Window *next;     /* flat list for cycling */
} Window;

extern Window *window_list;
extern Window *current_window;

Window *window_create(Buffer *buf, int y, int x, int rows, int cols);
void window_destroy(Window *win);
void window_scroll_to_point(Window *win);
void window_point_to_screen(Window *win, int *sy, int *sx);

/* Split operations */
Window *window_split_below(Window *win);
Window *window_split_right(Window *win);
void window_delete(Window *win);
void window_delete_others(Window *win);
Window *window_next(Window *win);
Window *window_at_pos(int y, int x);

/* Resize all windows to fill terminal */
void window_resize_all(int rows, int cols);

/* Count visible windows */
int window_count(void);

/* Split boundary tracking for mouse resize */
typedef struct SplitBoundary {
    Window *win_before;  /* above or left */
    Window *win_after;   /* below or right */
    bool is_vertical;    /* true = side-by-side (vertical separator) */
} SplitBoundary;

/* Returns a split boundary if (y,x) is on a modeline or vertical separator */
SplitBoundary *window_boundary_at_pos(int y, int x);

/* Draw vertical separators between side-by-side windows */
void window_draw_separators(void);

#endif
