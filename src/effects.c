#include "effects.h"
#include "util.h"
#ifndef FONT_LOCK_TEST_BUILD
#include "buffer.h"
#include "window.h"
#include "command.h"
#include "sci.h"
#include "mode.h"
#include "shell.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---- EDN Mini-Parser ---- */
/* Restricted subset: vectors, maps, keywords, strings, characters,
 * integers, booleans, nil */

static void vec_push(EdnVal *vec, EdnVal *item) {
    if (vec->vec.count >= vec->vec.capacity) {
        vec->vec.capacity = vec->vec.capacity ? vec->vec.capacity * 2 : 8;
        vec->vec.items = hrealloc(vec->vec.items, sizeof(EdnVal *) * (size_t)vec->vec.capacity);
    }
    vec->vec.items[vec->vec.count++] = item;
}

void edn_free(EdnVal *v) {
    if (!v) return;
    switch (v->type) {
    case EDN_KEYWORD:
    case EDN_STRING:
        free(v->str);
        break;
    case EDN_VECTOR:
        for (int i = 0; i < v->vec.count; i++)
            edn_free(v->vec.items[i]);
        free(v->vec.items);
        break;
    case EDN_MAP:
        for (int i = 0; i < v->map.count; i++) {
            edn_free(v->map.keys[i]);
            edn_free(v->map.vals[i]);
        }
        free(v->map.keys);
        free(v->map.vals);
        break;
    default:
        break;
    }
    free(v);
}

static void skip_whitespace(const char *s, size_t len, size_t *pos) {
    for (;;) {
        while (*pos < len && (s[*pos] == ' ' || s[*pos] == '\t' ||
                              s[*pos] == '\n' || s[*pos] == '\r' ||
                              s[*pos] == ','))
            (*pos)++;
        /* EDN line comment: ;; ... \n (a single ; is also a comment). */
        if (*pos < len && s[*pos] == ';') {
            while (*pos < len && s[*pos] != '\n') (*pos)++;
            continue;
        }
        break;
    }
}

