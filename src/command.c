#include "command.h"
#include "buffer.h"
#include "window.h"
#include "display.h"
#include "keymap.h"
#include "sci.h"
#include "effects.h"
#include "mode.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <ncurses.h>

CommandEntry command_table[MAX_COMMANDS];
int command_count = 0;
char minibuf_message[256] = "";
bool editor_running = true;
bool need_redisplay = true;

KillRing kill_ring;

void command_register(const char *name, CommandFn fn, const char *docstring) {
    if (command_count >= MAX_COMMANDS) return;
    command_table[command_count].name = name;
    command_table[command_count].fn = fn;
    command_table[command_count].docstring = docstring;
    command_table[command_count].source = "C";
    command_table[command_count].dispatch = CMD_C_NATIVE;
    command_count++;
}

void command_register_clojure(const char *name, const char *docstring) {
    /* Skip if a C-native version already exists (C stays as fast path) */
    if (command_find(name)) return;
    if (command_count >= MAX_COMMANDS) return;
    command_table[command_count].name = name;
    command_table[command_count].fn = NULL;
    command_table[command_count].docstring = docstring ? docstring : "";
    command_table[command_count].source = "Clojure";
    command_table[command_count].dispatch = CMD_CLOJURE;
    command_count++;
}

CommandEntry *command_find(const char *name) {
    for (int i = 0; i < command_count; i++) {
        if (strcmp(command_table[i].name, name) == 0)
            return &command_table[i];
    }
    return NULL;
}

CommandFn command_lookup(const char *name) {
    CommandEntry *entry = command_find(name);
    return entry ? entry->fn : NULL;
}

void command_execute(const char *name) {
    CommandEntry *entry = command_find(name);
    if (entry && entry->fn) {
        entry->fn();
        need_redisplay = true;
    } else {
        message("Unknown command: %s", name);
    }
}

void command_dispatch(const char *name, bool clj_available) {
    CommandEntry *entry = command_find(name);
    if (!entry) {
        message("Unknown command: %s", name);
        return;
    }

    if (entry->dispatch == CMD_C_NATIVE || !clj_available) {
        if (entry->fn) {
            entry->fn();
            need_redisplay = true;
        } else {
            message("Command '%s' requires Clojure", name);
        }
    } else {
        /* Clojure dispatch via SCI */
        state_push_snapshot();
        char dispatch_code[256];
        snprintf(dispatch_code, sizeof(dispatch_code),
                 "(hammock.commands/dispatch \"%s\")", name);
        char *effects_edn = sci_eval(dispatch_code);
        if (effects_edn) {
            effects_execute(effects_edn);
            free(effects_edn);
        }
        need_redisplay = true;
    }
}

void message(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(minibuf_message, sizeof(minibuf_message), fmt, ap);
    va_end(ap);
    need_redisplay = true;
}

/* ---- Built-in commands ---- */

static void cmd_forward_char(void) {
    Buffer *buf = current_buffer;
    if (buf->point < buffer_length(buf)) {
        buf->point++;
        current_window->target_col = -1;
    }
}

static void cmd_backward_char(void) {
    Buffer *buf = current_buffer;
    if (buf->point > 0) {
        buf->point--;
        current_window->target_col = -1;
    }
}

static void cmd_next_line(void) {
    Buffer *buf = current_buffer;
    int line, col;
    buffer_point_to_line_col(buf, buf->point, &line, &col);

    if (current_window->target_col >= 0)
        col = current_window->target_col;
    else
        current_window->target_col = col;

    size_t next = buffer_next_line_start(buf, buf->point);
    if (next == buf->point && buffer_char_at(buf, buf->point) != '\n') {
        /* Already on last line */
        return;
    }
    /* Move to target column on next line */
    size_t end = buffer_line_end(buf, next);
    size_t target = next + (size_t)col;
    if (target > end) target = end;
    buf->point = target;
}

static void cmd_previous_line(void) {
    Buffer *buf = current_buffer;
    int line, col;
    buffer_point_to_line_col(buf, buf->point, &line, &col);

    if (current_window->target_col >= 0)
        col = current_window->target_col;
    else
        current_window->target_col = col;

    if (line == 0) return;
    size_t prev = buffer_prev_line_start(buf, buf->point);
    size_t end = buffer_line_end(buf, prev);
    size_t target = prev + (size_t)col;
    if (target > end) target = end;
    buf->point = target;
}

static void cmd_beginning_of_line(void) {
    current_buffer->point = buffer_line_start(current_buffer, current_buffer->point);
    current_window->target_col = -1;
}

static void cmd_end_of_line(void) {
    current_buffer->point = buffer_line_end(current_buffer, current_buffer->point);
    current_window->target_col = -1;
}

static void cmd_beginning_of_buffer(void) {
    current_buffer->point = 0;
    current_window->target_col = -1;
}

static void cmd_end_of_buffer(void) {
    current_buffer->point = buffer_length(current_buffer);
    current_window->target_col = -1;
}

static void cmd_forward_word(void) {
    current_buffer->point = buffer_forward_word(current_buffer, current_buffer->point);
    current_window->target_col = -1;
}

