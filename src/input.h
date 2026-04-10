#ifndef INPUT_H
#define INPUT_H

#include "keymap.h"
#include <ncurses.h>

/* Initialize input system */
void input_init(void);

/* Read a key event from ncurses, translating to our KeyEvent */
KeyEvent input_read(void);

/* Mouse event abstraction */
typedef enum {
    MOUSE_NONE,
    MOUSE_CLICK,
    MOUSE_DRAG,
    MOUSE_RELEASE,
    MOUSE_SCROLL_UP,
    MOUSE_SCROLL_DOWN
} MouseAction;

typedef struct {
    MouseAction action;
    int y, x;
} MouseEvent;

/* Get a high-level mouse event (translates from raw MEVENT) */
bool input_get_mouse_event(MouseEvent *mev);

#endif