EdnVal *edn_parse(const char *s, size_t len, size_t *consumed) {
    size_t pos = 0;
    skip_whitespace(s, len, &pos);

    if (pos >= len) { *consumed = pos; return NULL; }

    char ch = s[pos];

    /* Vector */
    if (ch == '[') {
        pos++;
        EdnVal *vec = hmalloc(sizeof(EdnVal));
        vec->type = EDN_VECTOR;
        vec->vec.items = NULL;
        vec->vec.count = 0;
        vec->vec.capacity = 0;

        while (pos < len) {
            skip_whitespace(s, len, &pos);
            if (pos < len && s[pos] == ']') {
                pos++;
                *consumed = pos;
                return vec;
            }
            size_t child_consumed = 0;
            EdnVal *child = edn_parse(s + pos, len - pos, &child_consumed);
            if (!child) { edn_free(vec); *consumed = pos; return NULL; }
            vec_push(vec, child);
            pos += child_consumed;
        }
        edn_free(vec);
        *consumed = pos;
        return NULL; /* unterminated vector */
    }

    /* Map */
    if (ch == '{') {
        pos++;
        EdnVal *m = hmalloc(sizeof(EdnVal));
        m->type = EDN_MAP;
        m->map.keys = NULL;
        m->map.vals = NULL;
        m->map.count = 0;
        m->map.capacity = 0;

        while (pos < len) {
            skip_whitespace(s, len, &pos);
            if (pos < len && s[pos] == '}') {
                pos++;
                *consumed = pos;
                return m;
            }
            size_t kc = 0, vc = 0;
            EdnVal *k = edn_parse(s + pos, len - pos, &kc);
            if (!k) { edn_free(m); *consumed = pos; return NULL; }
            pos += kc;
            skip_whitespace(s, len, &pos);
            if (pos >= len) { edn_free(k); edn_free(m); *consumed = pos; return NULL; }
            EdnVal *v = edn_parse(s + pos, len - pos, &vc);
            if (!v) { edn_free(k); edn_free(m); *consumed = pos; return NULL; }
            pos += vc;

            if (m->map.count >= m->map.capacity) {
                m->map.capacity = m->map.capacity ? m->map.capacity * 2 : 8;
                m->map.keys = hrealloc(m->map.keys, sizeof(EdnVal *) * (size_t)m->map.capacity);
                m->map.vals = hrealloc(m->map.vals, sizeof(EdnVal *) * (size_t)m->map.capacity);
            }
            m->map.keys[m->map.count] = k;
            m->map.vals[m->map.count] = v;
            m->map.count++;
        }
        edn_free(m);
        *consumed = pos;
        return NULL;
    }

    /* Keyword */
    if (ch == ':') {
        pos++;
        size_t start = pos;
        while (pos < len && (isalnum(s[pos]) || s[pos] == '-' || s[pos] == '_'
                             || s[pos] == '.' || s[pos] == '/' || s[pos] == '?'
                             || s[pos] == '!'))
            pos++;
        EdnVal *kw = hmalloc(sizeof(EdnVal));
        kw->type = EDN_KEYWORD;
        kw->str = hstrndup(s + start, pos - start);
        *consumed = pos;
        return kw;
    }

    /* String */
    if (ch == '"') {
        pos++;
        /* Build string, handling escapes */
        size_t cap = 64;
        char *buf = hmalloc(cap);
        size_t slen = 0;

        while (pos < len && s[pos] != '"') {
            char c = s[pos];
            if (c == '\\' && pos + 1 < len) {
                pos++;
                switch (s[pos]) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                default: c = s[pos]; break;
                }
            }
            if (slen + 1 >= cap) {
                cap *= 2;
                buf = hrealloc(buf, cap);
            }
            buf[slen++] = c;
            pos++;
        }
        if (pos < len) pos++; /* skip closing quote */
        buf[slen] = '\0';

        EdnVal *sv = hmalloc(sizeof(EdnVal));
        sv->type = EDN_STRING;
        sv->str = buf;
        *consumed = pos;
        return sv;
    }

    /* Character literal: \c, \newline, \space, \tab, \return, \formfeed, \backspace */
    if (ch == '\\') {
        pos++;
        struct { const char *name; int ch; } named[] = {
            {"newline", '\n'}, {"tab", '\t'}, {"space", ' '},
            {"return", '\r'}, {"formfeed", '\f'}, {"backspace", '\b'},
        };
        EdnVal *cv = hmalloc(sizeof(EdnVal));
        cv->type = EDN_CHAR;
        bool matched = false;
        for (size_t i = 0; i < sizeof(named)/sizeof(named[0]); i++) {
            size_t nlen = strlen(named[i].name);
            if (len - pos >= nlen && strncmp(s + pos, named[i].name, nlen) == 0 &&
                (pos + nlen >= len || !isalnum(s[pos + nlen]))) {
                cv->ch = named[i].ch;
                pos += nlen;
                matched = true;
                break;
            }
        }
        if (!matched) {
            if (pos >= len) { free(cv); *consumed = pos; return NULL; }
            cv->ch = (unsigned char)s[pos];
            pos++;
        }
        *consumed = pos;
        return cv;
    }

    /* Integer (possibly negative) */
    if (isdigit(ch) || (ch == '-' && pos + 1 < len && isdigit(s[pos + 1]))) {
        size_t start = pos;
        if (ch == '-') pos++;
        while (pos < len && isdigit(s[pos])) pos++;
        char *numstr = hstrndup(s + start, pos - start);
        EdnVal *iv = hmalloc(sizeof(EdnVal));
        iv->type = EDN_INT;
        iv->num = atoll(numstr);
        free(numstr);
        *consumed = pos;
        return iv;
    }

    /* true / false / nil */
    if (len - pos >= 4 && strncmp(s + pos, "true", 4) == 0 &&
        (pos + 4 >= len || !isalnum(s[pos + 4]))) {
        EdnVal *bv = hmalloc(sizeof(EdnVal));
        bv->type = EDN_BOOL;
        bv->bval = true;
        *consumed = pos + 4;
        return bv;
    }
    if (len - pos >= 5 && strncmp(s + pos, "false", 5) == 0 &&
        (pos + 5 >= len || !isalnum(s[pos + 5]))) {
        EdnVal *bv = hmalloc(sizeof(EdnVal));
        bv->type = EDN_BOOL;
        bv->bval = false;
        *consumed = pos + 5;
        return bv;
    }
    if (len - pos >= 3 && strncmp(s + pos, "nil", 3) == 0 &&
        (pos + 3 >= len || !isalnum(s[pos + 3]))) {
        EdnVal *nv = hmalloc(sizeof(EdnVal));
        nv->type = EDN_NIL;
        *consumed = pos + 3;
        return nv;
    }

    /* Unknown token */
    *consumed = pos;
    return NULL;
}

/* ---- Helper: extract values from effect vector ---- */

static const char *edn_keyword_val(EdnVal *v) {
    return (v && v->type == EDN_KEYWORD) ? v->str : NULL;
}