static void cmd_backward_word(void) {
    current_buffer->point = buffer_backward_word(current_buffer, current_buffer->point);
    current_window->target_col = -1;
}

static void cmd_scroll_down(void) {
    int visible = current_window->rows - 2;
    if (visible < 1) visible = 1;
    for (int i = 0; i < visible; i++)
        cmd_next_line();
}

static void cmd_scroll_up(void) {
    int visible = current_window->rows - 2;
    if (visible < 1) visible = 1;
    for (int i = 0; i < visible; i++)
        cmd_previous_line();
}

static void cmd_delete_backward_char(void) {
    if (current_buffer->read_only) { message("Buffer is read-only"); return; }
    Buffer *buf = current_buffer;
    if (buf->mark_active) {
        size_t start = buf->mark < buf->point ? buf->mark : buf->point;
        size_t end = buf->mark > buf->point ? buf->mark : buf->point;
        buffer_delete_region(buf, start, end - start);
        buf->mark_active = false;
    } else {
        buffer_delete_backward(buf);
    }
}

static void cmd_delete_char(void) {
    if (current_buffer->read_only) { message("Buffer is read-only"); return; }
    Buffer *buf = current_buffer;
    if (buf->mark_active) {
        size_t start = buf->mark < buf->point ? buf->mark : buf->point;
        size_t end = buf->mark > buf->point ? buf->mark : buf->point;
        buffer_delete_region(buf, start, end - start);
        buf->mark_active = false;
    } else {
        buffer_delete_forward(buf);
    }
}

static void cmd_newline(void) {
    if (current_buffer->read_only) { message("Buffer is read-only"); return; }
    buffer_insert_char(current_buffer, '\n');
    current_window->target_col = -1;
}

static void cmd_self_insert_tab(void) {
    if (current_buffer->read_only) { message("Buffer is read-only"); return; }
    /* Insert spaces to next tab stop */
    int line, col;
    buffer_point_to_line_col(current_buffer, current_buffer->point, &line, &col);
    int spaces = 4 - (col % 4);
    for (int i = 0; i < spaces; i++)
        buffer_insert_char(current_buffer, ' ');
}

static void cmd_kill_line(void) {
    if (current_buffer->read_only) { message("Buffer is read-only"); return; }
    Buffer *buf = current_buffer;
    size_t end = buffer_line_end(buf, buf->point);
    if (end == buf->point && end < buffer_length(buf)) {
        /* At end of line, kill the newline */
        end++;
    }
    if (end > buf->point) {
        char *text = buffer_region(buf, buf->point, end);
        kill_ring_push(&kill_ring, text);
        clipboard_copy(text);
        buffer_delete_region(buf, buf->point, end - buf->point);
        free(text);
    }
}

static void cmd_set_mark(void) {
    current_buffer->mark = current_buffer->point;
    current_buffer->mark_active = true;
    message("Mark set");
}

static void cmd_kill_region(void) {
    if (current_buffer->read_only) { message("Buffer is read-only"); return; }
    Buffer *buf = current_buffer;
    if (!buf->mark_active) { message("No mark set"); return; }

    size_t start = buf->mark < buf->point ? buf->mark : buf->point;
    size_t end = buf->mark > buf->point ? buf->mark : buf->point;
    char *text = buffer_region(buf, start, end);
    kill_ring_push(&kill_ring, text);
    clipboard_copy(text);

    buffer_delete_region(buf, start, end - start);
    buf->mark_active = false;
    free(text);
}

static void cmd_kill_ring_save(void) {
    Buffer *buf = current_buffer;
    if (!buf->mark_active) { message("No mark set"); return; }

    size_t start = buf->mark < buf->point ? buf->mark : buf->point;
    size_t end = buf->mark > buf->point ? buf->mark : buf->point;
    char *text = buffer_region(buf, start, end);
    kill_ring_push(&kill_ring, text);
    clipboard_copy(text);
    buf->mark_active = false;
    free(text);
    message("Region saved");
}

static void cmd_yank(void) {
    if (current_buffer->read_only) { message("Buffer is read-only"); return; }
    char *clip = clipboard_paste();
    const char *kr_top = kill_ring_top(&kill_ring);
    const char *text;
    if (clip && (!kr_top || strcmp(clip, kr_top) != 0)) {
        kill_ring_push(&kill_ring, clip);
        text = kill_ring_top(&kill_ring);
    } else {
        text = kr_top;
    }
    free(clip);
    if (text) {
        buffer_insert_string(current_buffer, text, strlen(text));
    } else {
        message("Kill ring is empty");
    }
}

