#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdbool.h>

/* Memory allocation wrappers that abort on failure */
void *hmalloc(size_t size);
void *hrealloc(void *ptr, size_t size);
char *hstrdup(const char *s);
char *hstrndup(const char *s, size_t n);

/* Kill ring */
#define KILL_RING_SIZE 16

typedef struct {
    char *entries[KILL_RING_SIZE];
    int head;
    int count;
} KillRing;

void kill_ring_init(KillRing *kr);
void kill_ring_push(KillRing *kr, const char *text);
const char *kill_ring_top(KillRing *kr);
/* Return the entry `offset` steps older than the newest (0 = newest).
 * Wraps around modulo count. Returns NULL when the ring is empty. */
const char *kill_ring_nth(KillRing *kr, int offset);
void kill_ring_free(KillRing *kr);

/* Undo system */
typedef enum {
    UNDO_INSERT,
    UNDO_DELETE,
    UNDO_BOUNDARY,
} UndoType;

typedef struct UndoEntry {
    UndoType type;
    size_t pos;
    char *text;
    size_t len;
    struct UndoEntry *next;
} UndoEntry;

typedef struct {
    UndoEntry *head;
} UndoList;

void undo_init(UndoList *ul);
void undo_record_insert(UndoList *ul, size_t pos, const char *text, size_t len);
void undo_record_delete(UndoList *ul, size_t pos, const char *text, size_t len);
void undo_add_boundary(UndoList *ul);
UndoEntry *undo_pop(UndoList *ul);
void undo_free(UndoList *ul);
void undo_entry_free(UndoEntry *e);

/* System clipboard */
void clipboard_copy(const char *text);
char *clipboard_paste(void);  /* Returns malloc'd string or NULL */

/* String utilities */
bool str_ends_with(const char *s, const char *suffix);
char *path_join(const char *dir, const char *file);

#endif
