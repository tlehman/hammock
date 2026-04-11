#include "buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

Buffer *buffer_list = NULL;
Buffer *current_buffer = NULL;

static void buffer_move_gap(Buffer *buf, size_t pos) {
    if (pos == buf->gap_start) return;

    if (pos < buf->gap_start) {
        size_t shift = buf->gap_start - pos;
        memmove(buf->data + buf->gap_end - shift,
                buf->data + pos, shift);
        buf->gap_start = pos;
        buf->gap_end -= shift;
    } else {
        size_t shift = pos - buf->gap_start;
        memmove(buf->data + buf->gap_start,
                buf->data + buf->gap_end, shift);
        buf->gap_start += shift;
        buf->gap_end += shift;
    }
}

static void buffer_ensure_gap(Buffer *buf, size_t needed) {
    size_t gap_size = buf->gap_end - buf->gap_start;
    if (gap_size >= needed) return;

    size_t new_gap = needed + INITIAL_GAP_SIZE;
    size_t content_after = buf->capacity - buf->gap_end;
    size_t new_capacity = buf->gap_start + new_gap + content_after;

    buf->data = hrealloc(buf->data, new_capacity);
    memmove(buf->data + buf->gap_start + new_gap,
            buf->data + buf->gap_end, content_after);
    buf->gap_end = buf->gap_start + new_gap;
    buf->capacity = new_capacity;
}

Buffer *buffer_create(const char *name) {
    Buffer *buf = hmalloc(sizeof(Buffer));
    memset(buf, 0, sizeof(Buffer));

    buf->capacity = INITIAL_GAP_SIZE;
    buf->data = hmalloc(buf->capacity);
    buf->gap_start = 0;
    buf->gap_end = buf->capacity;
    buf->name = hstrdup(name);
    undo_init(&buf->undo);

    /* Add to buffer list */
    buf->next = buffer_list;
    buffer_list = buf;

    return buf;
}

void buffer_destroy(Buffer *buf) {
    if (!buf) return;

    /* Remove from buffer list */
    Buffer **pp = &buffer_list;
    while (*pp && *pp != buf) pp = &(*pp)->next;
    if (*pp) *pp = buf->next;

    free(buf->data);
    free(buf->name);
    free(buf->filename);
    undo_free(&buf->undo);
    free(buf);
}

size_t buffer_length(Buffer *buf) {
    return buf->capacity - (buf->gap_end - buf->gap_start);
}

char buffer_char_at(Buffer *buf, size_t pos) {
    if (pos >= buffer_length(buf)) return '\0';
    if (pos < buf->gap_start)
        return buf->data[pos];
    else
        return buf->data[buf->gap_end + (pos - buf->gap_start)];
}

void buffer_set_point(Buffer *buf, size_t pos) {
    size_t len = buffer_length(buf);
    if (pos > len) pos = len;
    buf->point = pos;
}

void buffer_insert_char(Buffer *buf, char ch) {
    buffer_ensure_gap(buf, 1);
    buffer_move_gap(buf, buf->point);
    buf->data[buf->gap_start] = ch;
    buf->gap_start++;
    buf->point++;
    buf->modified = true;
    if (!buf->undo_inhibit)
        undo_record_insert(&buf->undo, buf->point - 1, &ch, 1);
}

void buffer_insert_string(Buffer *buf, const char *s, size_t len) {
    if (len == 0) return;
    buffer_ensure_gap(buf, len);
    buffer_move_gap(buf, buf->point);
    memcpy(buf->data + buf->gap_start, s, len);
    buf->gap_start += len;
    buf->point += len;
    buf->modified = true;
    if (!buf->undo_inhibit)
        undo_record_insert(&buf->undo, buf->point - len, s, len);
}

void buffer_delete_backward(Buffer *buf) {
    if (buf->point == 0) return;
    buffer_move_gap(buf, buf->point);
    char ch = buf->data[buf->gap_start - 1];
    if (!buf->undo_inhibit)
        undo_record_delete(&buf->undo, buf->point - 1, &ch, 1);
    buf->gap_start--;
    buf->point--;
    buf->modified = true;
}

void buffer_delete_forward(Buffer *buf) {
    if (buf->point >= buffer_length(buf)) return;
    buffer_move_gap(buf, buf->point);
    char ch = buf->data[buf->gap_end];
    if (!buf->undo_inhibit)
        undo_record_delete(&buf->undo, buf->point, &ch, 1);
    buf->gap_end++;
    buf->modified = true;
}

/* Forward declaration for use in buffer_delete_region */
char *buffer_region(Buffer *buf, size_t start, size_t end);