static void cmd_undo(void) {
    if (current_buffer->read_only) { message("Buffer is read-only"); return; }
    current_buffer->undo_inhibit = true;
    UndoEntry *e = undo_pop(&current_buffer->undo);
    if (!e) { current_buffer->undo_inhibit = false; message("No further undo information"); return; }

    /* Skip boundaries */
    while (e && e->type == UNDO_BOUNDARY) {
        undo_entry_free(e);
        e = undo_pop(&current_buffer->undo);
    }
    if (!e) { current_buffer->undo_inhibit = false; return; }

    if (e->type == UNDO_INSERT) {
        /* Was an insert, so delete it */
        current_buffer->point = e->pos;
        for (size_t i = 0; i < e->len; i++)
            buffer_delete_forward(current_buffer);
    } else if (e->type == UNDO_DELETE) {
        /* Was a delete, so re-insert */
        current_buffer->point = e->pos;
        buffer_insert_string(current_buffer, e->text, e->len);
        current_buffer->point = e->pos + e->len;
    }
    undo_entry_free(e);
    current_buffer->undo_inhibit = false;
}

static void cmd_keyboard_quit(void) {
    current_buffer->mark_active = false;
    message("Quit");
}

static void cmd_save_buffer(void) {
    if (!current_buffer->filename) {
        message("No file name");
        return;
    }
    if (buffer_save_file(current_buffer)) {
        message("Wrote %s", current_buffer->filename);
    } else {
        message("Error writing %s", current_buffer->filename);
    }
}

/* Completion: buffer names */
int complete_buffer_name(const char *input, const char **candidates, int max_cand) {
    int n = 0;
    size_t ilen = strlen(input);
    for (Buffer *b = buffer_list; b && n < max_cand; b = b->next) {
        if (ilen == 0 || strncmp(b->name, input, ilen) == 0)
            candidates[n++] = b->name;
    }
    return n;
}

/* Completion: command names */
int complete_command_name(const char *input, const char **candidates, int max_cand) {
    int n = 0;
    size_t ilen = strlen(input);
    for (int i = 0; i < command_count && n < max_cand; i++) {
        if (ilen == 0 || strncmp(command_table[i].name, input, ilen) == 0)
            candidates[n++] = command_table[i].name;
    }
    return n;
}

/* Completion: file names */
int complete_file_name(const char *input, const char **candidates, int max_cand) {
    static char file_candidates[64][256];
    int n = 0;

    /* Determine directory and prefix */
    char dir[512] = ".";
    const char *prefix = input;
    const char *last_slash = strrchr(input, '/');
    if (last_slash) {
        size_t dlen = (size_t)(last_slash - input);
        if (dlen > 0 && dlen < sizeof(dir)) {
            memcpy(dir, input, dlen);
            dir[dlen] = '\0';
        }
        prefix = last_slash + 1;
    }

    /* Use popen to list directory entries */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "ls -1 '%s' 2>/dev/null", dir);
    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;

    size_t plen = strlen(prefix);
    char line[256];
    while (fgets(line, sizeof(line), fp) && n < max_cand && n < 64) {
        size_t llen = strlen(line);
        if (llen > 0 && line[llen - 1] == '\n') line[--llen] = '\0';
        if (plen == 0 || strncmp(line, prefix, plen) == 0) {
            if (last_slash) {
                snprintf(file_candidates[n], sizeof(file_candidates[n]),
                         "%.*s/%s", (int)(last_slash - input), input, line);
            } else {
                strncpy(file_candidates[n], line, sizeof(file_candidates[n]) - 1);
            }
            candidates[n] = file_candidates[n];
            n++;
        }
    }
    pclose(fp);
    return n;
}

/* Find longest common prefix among candidates */
static int common_prefix_len(const char **candidates, int count, int start_from) {
    if (count <= 0) return start_from;
    int prefix_len = (int)strlen(candidates[0]);
    for (int i = 1; i < count; i++) {
        int j = start_from;
        while (j < prefix_len && candidates[i][j] && candidates[0][j] == candidates[i][j])
            j++;
        prefix_len = j;
    }
    return prefix_len;
}

/* Minibuffer input with optional tab completion */
char *minibuffer_read(const char *prompt, CompletionFn complete) {
    static char input[512];
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    move(rows - 1, 0);
    clrtoeol();
    addstr(prompt);
    refresh();

    /* Use blocking input while in minibuffer prompt */
    wtimeout(stdscr, -1);
    int pos = 0;
    input[0] = '\0';

    while (1) {
        int ch = getch();
        if (ch == ERR) continue;
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            break;
        } else if (ch == 7 || ch == 27) { /* C-g or Escape */
            wtimeout(stdscr, 16);
            move(rows - 1, 0);
            clrtoeol();
            refresh();
            return NULL;
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (pos > 0) {
                pos--;
                input[pos] = '\0';
                move(rows - 1, 0);
                clrtoeol();
                addstr(prompt);
                addstr(input);
            }
        } else if (ch == '\t' && complete) {
            /* Tab completion */
            const char *candidates[128];
            int count = complete(input, candidates, 128);

            if (count == 0) {
                /* No matches - flash */
                beep();
            } else if (count == 1) {
                /* Unique match - complete fully */
                strncpy(input, candidates[0], sizeof(input) - 1);
                input[sizeof(input) - 1] = '\0';
                pos = (int)strlen(input);
                move(rows - 1, 0);
                clrtoeol();
                addstr(prompt);
                addstr(input);
            } else {
                /* Multiple matches - complete common prefix */
                int cplen = common_prefix_len(candidates, count, pos);
                if (cplen > pos) {
                    memcpy(input, candidates[0], (size_t)cplen);
                    input[cplen] = '\0';
                    pos = cplen;
                    move(rows - 1, 0);
                    clrtoeol();
                    addstr(prompt);
                    addstr(input);
                } else {
                    /* Show candidates in minibuffer area */
                    move(rows - 1, 0);
                    clrtoeol();
                    char display[512];
                    int dpos = 0;
                    dpos += snprintf(display + dpos, sizeof(display) - (size_t)dpos, "{");
                    for (int i = 0; i < count && dpos < (int)sizeof(display) - 10; i++) {
                        if (i > 0) dpos += snprintf(display + dpos, sizeof(display) - (size_t)dpos, ", ");
                        dpos += snprintf(display + dpos, sizeof(display) - (size_t)dpos, "%s", candidates[i]);
                    }
                    snprintf(display + dpos, sizeof(display) - (size_t)dpos, "}");
                    addnstr(display, cols);
                    refresh();
                    /* Wait for any key, then redraw prompt */
                    getch();
                    move(rows - 1, 0);
                    clrtoeol();
                    addstr(prompt);
                    addstr(input);
                }
            }
        } else if (ch >= 32 && ch < 127 && pos < 510) {
            input[pos++] = (char)ch;
            input[pos] = '\0';
            addch((chtype)ch);
        }
        refresh();
    }
    /* Restore polling timeout */
    wtimeout(stdscr, 16);
    move(rows - 1, 0);
    clrtoeol();
    refresh();
    return input;
}

