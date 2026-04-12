#ifndef KEYMAP_H
#define KEYMAP_H

#include <stdbool.h>

/* Key modifiers */
#define MOD_CTRL  (1 << 0)
#define MOD_META  (1 << 1)

/* Special key codes (above normal char range) */
#define KEY_ESCAPE_SEQ  0x1000
#define HK_BACKSPACE    0x1001
#define HK_DELETE       0x1002
#define HK_UP           0x1003
#define HK_DOWN         0x1004
#define HK_LEFT         0x1005
#define HK_RIGHT        0x1006
#define HK_HOME         0x1007
#define HK_END          0x1008
#define HK_PGUP         0x1009
#define HK_PGDN         0x100A
#define HK_TAB          0x100B
#define HK_ENTER        0x100C
#define HK_SHIFT_TAB    0x100D
#define HK_F1           0x1010
#define HK_MOUSE        0x1100

/* Key event */
typedef struct {
    int key;
    int modifiers;
} KeyEvent;

/* Keybinding entry */
typedef struct {
    int key;
    int modifiers;
    const char *command;    /* command name, or NULL if prefix */
    struct Keymap *submap;  /* non-NULL for prefix keys */
} KeyBinding;

#define MAX_BINDINGS 256

typedef struct Keymap {
    KeyBinding bindings[MAX_BINDINGS];
    int count;
    const char *name;
} Keymap;

extern Keymap global_keymap;
extern Keymap cx_keymap;        /* C-x prefix */
extern Keymap ch_keymap;        /* C-h help prefix */

void keymap_init(Keymap *km, const char *name);
void keymap_bind(Keymap *km, int key, int modifiers, const char *command);
void keymap_bind_prefix(Keymap *km, int key, int modifiers, Keymap *submap);
const char *keymap_lookup(Keymap *km, int key, int modifiers, Keymap **submap_out);

/* Load keybindings from EDN string (from Clojure export).
 * This is the sole entry point for populating keymaps — all binding
 * definitions live in clj/keybindings.clj. */
void keybindings_load_edn(const char *edn);

/* Find a mode-specific keymap by name (loaded from Clojure) */
Keymap *keymap_find_mode(const char *name);

/* Key description for display */
const char *key_name(int key, int modifiers);

/* Reverse keybinding lookup: find all key sequences bound to a command */
typedef struct {
    int key;
    int modifiers;
    const char *prefix;  /* "" for global, "C-x " for cx_keymap, etc. */
} KeyBindingResult;

#define MAX_BINDING_RESULTS 16
int keymap_reverse_lookup(const char *command, KeyBindingResult *results, int max_results);

/* Format a keybinding result as a human-readable string (static buffer) */
const char *keybinding_format(KeyBindingResult *r);

/* F1 help prefix keymap */
extern Keymap f1_keymap;

#endif