static const char *edn_string_val(EdnVal *v) {
    return (v && v->type == EDN_STRING) ? v->str : NULL;
}

static long long edn_int_val(EdnVal *v, long long def) {
    return (v && v->type == EDN_INT) ? v->num : def;
}

static bool edn_bool_val(EdnVal *v, bool def) {
    return (v && v->type == EDN_BOOL) ? v->bval : def;
}

EdnVal *edn_map_get(EdnVal *m, const char *kw) {
    if (!m || m->type != EDN_MAP || !kw) return NULL;
    for (int i = 0; i < m->map.count; i++) {
        EdnVal *k = m->map.keys[i];
        if (k && k->type == EDN_KEYWORD && k->str && strcmp(k->str, kw) == 0)
            return m->map.vals[i];
    }
    return NULL;
}

#ifndef FONT_LOCK_TEST_BUILD
/* ---- Effect Executor ---- */

/* Forward declarations */
static void edn_escape_string(char *out, size_t outsize, const char *s);

/* The kill ring used by commands; defined in command.c */
extern KillRing kill_ring;

/* Yank state — tracks the text last inserted by :yank so that :yank-pop can
 * replace it with an older kill-ring entry (M-y). Any buffer mutation other
 * than :yank or :yank-pop invalidates the state via yank_state_invalidate(),
 * which mirrors Emacs' `last-command == yank` check. */
static Buffer *yank_state_buf = NULL;
static size_t yank_state_start = 0;
static size_t yank_state_end = 0;
static int yank_state_offset = 0;  /* offset from the newest kill-ring entry */

void yank_state_invalidate(void) {
    yank_state_buf = NULL;
}

