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
} EdnType;

typedef struct EdnVal {
    EdnType type;
    union {
        char *str;           /* EDN_KEYWORD (without colon), EDN_STRING */
        long long num;       /* EDN_INT */
        bool bval;           /* EDN_BOOL */
        struct {
            struct EdnVal **items;
            int count;
            int capacity;
        } vec;               /* EDN_VECTOR */
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

#endif
