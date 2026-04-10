#include "buffer.h"
#include "window.h"
#include "display.h"
#include "input.h"
#include "keymap.h"
#include "command.h"
#include "sci.h"
#include "mode.h"
#include "markdown.h"
#include "shell.h"
#include "git.h"
#include "news.h"
#include "effects.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <locale.h>
#include <ncurses.h>

static volatile sig_atomic_t got_sigwinch = 0;

static void handle_sigwinch(int sig) {
    (void)sig;
    got_sigwinch = 1;
}

static void self_insert(int ch) {
    if (current_buffer->read_only) {
        message("Buffer is read-only");
        return;
    }
    char c = (char)ch;
    buffer_insert_char(current_buffer, c);
    current_window->target_col = -1;
    need_redisplay = true;
}

/* Mouse drag state */
static Window *drag_origin_window = NULL;
static SplitBoundary *resizing_split = NULL;

/* Compute buffer position from screen coordinates within a window (wrap-aware) */
static size_t screen_to_buffer_pos(Window *win, int screen_y, int screen_x) {
    Buffer *buf = win->buffer;
    int click_row = screen_y - win->y;
    int click_col = screen_x - win->x;
    int visible_rows = win->rows - 1;

    if (click_row < 0) click_row = 0;
    if (click_row >= visible_rows) click_row = visible_rows - 1;
    if (click_col < 0) click_col = 0;

    /* Walk from top_line, counting screen rows with wrapping */
    size_t pos = 0;
    size_t len = buffer_length(buf);
    for (int i = 0; i < win->top_line && pos < len; i++)
        pos = buffer_next_line_start(buf, pos);

    int screen_row = 0;
    while (pos < len && screen_row < visible_rows) {
        size_t line_start = pos;
        size_t line_end = buffer_line_end(buf, pos);

        /* Count visual columns to determine how many screen rows this line uses */
        int vcol = 0;
        for (size_t p = line_start; p < line_end; p++) {
            char ch = buffer_char_at(buf, p);
            if (ch == '\t') vcol += 4 - (vcol % 4);
            else vcol++;
        }
        int rows_for_line = vcol == 0 ? 1 : (vcol + win->cols - 1) / win->cols;

        if (click_row < screen_row + rows_for_line) {
            /* Click is on a screen row belonging to this buffer line */
            int row_within_line = click_row - screen_row;
            int target_vcol = row_within_line * win->cols + click_col;

            int cur_vcol = 0;
            for (size_t p = line_start; p < line_end; p++) {
                if (cur_vcol >= target_vcol) return p;
                char ch = buffer_char_at(buf, p);
                if (ch == '\t') cur_vcol += 4 - (cur_vcol % 4);
                else cur_vcol++;
            }
            return line_end;
        }

        screen_row += rows_for_line;
        pos = (line_end < len) ? line_end + 1 : line_end;
    }

    return pos;
}