void buffer_delete_region(Buffer *buf, size_t start, size_t count) {
    if (count == 0) return;
    size_t len = buffer_length(buf);
    if (start > len) start = len;
    if (start + count > len) count = len - start;

    /* Record undo for the whole region */
    if (!buf->undo_inhibit) {
        char *deleted = buffer_region(buf, start, start + count);
        undo_record_delete(&buf->undo, start, deleted, count);
        free(deleted);
    }

    buf->point = start;
    buffer_move_gap(buf, start);
    buf->gap_end += count;
    buf->modified = true;
}

/* Line operations */

size_t buffer_line_start(Buffer *buf, size_t pos) {
    while (pos > 0 && buffer_char_at(buf, pos - 1) != '\n')
        pos--;
    return pos;
}

size_t buffer_line_end(Buffer *buf, size_t pos) {
    size_t len = buffer_length(buf);
    while (pos < len && buffer_char_at(buf, pos) != '\n')
        pos++;
    return pos;
}

size_t buffer_next_line_start(Buffer *buf, size_t pos) {
    size_t len = buffer_length(buf);
    pos = buffer_line_end(buf, pos);
    if (pos < len) pos++;  /* skip the newline */
    return pos;
}

size_t buffer_prev_line_start(Buffer *buf, size_t pos) {
    if (pos == 0) return 0;
    pos = buffer_line_start(buf, pos);
    if (pos == 0) return 0;
    return buffer_line_start(buf, pos - 1);
}

void buffer_point_to_line_col(Buffer *buf, size_t pos, int *line, int *col) {
    int l = 0, c = 0;
    for (size_t i = 0; i < pos && i < buffer_length(buf); i++) {
        if (buffer_char_at(buf, i) == '\n') {
            l++;
            c = 0;
        } else {
            c++;
        }
    }
    *line = l;
    *col = c;
}

size_t buffer_line_count(Buffer *buf) {
    size_t len = buffer_length(buf);
    size_t lines = 1;
    for (size_t i = 0; i < len; i++) {
        if (buffer_char_at(buf, i) == '\n') lines++;
    }
    return lines;
}

/* Word operations */

size_t buffer_forward_word(Buffer *buf, size_t pos) {
    size_t len = buffer_length(buf);
    /* Skip non-word chars */
    while (pos < len && !isalnum(buffer_char_at(buf, pos)))
        pos++;
    /* Skip word chars */
    while (pos < len && isalnum(buffer_char_at(buf, pos)))
        pos++;
    return pos;
}

size_t buffer_backward_word(Buffer *buf, size_t pos) {
    if (pos == 0) return 0;
    pos--;
    /* Skip non-word chars */
    while (pos > 0 && !isalnum(buffer_char_at(buf, pos)))
        pos--;
    /* Skip word chars */
    while (pos > 0 && isalnum(buffer_char_at(buf, pos - 1)))
        pos--;
    return pos;
}

/* Paragraph operations */

/* A blank line is one that contains only whitespace (or is empty). */
static bool line_is_blank(Buffer *buf, size_t line_start) {
    size_t len = buffer_length(buf);
    size_t pos = line_start;
    while (pos < len) {
        char c = buffer_char_at(buf, pos);
        if (c == '\n') return true;
        if (c != ' ' && c != '\t' && c != '\r') return false;
        pos++;
    }
    return true; /* end of buffer counts as blank */
}

size_t buffer_forward_paragraph(Buffer *buf, size_t pos) {
    size_t len = buffer_length(buf);
    /* Move to start of current line */
    size_t ls = buffer_line_start(buf, pos);
    /* Skip non-blank lines */
    while (ls < len && !line_is_blank(buf, ls))
        ls = buffer_next_line_start(buf, ls);
    /* Skip blank lines */
    while (ls < len && line_is_blank(buf, ls)) {
        size_t next = buffer_next_line_start(buf, ls);
        if (next == ls) break; /* at end */
        ls = next;
    }
    return ls < len ? ls : len;
}

size_t buffer_backward_paragraph(Buffer *buf, size_t pos) {
    if (pos == 0) return 0;
    /* Move to start of current line */
    size_t ls = buffer_line_start(buf, pos);
    /* If at start of a line, step back one line to make progress */
    if (ls == pos && ls > 0)
        ls = buffer_prev_line_start(buf, ls);
    /* Skip blank lines */
    while (ls > 0 && line_is_blank(buf, ls))
        ls = buffer_prev_line_start(buf, ls);
    /* Skip non-blank lines */
    while (ls > 0) {
        size_t prev = buffer_prev_line_start(buf, ls);
        if (line_is_blank(buf, prev)) break;
        ls = prev;
    }
    return ls;
}

