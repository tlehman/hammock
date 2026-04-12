#ifndef MODE_H
#define MODE_H

#include "keymap.h"
#include "syntax.h"

/* Forward-declare Buffer to avoid circular include. */
struct Buffer;

/* Major mode: string-keyed registry.
 * Adding a new mode is a pure clj/modes.clj edit — no C enum to extend.
 * C holds a cached snapshot of the Clojure mode registry, rebuilt when
 * the :modes domain version counter bumps. */

#define MAX_MODES 32

typedef struct {
    const char *name;        /* interned, non-NULL */
    SyntaxLang syntax_lang;
    Keymap *keymap;          /* mode-specific keybindings, NULL for global only */
    const char **extensions; /* NULL-terminated, or NULL */
} MajorMode;

/* Initialize all modes (C fallback when SCI is unavailable). */
void modes_init(void);

/* Load modes from EDN string (from Clojure export). */
void modes_load_edn(const char *edn);

/* Detect mode for a buffer based on filename. Returns interned name
 * (e.g. "Clojure", "Markdown") or "Fundamental" if no match. */
const char *mode_detect(const char *filename);

/* Look up a mode by name. Returns NULL if not found. */
MajorMode *mode_find(const char *name);

/* Get syntax language for a mode name. Returns LANG_NONE if not found. */
SyntaxLang mode_syntax_for(const char *name);

/* Get keymap for a mode name. Returns NULL if no mode-specific keymap. */
Keymap *mode_keymap_for(const char *name);

/* Set buffer's major mode by name. The name must be interned (e.g. from
 * mode_detect, a string literal, or mode_find()->name). */
void buffer_set_mode(struct Buffer *buf, const char *name);

#endif