static void handle_mouse(void) {
    MouseEvent mev;
    if (!input_get_mouse_event(&mev)) return;

    switch (mev.action) {
    case MOUSE_SCROLL_UP: {
        Window *clicked = window_at_pos(mev.y, mev.x);
        if (clicked && clicked->top_line > 0) {
            clicked->top_line -= 3;
            if (clicked->top_line < 0) clicked->top_line = 0;
            /* Move cursor into visible area if it scrolled off */
            Buffer *buf = clicked->buffer;
            int line, col;
            buffer_point_to_line_col(buf, buf->point, &line, &col);
            int visible_rows = clicked->rows - 1;
            if (line >= clicked->top_line + visible_rows) {
                /* Cursor is below viewport — move it to last visible line */
                size_t pos = 0;
                for (int i = 0; i < clicked->top_line + visible_rows - 1; i++)
                    pos = buffer_next_line_start(buf, pos);
                buf->point = pos;
            }
        }
        need_redisplay = true;
        break;
    }
    case MOUSE_SCROLL_DOWN: {
        Window *clicked = window_at_pos(mev.y, mev.x);
        if (clicked) {
            Buffer *buf = clicked->buffer;
            int total = (int)buffer_line_count(buf);
            int visible_rows = clicked->rows - 1;
            int max_top = total - visible_rows;
            if (max_top < 0) max_top = 0;
            clicked->top_line += 3;
            if (clicked->top_line > max_top) clicked->top_line = max_top;
            /* Move cursor into visible area if it scrolled off */
            int line, col;
            buffer_point_to_line_col(buf, buf->point, &line, &col);
            if (line < clicked->top_line) {
                /* Cursor is above viewport — move it to first visible line */
                size_t pos = 0;
                for (int i = 0; i < clicked->top_line; i++)
                    pos = buffer_next_line_start(buf, pos);
                buf->point = pos;
            }
        }
        need_redisplay = true;
        break;
    }
    case MOUSE_CLICK: {
        /* Check if click is on a split boundary */
        SplitBoundary *sb = window_boundary_at_pos(mev.y, mev.x);
        if (sb) {
            resizing_split = sb;
            drag_origin_window = NULL;
            break;
        }

        /* Normal click: find window, set cursor */
        Window *clicked = window_at_pos(mev.y, mev.x);
        if (!clicked) break;

        current_window = clicked;
        current_buffer = clicked->buffer;

        int visible_rows = clicked->rows - 1;
        int click_row = mev.y - clicked->y;
        if (click_row >= visible_rows) break;  /* clicked on modeline */

        size_t pos = screen_to_buffer_pos(clicked, mev.y, mev.x);
        current_buffer->point = pos;
        current_buffer->mark = pos;
        current_buffer->mark_active = false;
        current_window->target_col = -1;

        drag_origin_window = clicked;
        need_redisplay = true;
        break;
    }
    case MOUSE_DRAG: {
        if (resizing_split) {
            /* Resize split boundary */
            SplitBoundary *sb = resizing_split;
            if (sb->is_vertical) {
                /* Compute right edge before modifying anything */
                int right_edge = sb->win_after->x + sb->win_after->cols;
                int new_sep = mev.x;
                int min_col = sb->win_before->x + 5;
                int max_col = right_edge - 5;
                if (new_sep < min_col) new_sep = min_col;
                if (new_sep > max_col) new_sep = max_col;
                sb->win_before->cols = new_sep - sb->win_before->x;
                sb->win_after->x = new_sep + 1;
                sb->win_after->cols = right_edge - (new_sep + 1);
            } else {
                int new_row = mev.y;
                int min_row = sb->win_before->y + 3;
                int max_row = sb->win_after->y + sb->win_after->rows - 3;
                if (new_row < min_row) new_row = min_row;
                if (new_row > max_row) new_row = max_row;
                int bottom_edge = sb->win_after->y + sb->win_after->rows;
                sb->win_before->rows = new_row - sb->win_before->y;
                sb->win_after->y = new_row;
                sb->win_after->rows = bottom_edge - new_row;
            }
            need_redisplay = true;
            break;
        }

        if (drag_origin_window) {
            /* Text selection drag */
            current_window = drag_origin_window;
            current_buffer = drag_origin_window->buffer;

            /* Auto-scroll at edges */
            int click_row = mev.y - drag_origin_window->y;
            int visible_rows = drag_origin_window->rows - 1;
            if (click_row <= 0 && drag_origin_window->top_line > 0) {
                drag_origin_window->top_line--;
            } else if (click_row >= visible_rows - 1) {
                drag_origin_window->top_line++;
            }

            size_t pos = screen_to_buffer_pos(drag_origin_window, mev.y, mev.x);
            current_buffer->point = pos;
            current_buffer->mark_active = true;
            current_window->target_col = -1;
            need_redisplay = true;
        }
        break;
    }
    case MOUSE_RELEASE: {
        if (resizing_split) {
            resizing_split = NULL;
            need_redisplay = true;
            break;
        }
        if (drag_origin_window) {
            /* If no actual drag happened (mark == point), deactivate region */
            if (current_buffer->mark_active &&
                current_buffer->mark == current_buffer->point) {
                current_buffer->mark_active = false;
            }
            drag_origin_window = NULL;
            need_redisplay = true;
        }
        break;
    }
    case MOUSE_NONE:
        break;
    }
}