static int execute_one_effect(EdnVal *effect) {
    if (!effect || effect->type != EDN_VECTOR || effect->vec.count < 1)
        return -1;

    const char *op = edn_keyword_val(effect->vec.items[0]);
    if (!op) return -1;

    Buffer *buf = current_buffer;
    Window *win = current_window;

    /* Point/cursor movement */
    if (strcmp(op, "point-set") == 0) {
        long long pos = edn_int_val(effect->vec.items[1], 0);
        size_t len = buffer_length(buf);
        buf->point = (size_t)(pos < 0 ? 0 : ((size_t)pos > len ? len : (size_t)pos));
    }
    else if (strcmp(op, "point-forward") == 0) {
        long long n = edn_int_val(effect->vec.items[1], 1);
        size_t len = buffer_length(buf);
        for (long long i = 0; i < n && buf->point < len; i++)
            buf->point++;
    }
    else if (strcmp(op, "point-backward") == 0) {
        long long n = edn_int_val(effect->vec.items[1], 1);
        for (long long i = 0; i < n && buf->point > 0; i++)
            buf->point--;
    }
    else if (strcmp(op, "point-to-line-start") == 0) {
        buf->point = buffer_line_start(buf, buf->point);
    }
    else if (strcmp(op, "point-to-line-end") == 0) {
        buf->point = buffer_line_end(buf, buf->point);
    }
    else if (strcmp(op, "point-to-buffer-start") == 0) {
        buf->point = 0;
    }
    else if (strcmp(op, "point-to-buffer-end") == 0) {
        buf->point = buffer_length(buf);
    }
    else if (strcmp(op, "point-to-line") == 0) {
        long long n = edn_int_val(effect->vec.items[1], 1);
        if (n < 1) n = 1;
        buf->point = 0;
        for (long long i = 1; i < n; i++) {
            size_t next = buffer_next_line_start(buf, buf->point);
            if (next == buf->point) break;
            buf->point = next;
        }
        win->target_col = -1;
    }
    else if (strcmp(op, "point-forward-word") == 0) {
        buf->point = buffer_forward_word(buf, buf->point);
    }
    else if (strcmp(op, "point-backward-word") == 0) {
        buf->point = buffer_backward_word(buf, buf->point);
    }
    else if (strcmp(op, "point-forward-paragraph") == 0) {
        buf->point = buffer_forward_paragraph(buf, buf->point);
    }
    else if (strcmp(op, "point-backward-paragraph") == 0) {
        buf->point = buffer_backward_paragraph(buf, buf->point);
    }
    else if (strcmp(op, "point-next-line") == 0) {
        /* Move to next line preserving target_col */
        int line, col;
        buffer_point_to_line_col(buf, buf->point, &line, &col);
        if (win->target_col >= 0) col = win->target_col;
        else win->target_col = col;
        size_t next = buffer_next_line_start(buf, buf->point);
        if (next != buf->point || buffer_char_at(buf, buf->point) == '\n') {
            size_t end = buffer_line_end(buf, next);
            size_t target = next + (size_t)col;
            if (target > end) target = end;
            buf->point = target;
        }
    }
    else if (strcmp(op, "point-prev-line") == 0) {
        int line, col;
        buffer_point_to_line_col(buf, buf->point, &line, &col);
        if (win->target_col >= 0) col = win->target_col;
        else win->target_col = col;
        if (line > 0) {
            size_t prev = buffer_prev_line_start(buf, buf->point);
            size_t end = buffer_line_end(buf, prev);
            size_t target = prev + (size_t)col;
            if (target > end) target = end;
            buf->point = target;
        }
    }

    /* Text mutation */
    else if (strcmp(op, "insert") == 0) {
        if (buf->read_only) { message("Buffer is read-only"); return 0; }
        const char *text = edn_string_val(effect->vec.items[1]);
        if (text) {
            buffer_insert_string(buf, text, strlen(text));
            yank_state_invalidate();
        }
    }
    else if (strcmp(op, "delete-forward") == 0) {
        if (buf->read_only) { message("Buffer is read-only"); return 0; }
        long long n = edn_int_val(effect->vec.items[1], 1);
        for (long long i = 0; i < n; i++)
            buffer_delete_forward(buf);
        yank_state_invalidate();
    }
    else if (strcmp(op, "delete-backward") == 0) {
        if (buf->read_only) { message("Buffer is read-only"); return 0; }
        long long n = edn_int_val(effect->vec.items[1], 1);
        for (long long i = 0; i < n; i++)
            buffer_delete_backward(buf);
        yank_state_invalidate();
    }

    /* Mark / region / kill */
    else if (strcmp(op, "set-mark") == 0) {
        buf->mark = buf->point;
        buf->mark_active = true;
    }
    else if (strcmp(op, "deactivate-mark") == 0) {
        buf->mark_active = false;
    }
    else if (strcmp(op, "kill-region") == 0) {
        if (buf->read_only) { message("Buffer is read-only"); return 0; }
        if (!buf->mark_active) { message("No mark set"); return 0; }
        size_t start = buf->mark < buf->point ? buf->mark : buf->point;
        size_t end = buf->mark > buf->point ? buf->mark : buf->point;
        char *text = buffer_region(buf, start, end);
        kill_ring_push(&kill_ring, text);
        clipboard_copy(text);
        buffer_delete_region(buf, start, end - start);
        buf->mark_active = false;
        free(text);
        yank_state_invalidate();
    }
    else if (strcmp(op, "copy-region") == 0) {
        if (!buf->mark_active) { message("No mark set"); return 0; }
        size_t start = buf->mark < buf->point ? buf->mark : buf->point;
        size_t end = buf->mark > buf->point ? buf->mark : buf->point;
        char *text = buffer_region(buf, start, end);
        kill_ring_push(&kill_ring, text);
        clipboard_copy(text);
        buf->mark_active = false;
        free(text);
        yank_state_invalidate();
    }
    else if (strcmp(op, "yank") == 0) {
        if (buf->read_only) { message("Buffer is read-only"); return 0; }
        char *clip = clipboard_paste();
        const char *kr_top = kill_ring_top(&kill_ring);
        const char *text;
        if (clip && (!kr_top || strcmp(clip, kr_top) != 0)) {
            kill_ring_push(&kill_ring, clip);
            text = kill_ring_top(&kill_ring);
        } else {
            text = kr_top;
        }
        if (text) {
            size_t start = buf->point;
            size_t tlen = strlen(text);
            buffer_insert_string(buf, text, tlen);
            yank_state_buf = buf;
            yank_state_start = start;
            yank_state_end = start + tlen;
            yank_state_offset = 0;
        } else {
            message("Kill ring is empty");
        }
        free(clip);
    }
    else if (strcmp(op, "yank-pop") == 0) {
        if (buf->read_only) { message("Buffer is read-only"); return 0; }
        if (yank_state_buf != buf || buf->point != yank_state_end) {
            message("Previous command was not a yank");
            return 0;
        }
        if (kill_ring.count == 0) {
            message("Kill ring is empty");
            return 0;
        }
        /* Replace the last-yanked text with the next-older kill-ring entry. */
        size_t len = yank_state_end - yank_state_start;
        buf->point = yank_state_start;
        buffer_delete_region(buf, yank_state_start, len);
        yank_state_offset = (yank_state_offset + 1) % kill_ring.count;
        const char *text = kill_ring_nth(&kill_ring, yank_state_offset);
        if (text) {
            size_t tlen = strlen(text);
            buffer_insert_string(buf, text, tlen);
            yank_state_end = yank_state_start + tlen;
        } else {
            yank_state_end = yank_state_start;
        }
    }
    else if (strcmp(op, "kill-line") == 0) {
        if (buf->read_only) { message("Buffer is read-only"); return 0; }
        size_t end = buffer_line_end(buf, buf->point);
        if (end == buf->point && end < buffer_length(buf)) end++;
        if (end > buf->point) {
            char *text = buffer_region(buf, buf->point, end);
            kill_ring_push(&kill_ring, text);
            clipboard_copy(text);
            buffer_delete_region(buf, buf->point, end - buf->point);
            free(text);
            yank_state_invalidate();
        }
    }
    else if (strcmp(op, "clipboard-copy") == 0) {
        const char *text = edn_string_val(effect->vec.items[1]);
        if (text) clipboard_copy(text);
    }

    /* Undo */
    else if (strcmp(op, "undo") == 0) {
        if (buf->read_only) { message("Buffer is read-only"); return 0; }
        buf->undo_inhibit = true;
        UndoEntry *e = undo_pop(&buf->undo);
        if (!e) { buf->undo_inhibit = false; message("No further undo information"); return 0; }
        while (e && e->type == UNDO_BOUNDARY) {
            undo_entry_free(e);
            e = undo_pop(&buf->undo);
        }
        if (!e) { buf->undo_inhibit = false; return 0; }
        if (e->type == UNDO_INSERT) {
            buf->point = e->pos;
            for (size_t i = 0; i < e->len; i++)
                buffer_delete_forward(buf);
        } else if (e->type == UNDO_DELETE) {
            buf->point = e->pos;
            buffer_insert_string(buf, e->text, e->len);
            buf->point = e->pos + e->len;
        }
        undo_entry_free(e);
        buf->undo_inhibit = false;
        yank_state_invalidate();
    }

    /* Buffer management */
    else if (strcmp(op, "buffer-create") == 0) {
        const char *name = edn_string_val(effect->vec.items[1]);
        if (name) buffer_create(name);
    }
    else if (strcmp(op, "buffer-append-text") == 0) {
        const char *name = edn_string_val(effect->vec.items[1]);
        const char *text = edn_string_val(effect->vec.items[2]);
        if (name && text) {
            Buffer *target = buffer_find(name);
            if (target) {
                if (target == messages_buffer ||
                    strcmp(name, "*Messages*") == 0) {
                    buffer_append_line_capped(target, text, MESSAGES_CAP);
                } else {
                    size_t saved = target->point;
                    bool inhibit = target->undo_inhibit;
                    target->undo_inhibit = true;
                    target->point = buffer_length(target);
                    buffer_insert_string(target, text, strlen(text));
                    target->undo_inhibit = inhibit;
                    size_t new_len = buffer_length(target);
                    target->point = saved <= new_len ? saved : new_len;
                }
                yank_state_invalidate();
            }
        }
    }
    else if (strcmp(op, "buffer-switch") == 0) {
        const char *name = edn_string_val(effect->vec.items[1]);
        if (name) {
            Buffer *b = buffer_find(name);
            if (b) {
                current_buffer = b;
                win->buffer = b;
                yank_state_invalidate();
            } else {
                message("No buffer named %s", name);
            }
        }
    }
    else if (strcmp(op, "buffer-load-file") == 0) {
        const char *path = edn_string_val(effect->vec.items[1]);
        if (path) {
            if (!buffer_load_file(buf, path)) {
                buf->filename = hstrdup(path);
                message("(New file)");
            }
            buffer_set_mode(buf, mode_detect(path));
            yank_state_invalidate();
        }
    }
    else if (strcmp(op, "buffer-save") == 0) {
        if (!buf->filename) { message("No file name"); }
        else if (buffer_save_file(buf)) { message("Wrote %s", buf->filename); }
        else { message("Error writing %s", buf->filename); }
    }
    else if (strcmp(op, "buffer-destroy") == 0) {
        const char *name = edn_string_val(effect->vec.items[1]);
        if (name) {
            Buffer *b = buffer_find(name);
            if (b && b != current_buffer) buffer_destroy(b);
        }
    }
    else if (strcmp(op, "buffer-set-contents") == 0) {
        const char *text = edn_string_val(effect->vec.items[1]);
        if (text) {
            /* Clear buffer and insert new contents */
            buffer_delete_region(buf, 0, buffer_length(buf));
            buffer_insert_string(buf, text, strlen(text));
            yank_state_invalidate();
        }
    }
    else if (strcmp(op, "buffer-set-read-only") == 0) {
        buf->read_only = edn_bool_val(effect->vec.items[1], false);
    }
    else if (strcmp(op, "buffer-set-modified") == 0) {
        buf->modified = edn_bool_val(effect->vec.items[1], false);
    }
    else if (strcmp(op, "buffer-set-mode") == 0) {
        const char *mname = edn_string_val(effect->vec.items[1]);
        if (mname) {
            MajorMode *m = mode_find(mname);
            if (m) buffer_set_mode(buf, m->name);
        }
    }
    else if (strcmp(op, "buffer-set-filename") == 0) {
        const char *path = edn_string_val(effect->vec.items[1]);
        if (path) {
            free(buf->filename);
            buf->filename = hstrdup(path);
        }
    }

    /* Window management */
    else if (strcmp(op, "window-split-below") == 0) {
        window_split_below(win);
    }
    else if (strcmp(op, "window-split-right") == 0) {
        window_split_right(win);
    }
    else if (strcmp(op, "window-delete") == 0) {
        if (window_count() <= 1) { message("Cannot delete sole window"); }
        else { window_delete(win); current_buffer = current_window->buffer; }
    }
    else if (strcmp(op, "window-delete-others") == 0) {
        window_delete_others(win);
    }
    else if (strcmp(op, "window-other") == 0) {
        Window *next = window_next(win);
        if (next) {
            current_window = next;
            current_buffer = next->buffer;
        }
    }
    else if (strcmp(op, "display-buffer") == 0) {
        const char *name = edn_string_val(effect->vec.items[1]);
        if (name) {
            Buffer *target = buffer_find(name);
            if (!target) {
                message("No buffer named %s", name);
            } else {
                /* Reuse an existing window already showing this buffer. */
                Window *existing = NULL;
                for (Window *w = window_list; w; w = w->next) {
                    if (w->buffer == target) { existing = w; break; }
                }
                if (existing) {
                    current_window = existing;
                    current_buffer = target;
                } else {
                    Window *new_win = window_split_below(win);
                    if (new_win) {
                        new_win->buffer = target;
                        current_window = new_win;
                    } else {
                        /* Too small to split: reuse current window. */
                        win->buffer = target;
                    }
                    current_buffer = target;
                }
            }
        }
    }

    /* Display */
    else if (strcmp(op, "message") == 0) {
        const char *text = edn_string_val(effect->vec.items[1]);
        if (text) message("%s", text);
    }
    else if (strcmp(op, "redisplay") == 0) {
        need_redisplay = true;
    }

    /* Scroll */
    else if (strcmp(op, "scroll-down") == 0) {
        long long n = edn_int_val(effect->vec.items[1], win->rows - 2);
        for (long long i = 0; i < n; i++) {
            /* Inline next-line logic */
            int line, col;
            buffer_point_to_line_col(buf, buf->point, &line, &col);
            if (win->target_col >= 0) col = win->target_col;
            else win->target_col = col;
            size_t next = buffer_next_line_start(buf, buf->point);
            if (next != buf->point || buffer_char_at(buf, buf->point) == '\n') {
                size_t end = buffer_line_end(buf, next);
                size_t target = next + (size_t)col;
                if (target > end) target = end;
                buf->point = target;
            }
        }
    }
    else if (strcmp(op, "scroll-up") == 0) {
        long long n = edn_int_val(effect->vec.items[1], win->rows - 2);
        for (long long i = 0; i < n; i++) {
            int line, col;
            buffer_point_to_line_col(buf, buf->point, &line, &col);
            if (win->target_col >= 0) col = win->target_col;
            else win->target_col = col;
            if (line > 0) {
                size_t prev = buffer_prev_line_start(buf, buf->point);
                size_t end = buffer_line_end(buf, prev);
                size_t target = prev + (size_t)col;
                if (target > end) target = end;
                buf->point = target;
            }
        }
    }

    /* Target column */
    else if (strcmp(op, "reset-target-col") == 0) {
        win->target_col = -1;
    }
    else if (strcmp(op, "preserve-target-col") == 0) {
        /* No-op: don't reset target_col */
    }

    /* Lifecycle */
    else if (strcmp(op, "quit") == 0) {
        editor_running = false;
    }

    /* Shell */
    else if (strcmp(op, "shell-start") == 0) {
        shell_start();
    }
    else if (strcmp(op, "shell-command") == 0) {
        const char *cmd = edn_string_val(effect->vec.items[1]);
        if (cmd) shell_command(cmd);
    }

    /* Search (for isearch) */
    else if (strcmp(op, "search-forward") == 0) {
        const char *pattern = edn_string_val(effect->vec.items[1]);
        if (pattern && pattern[0]) {
            size_t len = buffer_length(buf);
            size_t plen = strlen(pattern);
            for (size_t i = buf->point + 1; i + plen <= len; i++) {
                bool match = true;
                for (size_t j = 0; j < plen; j++) {
                    if (buffer_char_at(buf, i + j) != pattern[j]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    buf->point = i + plen;
                    break;
                }
            }
        }
    }
    else if (strcmp(op, "search-backward") == 0) {
        const char *pattern = edn_string_val(effect->vec.items[1]);
        if (pattern && pattern[0]) {
            size_t plen = strlen(pattern);
            if (buf->point >= plen) {
                for (size_t i = buf->point - plen; ; i--) {
                    bool match = true;
                    for (size_t j = 0; j < plen; j++) {
                        if (buffer_char_at(buf, i + j) != pattern[j]) {
                            match = false;
                            break;
                        }
                    }
                    if (match) {
                        buf->point = i;
                        break;
                    }
                    if (i == 0) break;
                }
            }
        }
    }

    /* Prompt -- collect minibuffer input and dispatch Clojure callback */
    /* Dispatch a named command (used by hammock:// links) */
    else if (strcmp(op, "run-command") == 0) {
        const char *name = edn_string_val(effect->vec.items[1]);
        if (name) command_dispatch(name, true);
    }

    else if (strcmp(op, "prompt") == 0) {
        if (effect->vec.count >= 4) {
            const char *prompt_text = edn_string_val(effect->vec.items[1]);
            const char *callback_fn = edn_string_val(effect->vec.items[2]);
            const char *comp_type = edn_keyword_val(effect->vec.items[3]);

            if (prompt_text && callback_fn) {
                /* Select completion function */
                CompletionFn comp = NULL;
                if (comp_type) {
                    if (strcmp(comp_type, "file") == 0) comp = complete_file_name;
                    else if (strcmp(comp_type, "buffer") == 0) comp = complete_buffer_name;
                    else if (strcmp(comp_type, "command") == 0) comp = complete_command_name;
                }

                char *input = minibuffer_read(prompt_text, comp);
                if (input && input[0] != '\0') {
                    /* Escape the input for EDN */
                    char escaped[1024];
                    edn_escape_string(escaped, sizeof(escaped), input);

                    /* Call the Clojure callback with the input */
                    char code[2048];
                    snprintf(code, sizeof(code), "(%s %s)", callback_fn, escaped);
                    char *cb_result = sci_eval(code);
                    if (cb_result) {
                        effects_execute(cb_result);
                        free(cb_result);
                    }
                } else {
                    message("Quit");
                }
            }
        }
    }

    else {
        message("Unknown effect: %s", op);
        return -1;
    }

    return 0;
}

int effects_execute(const char *edn_effects) {
    if (!edn_effects || edn_effects[0] == '\0') return 0;

    /* Skip leading SCI output -- find the first '[' */
    const char *start = strchr(edn_effects, '[');
    if (!start) {
        /* No effect vector -- might just be a value/message, which is fine */
        return 0;
    }

    size_t len = strlen(start);
    size_t consumed = 0;
    EdnVal *root = edn_parse(start, len, &consumed);
    if (!root || root->type != EDN_VECTOR) {
        edn_free(root);
        return -1;
    }

    int result = 0;
    for (int i = 0; i < root->vec.count; i++) {
        EdnVal *effect = root->vec.items[i];
        int r = execute_one_effect(effect);
        if (r < 0) {
            result = -1;
        }
    }

    need_redisplay = true;
    edn_free(root);
    return result;
}

/* ---- State Snapshot ---- */

/* Escape a string for EDN output */
static void edn_escape_string(char *out, size_t outsize, const char *s) {
    size_t o = 0;
    out[o++] = '"';
    for (size_t i = 0; s[i] && o + 4 < outsize; i++) {
        switch (s[i]) {
        case '"':  out[o++] = '\\'; out[o++] = '"'; break;
        case '\\': out[o++] = '\\'; out[o++] = '\\'; break;
        case '\n': out[o++] = '\\'; out[o++] = 'n'; break;
        case '\t': out[o++] = '\\'; out[o++] = 't'; break;
        case '\r': out[o++] = '\\'; out[o++] = 'r'; break;
        default:   out[o++] = s[i]; break;
        }
    }
    out[o++] = '"';
    out[o] = '\0';
}

char *state_snapshot_edn(void) {
    Buffer *buf = current_buffer;
    Window *win = current_window;

    /* Get current line info */
    int line_num = 0, col = 0;
    buffer_point_to_line_col(buf, buf->point, &line_num, &col);
    char *raw_line = buffer_line_text(buf, line_num);
    char line_escaped[1024];
    if (raw_line) {
        edn_escape_string(line_escaped, sizeof(line_escaped), raw_line);
        free(raw_line);
    } else {
        snprintf(line_escaped, sizeof(line_escaped), "\"\"");
    }

    /* Build buffer list EDN */
    size_t buflist_cap = 4096;
    char *buflist = hmalloc(buflist_cap);
    size_t buflist_len = 0;
    buflist[buflist_len++] = '[';
    for (Buffer *b = buffer_list; b; b = b->next) {
        char bname_esc[256];
        edn_escape_string(bname_esc, sizeof(bname_esc), b->name);
        char bfn_esc[512];
        if (b->filename)
            edn_escape_string(bfn_esc, sizeof(bfn_esc), b->filename);
        else
            snprintf(bfn_esc, sizeof(bfn_esc), "nil");
        const char *bmname = b->mode_name ? b->mode_name : "Fundamental";
        char bmode_esc[128];
        edn_escape_string(bmode_esc, sizeof(bmode_esc), bmname);

        char entry[1024];
        int elen = snprintf(entry, sizeof(entry),
            "{:name %s :size %zu :modified %s :read-only %s :mode %s :filename %s}",
            bname_esc,
            buffer_length(b),
            b->modified ? "true" : "false",
            b->read_only ? "true" : "false",
            bmode_esc,
            bfn_esc);

        /* Grow buflist if needed */
        while (buflist_len + (size_t)elen + 2 >= buflist_cap) {
            buflist_cap *= 2;
            buflist = realloc(buflist, buflist_cap);
        }
        if (buflist_len > 1) buflist[buflist_len++] = ' ';
        memcpy(buflist + buflist_len, entry, (size_t)elen);
        buflist_len += (size_t)elen;
    }
    buflist[buflist_len++] = ']';
    buflist[buflist_len] = '\0';

    /* Build EDN map with current editor state metadata */
    size_t cap = 2048 + buflist_len;
    char *edn = hmalloc(cap);

    char name_escaped[256];
    edn_escape_string(name_escaped, sizeof(name_escaped), buf->name);

    char filename_escaped[512];
    if (buf->filename)
        edn_escape_string(filename_escaped, sizeof(filename_escaped), buf->filename);
    else
        snprintf(filename_escaped, sizeof(filename_escaped), "nil");

    const char *mname = buf->mode_name ? buf->mode_name : "Fundamental";
    char mode_escaped[128];
    edn_escape_string(mode_escaped, sizeof(mode_escaped), mname);

    snprintf(edn, cap,
        "{:current-buffer %s"
        " :point %zu"
        " :mark %zu"
        " :mark-active %s"
        " :length %zu"
        " :modified %s"
        " :read-only %s"
        " :mode %s"
        " :filename %s"
        " :window-count %d"
        " :top-line %d"
        " :visible-rows %d"
        " :current-line %s"
        " :line-number %d"
        " :col %d"
        " :buffers %s}",
        name_escaped,
        buf->point,
        buf->mark,
        buf->mark_active ? "true" : "false",
        buffer_length(buf),
        buf->modified ? "true" : "false",
        buf->read_only ? "true" : "false",
        mode_escaped,
        filename_escaped,
        window_count(),
        win->top_line,
        win->rows - 1,
        line_escaped,
        line_num + 1,  /* 1-indexed for Clojure */
        col,
        buflist);

    free(buflist);
    return edn;
}

void state_push_snapshot(void) {
    char *snapshot = state_snapshot_edn();
    size_t code_len = strlen(snapshot) + 64;
    char *code = hmalloc(code_len);
    snprintf(code, code_len, "(reset! hammock.state/*editor* %s)", snapshot);
    char *result = sci_eval(code);
    free(result);
    free(code);
    free(snapshot);
}

#endif /* FONT_LOCK_TEST_BUILD */
