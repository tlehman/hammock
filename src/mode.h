#ifndef MODE_H
#define MODE_H

#include "keymap.h"
#include "syntax.h"
#include "buffer.h"

/* Major mode IDs */
typedef enum {
    MODE_FUNDAMENTAL = 0,
    MODE_C,
    MODE_CLOJURE,
    MODE_BASH,
    MODE_MARKDOWN,
    MODE_GIT_STATUS,
    MODE_SHELL,
    MODE_BUFFER_LIST,
    MODE_DIFF,
    MODE_GREP,
    MODE_HELP,
    MODE_COUNT,
} MajorModeID;

typedef struct {
    MajorModeID id;
    const char *name;
    SyntaxLang syntax_lang;
    Keymap *keymap;         /* mode-specific keybindings, NULL for global only */
    const char **extensions;
} MajorMode;

extern MajorMode major_modes[MODE_COUNT];

/* Initialize all modes */
void modes_init(void);

/* Load modes from EDN string (from Clojure export) */
void modes_load_edn(const char *edn);

/* Detect mode for a buffer based on filename */
MajorModeID mode_detect(const char *filename);

/* Get mode name string */
const char *mode_name(MajorModeID id);

/* Get syntax language for a mode */
SyntaxLang mode_syntax(MajorModeID id);

/* Set buffer's major mode */
void buffer_set_mode(Buffer *buf, MajorModeID mode);

#endif
