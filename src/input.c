#include "input.h"
#include <string.h>
#include <unistd.h>
#include <poll.h>

static bool button1_down = false;

/* SGR mouse parsing: read raw bytes from stdin before ncurses can swallow them */
static MouseEvent sgr_mouse;
static bool sgr_mouse_pending = false;

/* Try to parse one SGR mouse sequence starting at buf[0] which should be ESC.
 * Returns bytes consumed, or 0 if not an SGR mouse sequence. */
static int try_parse_sgr(const unsigned char *buf, int len) {
    if (len < 6) return 0;
    if (buf[0] != 0x1B || buf[1] != '[' || buf[2] != '<') return 0;

    /* Find the terminator M or m after the '<' */
    int end = -1;
    for (int i = 3; i < len; i++) {
        if (buf[i] == 'M' || buf[i] == 'm') {
            end = i;
            break;
        }
    }
    if (end < 0) return 0;

    /* Parse button;col;row */
    char seq[64];
    int param_len = end - 3;
    if (param_len >= (int)sizeof(seq)) return 0;
    memcpy(seq, buf + 3, param_len);
    seq[param_len] = '\0';
    char final = buf[end];

    int button = 0, col = 0, row = 0;
    if (sscanf(seq, "%d;%d;%d", &button, &col, &row) != 3) return 0;

    sgr_mouse.x = col - 1;  /* SGR coordinates are 1-based */
    sgr_mouse.y = row - 1;

    if (button == 64) {
        sgr_mouse.action = MOUSE_SCROLL_UP;
    } else if (button == 65) {
        sgr_mouse.action = MOUSE_SCROLL_DOWN;
    } else if (button == 0 && final == 'M') {
        sgr_mouse.action = MOUSE_CLICK;
        button1_down = true;
    } else if (button == 0 && final == 'm') {
        sgr_mouse.action = MOUSE_RELEASE;
        button1_down = false;
    } else if (button == 32) {
        sgr_mouse.action = MOUSE_DRAG;
    } else {
        return 0;
    }

    sgr_mouse_pending = true;
    return end + 1;
}

void input_init(void) {
    /* Don't use mousemask() — macOS ncurses can't decode SGR mouse events.
     * We parse SGR sequences ourselves from raw stdin reads. */
    printf("\033[?1002h\033[?1006h");
    fflush(stdout);
}

KeyEvent input_read(void) {
    KeyEvent ev = {0, 0};

    /* Read raw bytes from stdin before ncurses sees them,
     * so we can intercept SGR mouse sequences that macOS ncurses can't parse */
    struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
    int pr = poll(&pfd, 1, 16);  /* 16ms timeout (~60fps) */

    if (pr <= 0) {
        ev.key = ERR;
        return ev;
    }

    unsigned char raw[1024];
    ssize_t n = read(STDIN_FILENO, raw, sizeof(raw));

    if (n <= 0) {
        ev.key = ERR;
        return ev;
    }

    /* Scan the buffer for SGR mouse sequences.
     * Rapid trackpad scrolling sends many back-to-back. */
    bool got_mouse = false;
    ssize_t pos = 0;

    while (pos < n) {
        int consumed = try_parse_sgr(raw + pos, (int)(n - pos));
        if (consumed > 0) {
            pos += consumed;
            got_mouse = true;
        } else {
            break;
        }
    }

    if (got_mouse) {
        /* Discard everything else in the buffer — any remaining bytes are
         * fragments of SGR sequences. Also flush ncurses' internal buffer
         * to clear garbage from previous reads. */
        flushinp();
        ev.key = HK_MOUSE;
        return ev;
    }

    /* No SGR mouse — push all bytes into ncurses input buffer */
    for (ssize_t i = n - 1; i >= 0; i--)
        ungetch(raw[i]);

    int ch = getch();

    if (ch == ERR) {
        ev.key = ERR;
        return ev;
    }

    /* Handle escape sequences for Meta key */
    if (ch == 27) {
        /* In nodelay mode, getch() returns immediately */
        int next = getch();

        if (next == ERR) {
            /* Just Escape by itself */
            ev.key = 'g';
            ev.modifiers = MOD_CTRL;  /* treat ESC as C-g */
            return ev;
        }

        /* Meta + key */
        ev.modifiers = MOD_META;
        ch = next;
    }

    /* Map ncurses keys to our key codes */
    switch (ch) {
        case KEY_BACKSPACE:
        case 127:
            /* Byte 8 (^H) falls through to the default case so it maps to
             * C-h; modern terminals send 127 or KEY_BACKSPACE for the
             * Backspace key, so this keeps Backspace working while freeing
             * C-h to be a prefix. */
            ev.key = HK_BACKSPACE;
            ev.modifiers &= ~MOD_META;  /* backspace, not M-backspace usually */
            break;
        case KEY_DC:
            ev.key = HK_DELETE;
            break;
        case KEY_UP:
            ev.key = HK_UP;
            break;
        case KEY_DOWN:
            ev.key = HK_DOWN;
            break;
        case KEY_LEFT:
            ev.key = HK_LEFT;
            break;
        case KEY_RIGHT:
            ev.key = HK_RIGHT;
            break;
        case KEY_HOME:
            ev.key = HK_HOME;
            break;
        case KEY_END:
            ev.key = HK_END;
            break;
        case KEY_PPAGE:
            ev.key = HK_PGUP;
            break;
        case KEY_NPAGE:
            ev.key = HK_PGDN;
            break;
        case KEY_ENTER:
        case '\r':
            ev.key = HK_ENTER;
            break;
        case '\t':
            ev.key = HK_TAB;
            break;
        case KEY_BTAB:
            ev.key = HK_SHIFT_TAB;
            break;
        case KEY_MOUSE:
            /* Shouldn't happen — we parse SGR mouse from raw stdin */
            ev.key = ERR;
            break;
        case KEY_RESIZE:
            ev.key = KEY_RESIZE;
            break;
        case KEY_F(1):
            ev.key = HK_F1;
            break;
        default:
            if (ch >= 1 && ch <= 26 && !(ev.modifiers & MOD_META)) {
                /* Ctrl+letter */
                ev.key = ch + 'a' - 1;
                ev.modifiers |= MOD_CTRL;
            } else if (ch == 0) {
                /* C-Space */
                ev.key = ' ';
                ev.modifiers |= MOD_CTRL;
            } else {
                ev.key = ch;
            }
            break;
    }

    return ev;
}

bool input_get_mouse_event(MouseEvent *mev) {
    if (sgr_mouse_pending) {
        sgr_mouse_pending = false;
        *mev = sgr_mouse;
        return true;
    }
    mev->action = MOUSE_NONE;
    return false;
}
