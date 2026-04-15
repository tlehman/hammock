#ifndef EFFECTS_H
#define EFFECTS_H

#include <stddef.h>
#include <stdbool.h>

/* EDN mini-parser for the restricted subset used by effect vectors.
 * Supports: vectors, keywords, strings, integers, booleans, nil.
 */

typedef enum {
    EDN_KEYWORD,
    EDN_STRING,
    EDN_INT,
    EDN_BOOL,
    EDN_NIL,
    EDN_VECTOR,
    EDN_CHAR,
    EDN_MAP,
} EdnType;

typedef struct EdnVal {
    EdnType type;
    union {
        char *str;           /* EDN_KEYWORD (without colon), EDN_STRING */
        long long num;       /* EDN_INT */
        bool bval;           /* EDN_BOOL */
        int  ch;             /* EDN_CHAR */
        struct {
            struct EdnVal **items;
            int count;
            int capacity;
        } vec;               /* EDN_VECTOR */
        struct {
            struct EdnVal **keys;
            struct EdnVal **vals;
            int count;
            int capacity;
        } map;               /* EDN_MAP */
    };
} EdnVal;

/* Parse EDN from string. Returns NULL on error.
 * consumed is set to the number of bytes parsed. */
EdnVal *edn_parse(const char *s, size_t len, size_t *consumed);
void edn_free(EdnVal *v);

/* Execute an EDN effect vector string returned by Clojure.
 * Returns 0 on success, -1 on parse error. */
int effects_execute(const char *edn_effects);

/* Build an EDN state snapshot string from current C editor state.
 * Caller must free the returned string. */
char *state_snapshot_edn(void);

/* Push state snapshot to Clojure *editor* atom. */
void state_push_snapshot(void);

/* Drop the yank state used by :yank-pop. Call from any command that mutates
 * the buffer or kill ring between a :yank and the next :yank-pop, so M-y
 * says "Previous command was not a yank" just like Emacs. */
void yank_state_invalidate(void);

/* Look up a value by keyword name in an EDN_MAP. Returns NULL if missing. */
EdnVal *edn_map_get(EdnVal *m, const char *kw);

#endif
