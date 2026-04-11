#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>
#include <stdbool.h>
#include "util.h"

#define INITIAL_GAP_SIZE 1024

typedef struct Buffer {
    char *data;
    size_t gap_start;
    size_t gap_end;
    size_t capacity;

    size_t point;       /* cursor position in logical coords */
    size_t mark;
    bool mark_active;

    char *name;
    char *filename;
    bool modified;
    bool read_only;

    int major_mode;     /* mode enum, set later */

    UndoList undo;
    bool undo_inhibit;  /* suppress undo recording during undo */

    struct Buffer *next;
} Buffer;

/* Global buffer list */
extern Buffer *buffer_list;
extern Buffer *current_buffer;

Buffer *buffer_create(const char *name);
void buffer_destroy(Buffer *buf);
void buffer_insert_char(Buffer *buf, char ch);
void buffer_insert_string(Buffer *buf, const char *s, size_t len);
void buffer_delete_backward(Buffer *buf);
void buffer_delete_forward(Buffer *buf);
void buffer_delete_region(Buffer *buf, size_t start, size_t count);
char buffer_char_at(Buffer *buf, size_t pos);
size_t buffer_length(Buffer *buf);
void buffer_set_point(Buffer *buf, size_t pos);

/* Line operations */
size_t buffer_line_start(Buffer *buf, size_t pos);
size_t buffer_line_end(Buffer *buf, size_t pos);
size_t buffer_next_line_start(Buffer *buf, size_t pos);
size_t buffer_prev_line_start(Buffer *buf, size_t pos);
void buffer_point_to_line_col(Buffer *buf, size_t pos, int *line, int *col);
size_t buffer_line_count(Buffer *buf);

/* Word operations */
size_t buffer_forward_word(Buffer *buf, size_t pos);
size_t buffer_backward_word(Buffer *buf, size_t pos);

/* Paragraph operations */
size_t buffer_forward_paragraph(Buffer *buf, size_t pos);
size_t buffer_backward_paragraph(Buffer *buf, size_t pos);

/* File I/O */
bool buffer_load_file(Buffer *buf, const char *path);
bool buffer_save_file(Buffer *buf);

/* Content extraction */
char *buffer_contents(Buffer *buf);
char *buffer_region(Buffer *buf, size_t start, size_t end);
char *buffer_line_text(Buffer *buf, int line);

/* Buffer management */
Buffer *buffer_find(const char *name);
Buffer *buffer_find_file(const char *path);
void buffer_switch(Buffer *buf);

/* Append `text` at end of `buf`, ensure trailing newline, then evict
 * oldest lines until line count <= max_lines. Does not record undo.
 * Safe to call with a NULL buf (no-op). */
void buffer_append_line_capped(Buffer *buf, const char *text, size_t max_lines);

#endif