static void cmd_find_file(void) {
    char *path = minibuffer_read("Find file: ", complete_file_name);
    if (!path) { message("Quit"); return; }

    /* Check if already open */
    Buffer *buf = buffer_find_file(path);
    if (buf) {
        current_window->buffer = buf;
        current_buffer = buf;
        return;
    }

    /* Create new buffer */
    /* Use basename for buffer name */
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;

    buf = buffer_create(name);
    if (!buffer_load_file(buf, path)) {
        /* New file */
        buf->filename = hstrdup(path);
        message("(New file)");
    }
    buffer_set_mode(buf, mode_detect(path));
    current_window->buffer = buf;
    current_buffer = buf;
}

static void cmd_switch_to_buffer(void) {
    char *name = minibuffer_read("Switch to buffer: ", complete_buffer_name);
    if (!name) { message("Quit"); return; }

    Buffer *buf = buffer_find(name);
    if (buf) {
        current_window->buffer = buf;
        current_buffer = buf;
    } else {
        message("No buffer named %s", name);
    }
}

static void cmd_kill_buffer(void) {
    char prompt[256];
    snprintf(prompt, sizeof(prompt), "Kill buffer (%s): ", current_buffer->name);
    char *name = minibuffer_read(prompt, complete_buffer_name);
    if (!name) { message("Quit"); return; }

    Buffer *buf;
    if (name[0] == '\0')
        buf = current_buffer;
    else
        buf = buffer_find(name);

    if (!buf) { message("No buffer named %s", name); return; }

    if (buf->modified) {
        message("Buffer %s modified; not killing", buf->name);
        return;
    }

    /* Switch to another buffer if killing current */
    if (buf == current_buffer) {
        Buffer *other = buffer_list;
        if (other == buf) other = buf->next;
        if (!other) {
            other = buffer_create("*scratch*");
        }
        current_window->buffer = other;
        current_buffer = other;
    }
    buffer_destroy(buf);
}

static void cmd_save_buffers_kill_emacs(void) {
    /* Check for modified buffers */
    for (Buffer *b = buffer_list; b; b = b->next) {
        if (b->modified && b->filename) {
            char prompt[256];
            snprintf(prompt, sizeof(prompt), "Save %s? (y/n): ", b->name);
            char *ans = minibuffer_read(prompt, NULL);
            if (ans && (ans[0] == 'y' || ans[0] == 'Y')) {
                buffer_save_file(b);
            }
        }
    }
    editor_running = false;
}

static void cmd_other_window(void) {
    if (window_count() <= 1) {
        message("Only one window");
        return;
    }
    current_window = window_next(current_window);
    current_buffer = current_window->buffer;
}

static void cmd_split_window_below(void) {
    Window *new_win = window_split_below(current_window);
    if (new_win) {
        message("");
    } else {
        message("Window too small to split");
    }
}

static void cmd_split_window_right(void) {
    Window *new_win = window_split_right(current_window);
    if (new_win) {
        message("");
    } else {
        message("Window too small to split");
    }
}

static void cmd_delete_window(void) {
    if (window_count() <= 1) {
        message("Cannot delete sole window");
        return;
    }
    window_delete(current_window);
    /* Recalculate layout */
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    window_resize_all(rows - 1, cols);  /* -1 for minibuffer */
}

static void cmd_delete_other_windows(void) {
    window_delete_others(current_window);
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    current_window->y = 0;
    current_window->x = 0;
    current_window->rows = rows - 1;
    current_window->cols = cols;
}