/* File I/O */

bool buffer_load_file(Buffer *buf, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) { fclose(f); return false; }

    /* Reset buffer */
    free(buf->data);
    buf->capacity = (size_t)size + INITIAL_GAP_SIZE;
    buf->data = hmalloc(buf->capacity);

    size_t nread = fread(buf->data, 1, (size_t)size, f);
    fclose(f);

    /* Gap is after the content */
    buf->gap_start = nread;
    buf->gap_end = buf->capacity;
    buf->point = 0;
    buf->modified = false;

    free(buf->filename);
    buf->filename = hstrdup(path);

    return true;
}

bool buffer_save_file(Buffer *buf) {
    if (!buf->filename) return false;

    FILE *f = fopen(buf->filename, "w");
    if (!f) return false;

    /* Write content before gap */
    if (buf->gap_start > 0)
        fwrite(buf->data, 1, buf->gap_start, f);
    /* Write content after gap */
    size_t after = buf->capacity - buf->gap_end;
    if (after > 0)
        fwrite(buf->data + buf->gap_end, 1, after, f);

    fclose(f);
    buf->modified = false;
    return true;
}

/* Content extraction */

/* Copy from gap buffer using two memcpy calls (before-gap and after-gap segments) */
static size_t gap_buffer_copy(Buffer *buf, size_t start, size_t count, char *dest) {
    size_t copied = 0;
    /* Map logical positions to physical positions around the gap */
    if (start < buf->gap_start) {
        size_t before_gap = buf->gap_start - start;
        if (before_gap > count) before_gap = count;
        memcpy(dest, buf->data + start, before_gap);
        copied += before_gap;
    }
    if (copied < count) {
        /* Remaining bytes come from after the gap */
        size_t logical_after = start + copied;
        size_t physical = (logical_after < buf->gap_start)
                          ? buf->gap_end
                          : buf->gap_end + (logical_after - buf->gap_start);
        size_t remaining = count - copied;
        memcpy(dest + copied, buf->data + physical, remaining);
        copied += remaining;
    }
    return copied;
}

char *buffer_contents(Buffer *buf) {
    size_t len = buffer_length(buf);
    char *s = hmalloc(len + 1);
    gap_buffer_copy(buf, 0, len, s);
    s[len] = '\0';
    return s;
}

char *buffer_region(Buffer *buf, size_t start, size_t end) {
    if (start > end) { size_t t = start; start = end; end = t; }
    size_t len = end - start;
    char *s = hmalloc(len + 1);
    gap_buffer_copy(buf, start, len, s);
    s[len] = '\0';
    return s;
}

char *buffer_line_text(Buffer *buf, int line) {
    size_t pos = 0;
    for (int i = 0; i < line; i++) {
        pos = buffer_next_line_start(buf, pos);
    }
    size_t start = pos;
    size_t end = buffer_line_end(buf, pos);
    return buffer_region(buf, start, end);
}

/* Buffer management */

Buffer *buffer_find(const char *name) {
    for (Buffer *b = buffer_list; b; b = b->next) {
        if (strcmp(b->name, name) == 0) return b;
    }
    return NULL;
}

Buffer *buffer_find_file(const char *path) {
    for (Buffer *b = buffer_list; b; b = b->next) {
        if (b->filename && strcmp(b->filename, path) == 0) return b;
    }
    return NULL;
}

void buffer_switch(Buffer *buf) {
    current_buffer = buf;
}

void buffer_append_line_capped(Buffer *buf, const char *text, size_t max_lines) {
    if (!buf || !text) return;

    size_t saved_point = buf->point;
    bool saved_inhibit = buf->undo_inhibit;
    buf->undo_inhibit = true;

    buf->point = buffer_length(buf);
    size_t text_len = strlen(text);
    if (text_len > 0) buffer_insert_string(buf, text, text_len);
    if (text_len == 0 || text[text_len - 1] != '\n')
        buffer_insert_char(buf, '\n');

    /* Evict oldest lines until count <= max_lines + 1 (trailing empty line). */
    while (buffer_line_count(buf) > max_lines + 1) {
        size_t first_end = buffer_line_end(buf, 0);
        if (first_end < buffer_length(buf)) first_end++; /* include newline */
        if (first_end == 0) break;
        buffer_delete_region(buf, 0, first_end);
    }

    buf->undo_inhibit = saved_inhibit;
    size_t new_len = buffer_length(buf);
    buf->point = saved_point <= new_len ? saved_point : new_len;
    buf->modified = false;
}