int main(int argc, char *argv[]) {
    /* Enable UTF-8 output so ncurses renders multi-byte characters
     * (needed for inline math Unicode substitutions). */
    setlocale(LC_ALL, "");

    /* Initialize C subsystems */
    commands_init();
    shell_commands_init();
    news_init();
    display_init();
    input_init();

    /* Set up signal handler for terminal resize */
    struct sigaction sa;
    sa.sa_handler = handle_sigwinch;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, NULL);

    /* Initialize SCI and load scripting layer */
    bool clj_ok = sci_init();
    if (clj_ok) {
        /* Load Clojure modules in dependency order */
        free(sci_load_file("clj/state.clj"));
        free(sci_load_file("clj/effects.clj"));
        free(sci_load_file("clj/core.clj"));
        free(sci_load_file("clj/git.clj"));
        free(sci_load_file("clj/markdown.clj"));
        free(sci_load_file("clj/commands.clj"));
        free(sci_load_file("clj/keybindings.clj"));
        free(sci_load_file("clj/modes.clj"));

        /* Load keybindings from Clojure */
        char *kb_edn = sci_eval("(hammock.keybindings/export)");
        if (kb_edn) {
            keybindings_load_edn(kb_edn);
            free(kb_edn);
        }

        /* Load modes from Clojure */
        char *modes_edn = sci_eval("(hammock.modes/export)");
        if (modes_edn) {
            modes_load_edn(modes_edn);
            free(modes_edn);
        }

        /* Register Clojure commands in the unified command table */
        char *cmds_edn = sci_eval("(hammock.commands/export-command-metadata)");
        if (cmds_edn) {
            const char *start = strchr(cmds_edn, '[');
            if (start) {
                size_t consumed = 0;
                EdnVal *root = edn_parse(start, strlen(start), &consumed);
                if (root && root->type == EDN_VECTOR) {
                    for (int i = 0; i < root->vec.count; i++) {
                        EdnVal *pair = root->vec.items[i];
                        if (pair && pair->type == EDN_VECTOR && pair->vec.count >= 2 &&
                            pair->vec.items[0]->type == EDN_STRING &&
                            pair->vec.items[1]->type == EDN_STRING) {
                            command_register_clojure(
                                hstrdup(pair->vec.items[0]->str),
                                hstrdup(pair->vec.items[1]->str));
                        }
                    }
                }
                edn_free(root);
            }
            free(cmds_edn);
        }

        /* Install atom watches for live reload */
        free(sci_eval("(hammock.state/install-watches!)"));
    } else {
        /* Fallback: use C-defined keybindings and modes */
        keybindings_init();
        modes_init();
        message("Warning: Could not initialize SCI interpreter");
    }

    /* Always create *scratch* buffer */
    Buffer *scratch = buffer_create("*scratch*");
    {
        const char *welcome =
            "; Welcome to Hammock.\n"
            "; This is the scratch buffer.\n"
            "; Evaluate Clojure expressions with C-j.\n\n";
        buffer_insert_string(scratch, welcome, strlen(welcome));
        scratch->point = buffer_length(scratch);
        scratch->modified = false;
        buffer_set_mode(scratch, MODE_CLOJURE);
    }

    /* Open file if given, otherwise start in scratch */
    Buffer *buf;
    if (argc > 1) {
        const char *path = argv[1];
        const char *name = strrchr(path, '/');
        name = name ? name + 1 : path;
        buf = buffer_create(name);
        if (!buffer_load_file(buf, path)) {
            buf->filename = hstrdup(path);
            message("(New file)");
        }
        buffer_set_mode(buf, mode_detect(path));
    } else {
        buf = scratch;
    }

    current_buffer = buf;

    /* Create initial window */
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    Window *win = window_create(buf, 0, 0, rows - 1, cols);  /* -1 for minibuffer line */
    current_window = win;

    /* Set nodelay: input_read() handles timing via poll() on stdin */
    nodelay(stdscr, TRUE);

    /* Main loop */
    Keymap *pending_keymap = NULL;

    while (editor_running) {
        /* Handle terminal resize */
        if (got_sigwinch) {
            got_sigwinch = 0;
            endwin();
            refresh();
            getmaxyx(stdscr, rows, cols);
            window_resize_all(rows - 1, cols);
            need_redisplay = true;
        }

        /* Read shell output */
        shell_read_all();

        /* Display */
        if (need_redisplay) {
            /* Draw minibuffer FIRST, then windows, so cursor ends up in the active window */
            display_minibuffer(minibuf_message);
            for (Window *w = window_list; w; w = w->next) {
                window_scroll_to_point(w);
                display_refresh_window(w, w == current_window);
            }
            /* Draw vertical separators between side-by-side windows */
            window_draw_separators();
            /* Re-position cursor after separators (mvaddch moves it) */
            {
                int sy, sx;
                window_point_to_screen(current_window, &sy, &sx);
                int vis = current_window->rows - 1;
                if (sx >= current_window->x + current_window->cols)
                    sx = current_window->x + current_window->cols - 1;
                if (sy >= current_window->y + vis)
                    sy = current_window->y + vis - 1;
                move(sy, sx);
            }
            refresh();
            need_redisplay = false;
        }

        /* Check if Clojure config atoms changed (live reload) */
        if (clj_ok) {
            static long long last_version = 0;
            long long ver = sci_get_state_version();
            if (ver > last_version) {
                last_version = ver;
                /* Re-export keybindings */
                char *kb = sci_eval("(hammock.keybindings/export)");
                if (kb) { keybindings_load_edn(kb); free(kb); }
                /* Re-export modes */
                char *md = sci_eval("(hammock.modes/export)");
                if (md) { modes_load_edn(md); free(md); }
                need_redisplay = true;
            }
        }

        /* Read input */
        KeyEvent ev = input_read();

        /* Handle timeout (no input) */
        if (ev.key == ERR || (ev.key == 0 && ev.modifiers == 0)) {
            continue;
        }

        /* Clear message on next keypress */
        minibuf_message[0] = '\0';

        /* Handle mouse */
        if (ev.key == HK_MOUSE) {
            handle_mouse();
            continue;
        }

        /* Handle resize event */
        if (ev.key == KEY_RESIZE) {
            getmaxyx(stdscr, rows, cols);
            window_resize_all(rows - 1, cols);
            need_redisplay = true;
            continue;
        }

        /* Keymap dispatch: check mode keymap first, then global */
        Keymap *submap = NULL;
        const char *cmd = NULL;

        if (pending_keymap) {
            cmd = keymap_lookup(pending_keymap, ev.key, ev.modifiers, &submap);
        } else {
            /* Try mode-specific keymap first */
            MajorModeID mode_id = (MajorModeID)current_buffer->major_mode;
            if (mode_id >= 0 && mode_id < MODE_COUNT && major_modes[mode_id].keymap) {
                /* In writable buffers, skip bare printable-char mode bindings
                 * so they self-insert instead of triggering navigation commands */
                bool is_bare_printable = (ev.modifiers == 0 && ev.key >= 32 && ev.key < 127);
                if (!(is_bare_printable && !current_buffer->read_only)) {
                    cmd = keymap_lookup(major_modes[mode_id].keymap, ev.key, ev.modifiers, &submap);
                }
            }
            /* Fall back to global keymap */
            if (!cmd && !submap) {
                cmd = keymap_lookup(&global_keymap, ev.key, ev.modifiers, &submap);
            }
        }

        if (submap) {
            /* Prefix key - wait for next key */
            pending_keymap = submap;
            message("%s-", key_name(ev.key, ev.modifiers));
            continue;
        }

        pending_keymap = NULL;

        if (cmd) {
            command_dispatch(cmd, clj_ok);
            if (strcmp(cmd, "next-line") != 0 && strcmp(cmd, "previous-line") != 0 &&
                strcmp(cmd, "scroll-down") != 0 && strcmp(cmd, "scroll-up") != 0) {
                current_window->target_col = -1;
            }
        } else if (shell_for_buffer(current_buffer) != NULL) {
            /* In shell mode: send input to PTY */
            if (ev.key == HK_ENTER) {
                char nl = '\n';
                shell_send(current_buffer, &nl, 1);
            } else if (ev.modifiers == MOD_CTRL) {
                /* Send ctrl character */
                char c = (char)(ev.key - 'a' + 1);
                shell_send(current_buffer, &c, 1);
            } else if (ev.modifiers == 0 && ev.key >= 32 && ev.key < 127) {
                char c = (char)ev.key;
                shell_send(current_buffer, &c, 1);
            } else if (ev.key == HK_BACKSPACE) {
                char c = 127;
                shell_send(current_buffer, &c, 1);
            }
        } else if (ev.modifiers == 0 && ev.key >= 32 && ev.key < 127) {
            /* Self-insert for printable characters */
            self_insert(ev.key);
        } else {
            message("%s is undefined", key_name(ev.key, ev.modifiers));
        }
    }

    /* Cleanup */
    sci_shutdown();
    display_end();

    /* Free all buffers */
    while (buffer_list) {
        buffer_destroy(buffer_list);
    }

    /* Free all windows */
    while (window_list) {
        window_destroy(window_list);
    }

    return 0;
}