/* Incremental search (simple version) */
static void cmd_isearch_forward(void) {
    char search[256] = "";
    int pos = 0;
    size_t origin = current_buffer->point;
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)cols;

    while (1) {
        move(rows - 1, 0);
        clrtoeol();
        printw("I-search: %s", search);
        refresh();

        int ch = getch();
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            break;
        } else if (ch == 7) { /* C-g */
            current_buffer->point = origin;
            message("Quit");
            return;
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (pos > 0) {
                pos--;
                search[pos] = '\0';
            }
        } else if (ch == 19) { /* C-s again - next match */
            if (pos > 0) {
                current_buffer->point++;
            }
        } else if (ch >= 32 && ch < 127 && pos < 254) {
            search[pos++] = (char)ch;
            search[pos] = '\0';
        }

        /* Search forward from current point */
        if (pos > 0) {
            size_t len = buffer_length(current_buffer);
            bool found = false;
            for (size_t i = current_buffer->point; i + (size_t)pos <= len; i++) {
                bool match = true;
                for (int j = 0; j < pos; j++) {
                    if (buffer_char_at(current_buffer, i + (size_t)j) != search[j]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    current_buffer->point = i;
                    found = true;
                    break;
                }
            }
            if (!found) {
                /* Wrap around */
                for (size_t i = 0; i + (size_t)pos <= current_buffer->point; i++) {
                    bool match = true;
                    for (int j = 0; j < pos; j++) {
                        if (buffer_char_at(current_buffer, i + (size_t)j) != search[j]) {
                            match = false;
                            break;
                        }
                    }
                    if (match) {
                        current_buffer->point = i;
                        found = true;
                        break;
                    }
                }
            }

            /* Redraw to show match */
            window_scroll_to_point(current_window);
            display_refresh_window(current_window, true);
        }
    }
    message("");
}

static void cmd_isearch_backward(void) {
    /* Simple backward search - reuse forward with direction flag */
    cmd_isearch_forward(); /* TODO: implement proper backward */
}

/* Clojure eval-last-sexp (C-j) */
static void cmd_eval_last_sexp(void) {
    Buffer *buf = current_buffer;
    char *text = buffer_contents(buf);
    char *sexp = clojure_extract_sexp(text, buf->point);
    free(text);

    if (!sexp) {
        message("No sexp before point");
        return;
    }

    char *result = sci_eval(sexp);
    free(sexp);

    if (result) {
        /* Insert result into *scratch* buffer */
        Buffer *scratch = buffer_find("*scratch*");
        if (!scratch) {
            scratch = buffer_create("*scratch*");
            buffer_set_mode(scratch, MODE_CLOJURE);
        }
        scratch->point = buffer_length(scratch);
        char *insertion = hmalloc(strlen(result) + 8);
        sprintf(insertion, "\n; => %s", result);
        buffer_insert_string(scratch, insertion, strlen(insertion));
        free(insertion);
        message("=> %s", result);
        free(result);
    }
}

/* Buffer list commands moved to clj/commands.clj */

/* M-x: execute command by name */
static void cmd_execute_extended_command(void) {
    char *name = minibuffer_read("M-x ", complete_command_name);
    if (!name || name[0] == '\0') { message("Quit"); return; }
    command_dispatch(name, sci_is_ready());
}

/* ---- Find Definition (M-.) ---- */

/* Location stack for jumping back with M-, */
#define LOCATION_STACK_SIZE 32
typedef struct {
    char filename[512];
    size_t point;
} Location;

static Location location_stack[LOCATION_STACK_SIZE];
static int location_top = -1;

static void location_push(void) {
    if (location_top < LOCATION_STACK_SIZE - 1) {
        location_top++;
    } else {
        /* Shift stack down, dropping oldest */
        memmove(&location_stack[0], &location_stack[1],
                sizeof(Location) * (LOCATION_STACK_SIZE - 1));
    }
    Location *loc = &location_stack[location_top];
    if (current_buffer->filename)
        snprintf(loc->filename, sizeof(loc->filename), "%s", current_buffer->filename);
    else
        snprintf(loc->filename, sizeof(loc->filename), "%s", current_buffer->name);
    loc->point = current_buffer->point;
}

static void cmd_pop_mark(void) {
    if (location_top < 0) {
        message("Location stack empty");
        return;
    }
    Location *loc = &location_stack[location_top--];

    /* Try to find buffer by filename first, then by name */
    Buffer *buf = buffer_find_file(loc->filename);
    if (!buf) buf = buffer_find(loc->filename);
    if (!buf) {
        /* File not open, try to open it */
        const char *name = strrchr(loc->filename, '/');
        name = name ? name + 1 : loc->filename;
        buf = buffer_create(name);
        if (!buffer_load_file(buf, loc->filename)) {
            message("Cannot open %s", loc->filename);
            buffer_destroy(buf);
            return;
        }
        buffer_set_mode(buf, mode_detect(loc->filename));
    }
    current_window->buffer = buf;
    current_buffer = buf;
    buf->point = loc->point;
    if (buf->point > buffer_length(buf))
        buf->point = buffer_length(buf);
}

/* Extract the identifier/symbol under the cursor */
static bool is_c_ident(char ch) {
    return isalnum(ch) || ch == '_';
}

