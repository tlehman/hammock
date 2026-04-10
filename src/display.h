#ifndef DISPLAY_H
#define DISPLAY_H

#include "window.h"
#include <ncurses.h>

/* Color pairs */
#define COLOR_MODELINE      1
#define COLOR_MODELINE_INACTIVE 2
#define COLOR_MINIBUFFER    3
#define COLOR_REGION        4
#define COLOR_LINENUM       5
#define COLOR_KEYWORD       10
#define COLOR_STRING        11
#define COLOR_COMMENT       12
#define COLOR_TYPE          13
#define COLOR_FUNCTION      14
#define COLOR_NUMBER        15
#define COLOR_PREPROC       16
#define COLOR_HEADING1      20
#define COLOR_HEADING2      21
#define COLOR_HEADING3      22
#define COLOR_LINK          23
#define COLOR_CODE          24
#define COLOR_BOLD_TEXT      25
#define COLOR_ITALIC_TEXT    26
#define COLOR_DIFF_ADD      30
#define COLOR_DIFF_DEL      31
#define COLOR_DIFF_HEADER   32

void display_init(void);
void display_end(void);
void display_refresh_window(Window *win, bool is_current);
void display_modeline(Window *win, bool is_current);
void display_minibuffer(const char *msg);
void display_clear(void);

#endif