static char *word_at_point(void) {
    Buffer *buf = current_buffer;
    size_t pos = buf->point;
    size_t len = buffer_length(buf);
    MajorModeID mode = (MajorModeID)buf->major_mode;

    /* Pick the right character test for the mode */
    bool (*is_word)(char) = is_c_ident;
    if (mode == MODE_CLOJURE)
        is_word = clojure_is_symbol_char;

    /* If point is on a non-word char, try one position back */
    if (pos < len && !is_word(buffer_char_at(buf, pos))) {
        if (pos > 0 && is_word(buffer_char_at(buf, pos - 1)))
            pos--;
        else
            return NULL;
    }

    /* Scan backward to start of word */
    size_t start = pos;
    while (start > 0 && is_word(buffer_char_at(buf, start - 1)))
        start--;

    /* Scan forward to end of word */
    size_t end = pos;
    while (end < len && is_word(buffer_char_at(buf, end)))
        end++;

    if (start == end) return NULL;
    return buffer_region(buf, start, end);
}

/* Navigate to a file:line location */
static void goto_file_line(const char *filepath, int line) {
    location_push();

    Buffer *buf = buffer_find_file(filepath);
    if (!buf) {
        const char *name = strrchr(filepath, '/');
        name = name ? name + 1 : filepath;
        buf = buffer_create(name);
        if (!buffer_load_file(buf, filepath)) {
            buf->filename = hstrdup(filepath);
        }
        buffer_set_mode(buf, mode_detect(filepath));
    }
    current_window->buffer = buf;
    current_buffer = buf;

    /* Jump to line */
    buf->point = 0;
    for (int i = 1; i < line && buf->point < buffer_length(buf); i++)
        buf->point = buffer_next_line_start(buf, buf->point);
}

/* Parse first grep result line "file:line:text" */
static bool parse_grep_result(const char *line, char *filepath, size_t fpsize, int *lineno) {
    /* Format: filepath:lineno:text */
    const char *first_colon = strchr(line, ':');
    if (!first_colon) return false;
    const char *second_colon = strchr(first_colon + 1, ':');
    if (!second_colon) return false;

    size_t pathlen = (size_t)(first_colon - line);
    if (pathlen >= fpsize) pathlen = fpsize - 1;
    memcpy(filepath, line, pathlen);
    filepath[pathlen] = '\0';

    *lineno = atoi(first_colon + 1);
    return *lineno > 0;
}

/* Run a grep command and jump to best match, or show candidates */
static bool grep_and_jump(const char *cmd, const char *symbol) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return false;

    char results[20][1024];
    int count = 0;
    while (count < 20 && fgets(results[count], sizeof(results[0]), fp)) {
        /* Trim newline */
        size_t len = strlen(results[count]);
        if (len > 0 && results[count][len - 1] == '\n')
            results[count][len - 1] = '\0';
        if (results[count][0])
            count++;
    }
    pclose(fp);

    if (count == 0) return false;

    if (count == 1) {
        /* Single match - jump directly */
        char filepath[512];
        int lineno;
        if (parse_grep_result(results[0], filepath, sizeof(filepath), &lineno)) {
            goto_file_line(filepath, lineno);
            message("%s:%d", filepath, lineno);
            return true;
        }
        return false;
    }

    /* Multiple matches - show in a buffer and let user pick */
    Buffer *rbuf = buffer_find("*definitions*");
    if (!rbuf) {
        rbuf = buffer_create("*definitions*");
    } else {
        rbuf->read_only = false;
        rbuf->point = 0;
        while (buffer_length(rbuf) > 0)
            buffer_delete_forward(rbuf);
    }

    char header[256];
    snprintf(header, sizeof(header), "Definitions of '%s':\n\n", symbol);
    buffer_insert_string(rbuf, header, strlen(header));

    for (int i = 0; i < count; i++) {
        char line[1100];
        snprintf(line, sizeof(line), "%s\n", results[i]);
        buffer_insert_string(rbuf, line, strlen(line));
    }

    rbuf->point = 0;
    rbuf->modified = false;
    rbuf->read_only = true;

    /* If only showing candidates, jump to the first one */
    char filepath[512];
    int lineno;
    if (parse_grep_result(results[0], filepath, sizeof(filepath), &lineno)) {
        goto_file_line(filepath, lineno);
        message("%d definitions found for '%s' (showing first)", count, symbol);
        return true;
    }

    current_window->buffer = rbuf;
    current_buffer = rbuf;
    return true;
}

static void find_definition_c(const char *symbol) {
    char cmd[2048];

    /* Strategy: grep for C definition patterns in src/ */
    /* 1. Function definition: symbol( with return type on same or prev line */
    /* 2. #define symbol */
    /* 3. typedef ... symbol; */
    /* 4. struct/enum definition */
    /* Exclude lines that are just calls (indented) by requiring line starts non-whitespace */
    snprintf(cmd, sizeof(cmd),
        "grep -rn --include='*.c' --include='*.h' "
        "-e '^[a-zA-Z_*][^=]*[[:space:]*]%s[[:space:]]*(' "
        "-e '^#define[[:space:]][[:space:]]*%s\\b' "
        "-e '^typedef.*[[:space:]]%s;' "
        "-e '^}[[:space:]]*%s;' "
        "src/ 2>/dev/null | head -20",
        symbol, symbol, symbol, symbol);

    if (!grep_and_jump(cmd, symbol)) {
        /* Fallback: extern/global declarations, or any non-indented mention */
        snprintf(cmd, sizeof(cmd),
            "grep -rn --include='*.c' --include='*.h' "
            "-e '^extern.*%s' "
            "-e '^[a-zA-Z_].*%s[[:space:]]*=' "
            "-e '^[a-zA-Z_].*%s;' "
            "src/ 2>/dev/null | head -20",
            symbol, symbol, symbol);
        if (!grep_and_jump(cmd, symbol))
            message("No definition found for '%s'", symbol);
    }
}

static void find_definition_clojure(const char *symbol) {
    /* First try nREPL resolve for loaded namespaces */
    if (sci_is_ready()) {
        char code[512];
        /* Strip namespace prefix for resolve if present */
        const char *bare = strrchr(symbol, '/');
        bare = bare ? bare + 1 : symbol;

        snprintf(code, sizeof(code),
            "(let [sym '%s]"
            "  (when-let [v (resolve sym)]"
            "    (let [m (meta v)]"
            "      (when (:file m)"
            "        (str (:file m) \":\" (:line m))))))",
            symbol);
        char *result = sci_eval(code);
        if (result && result[0] && strncmp(result, "nil", 3) != 0 &&
            strncmp(result, "ERROR", 5) != 0) {
            /* Parse file:line from result */
            /* Result is like "clj/commands.clj:42" */
            char filepath[512];
            int lineno;
            if (parse_grep_result(result, filepath, sizeof(filepath), &lineno)) {
                goto_file_line(filepath, lineno);
                message("%s:%d", filepath, lineno);
                free(result);
                return;
            }
        }
        free(result);
    }

    /* Fallback: grep clj/ for (def... symbol using fixed string match */
    char cmd[1024];
    /* Use grep -rn with multiple patterns to match various def forms */
    snprintf(cmd, sizeof(cmd),
        "grep -rn --include='*.clj' --include='*.bb' "
        "-e '(defn %s ' -e '(defn %s)' "
        "-e '(def %s ' -e '(def %s)' "
        "-e '(defonce %s ' -e '(defonce %s)' "
        "-e '(defcommand \"%s\"' "
        "-e '(defmacro %s ' "
        "clj/ 2>/dev/null | head -20",
        symbol, symbol, symbol, symbol,
        symbol, symbol, symbol, symbol);

    if (!grep_and_jump(cmd, symbol))
        message("No definition found for '%s'", symbol);
}

static void find_definition_any(const char *symbol) {
    /* Try both C and Clojure directories */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "grep -rn --include='*.c' --include='*.h' "
        "'[^a-zA-Z_]%s[[:space:]]*(' src/ 2>/dev/null; "
        "grep -rn --include='*.clj' --include='*.bb' "
        "'(def[a-z]*[[:space:]][[:space:]]*%s\\b' clj/ 2>/dev/null"
        " | head -20",
        symbol, symbol);
    if (!grep_and_jump(cmd, symbol))
        message("No definition found for '%s'", symbol);
}

static void cmd_find_definition(void) {
    char *symbol = word_at_point();
    if (!symbol) { message("No symbol at point"); return; }

    MajorModeID mode = (MajorModeID)current_buffer->major_mode;
    if (mode == MODE_C)
        find_definition_c(symbol);
    else if (mode == MODE_CLOJURE)
        find_definition_clojure(symbol);
    else
        find_definition_any(symbol);

    free(symbol);
}

/* ---- Recursive grep ---- */

static char last_grep_pattern[256] = "";

static void grep_populate(const char *pattern) {
    /* Build grep command */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "grep -rn --color=never -- '%s' . 2>/dev/null | head -500",
             pattern);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        message("Failed to run grep");
        return;
    }

    /* Create or reuse *grep* buffer */
    Buffer *buf = buffer_find("*grep*");
    if (!buf) {
        buf = buffer_create("*grep*");
    } else {
        buf->read_only = false;
        buf->point = 0;
        while (buffer_length(buf) > 0)
            buffer_delete_forward(buf);
    }

    buffer_set_mode(buf, MODE_GREP);

    /* Header */
    char header[512];
    snprintf(header, sizeof(header), "grep -rn '%s'\n\n", pattern);
    buffer_insert_string(buf, header, strlen(header));

    /* Read results */
    int count = 0;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0]) {
            char out[1100];
            int n = snprintf(out, sizeof(out), "%s\n", line);
            buffer_insert_string(buf, out, (size_t)n);
            count++;
        }
    }
    pclose(fp);

    if (count == 0) {
        const char *msg = "(no matches)\n";
        buffer_insert_string(buf, msg, strlen(msg));
    }

    buf->point = 0;
    buf->modified = false;
    buf->read_only = true;

    /* Switch to grep buffer */
    current_window->buffer = buf;
    current_buffer = buf;

    message("%d match%s for \"%s\"", count, count == 1 ? "" : "es", pattern);
}

static void cmd_rgrep(void) {
    char *pattern = minibuffer_read("Grep: ", NULL);
    if (!pattern || !pattern[0]) return;

    snprintf(last_grep_pattern, sizeof(last_grep_pattern), "%s", pattern);
    grep_populate(pattern);
}

static void cmd_grep_visit(void) {
    /* Parse the line at point as filepath:lineno:text and jump there */
    int line_num, col;
    buffer_point_to_line_col(current_buffer, current_buffer->point, &line_num, &col);
    char *line_text = buffer_line_text(current_buffer, line_num);
    if (!line_text) return;

    char filepath[512];
    int lineno;
    if (parse_grep_result(line_text, filepath, sizeof(filepath), &lineno)) {
        /* Strip leading ./ if present */
        const char *path = filepath;
        if (path[0] == '.' && path[1] == '/') path += 2;
        goto_file_line(path, lineno);
    } else {
        message("No grep match on this line");
    }
    free(line_text);
}

static void cmd_grep_quit(void) {
    /* Switch to previous buffer */
    Buffer *other = buffer_list;
    while (other && (other == current_buffer || strcmp(other->name, "*grep*") == 0))
        other = other->next;
    if (other) {
        current_window->buffer = other;
        current_buffer = other;
    }
}

static void cmd_grep_refresh(void) {
    if (last_grep_pattern[0]) {
        current_buffer->read_only = false;
        grep_populate(last_grep_pattern);
    } else {
        message("No previous grep pattern");
    }
}

void commands_init(void) {
    kill_ring_init(&kill_ring);

    command_register("forward-char", cmd_forward_char, "Move forward one character");
    command_register("backward-char", cmd_backward_char, "Move backward one character");
    command_register("next-line", cmd_next_line, "Move to next line");
    command_register("previous-line", cmd_previous_line, "Move to previous line");
    command_register("beginning-of-line", cmd_beginning_of_line, "Move to beginning of line");
    command_register("end-of-line", cmd_end_of_line, "Move to end of line");
    command_register("beginning-of-buffer", cmd_beginning_of_buffer, "Move to beginning of buffer");
    command_register("end-of-buffer", cmd_end_of_buffer, "Move to end of buffer");
    command_register("forward-word", cmd_forward_word, "Move forward one word");
    command_register("backward-word", cmd_backward_word, "Move backward one word");
    command_register("scroll-down", cmd_scroll_down, "Scroll down");
    command_register("scroll-up", cmd_scroll_up, "Scroll up");

    command_register("delete-backward-char", cmd_delete_backward_char, "Delete character before point");
    command_register("delete-char", cmd_delete_char, "Delete character at point");
    command_register("newline", cmd_newline, "Insert newline");
    command_register("self-insert-tab", cmd_self_insert_tab, "Insert tab as spaces");

    command_register("kill-line", cmd_kill_line, "Kill to end of line");
    command_register("set-mark", cmd_set_mark, "Set mark at point");
    command_register("kill-region", cmd_kill_region, "Kill region");
    command_register("kill-ring-save", cmd_kill_ring_save, "Save region to kill ring");
    command_register("yank", cmd_yank, "Yank from kill ring");

    command_register("undo", cmd_undo, "Undo last change");
    command_register("keyboard-quit", cmd_keyboard_quit, "Cancel current operation");

    command_register("save-buffer", cmd_save_buffer, "Save current buffer");
    command_register("find-file", cmd_find_file, "Open a file");
    command_register("switch-to-buffer", cmd_switch_to_buffer, "Switch to named buffer");
    command_register("kill-buffer", cmd_kill_buffer, "Kill a buffer");
    command_register("save-buffers-kill-emacs", cmd_save_buffers_kill_emacs, "Save and quit");

    command_register("other-window", cmd_other_window, "Switch to other window");
    command_register("split-window-below", cmd_split_window_below, "Split window horizontally");
    command_register("split-window-right", cmd_split_window_right, "Split window vertically");
    command_register("delete-window", cmd_delete_window, "Delete current window");
    command_register("delete-other-windows", cmd_delete_other_windows, "Delete other windows");

    command_register("isearch-forward", cmd_isearch_forward, "Incremental search forward");
    command_register("isearch-backward", cmd_isearch_backward, "Incremental search backward");

    command_register("execute-extended-command", cmd_execute_extended_command, "Run command by name");

    command_register("eval-last-sexp", cmd_eval_last_sexp, "Evaluate sexp before point");

    command_register("find-definition", cmd_find_definition, "Jump to definition of symbol at point");
    command_register("pop-mark", cmd_pop_mark, "Jump back to previous location");

    command_register("rgrep", cmd_rgrep, "Recursively grep for a pattern in the current directory.");
    command_register("grep-visit", cmd_grep_visit, "Jump to the file and line of the grep match at point.");
    command_register("grep-quit", cmd_grep_quit, "Close the grep results buffer.");
    command_register("grep-refresh", cmd_grep_refresh, "Re-run the last grep search.");
}
