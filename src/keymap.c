#include "keymap.h"
#include "effects.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

Keymap global_keymap;
Keymap cx_keymap;
Keymap ch_keymap;
Keymap f1_keymap;

void keymap_init(Keymap *km, const char *name) {
    memset(km, 0, sizeof(*km));
    km->name = name;
}

void keymap_bind(Keymap *km, int key, int modifiers, const char *command) {
    /* Check for existing binding to update */
    for (int i = 0; i < km->count; i++) {
        if (km->bindings[i].key == key && km->bindings[i].modifiers == modifiers) {
            km->bindings[i].command = command;
            km->bindings[i].submap = NULL;
            return;
        }
    }
    if (km->count >= MAX_BINDINGS) return;
    km->bindings[km->count].key = key;
    km->bindings[km->count].modifiers = modifiers;
    km->bindings[km->count].command = command;
    km->bindings[km->count].submap = NULL;
    km->count++;
}

void keymap_bind_prefix(Keymap *km, int key, int modifiers, Keymap *submap) {
    for (int i = 0; i < km->count; i++) {
        if (km->bindings[i].key == key && km->bindings[i].modifiers == modifiers) {
            km->bindings[i].command = NULL;
            km->bindings[i].submap = submap;
            return;
        }
    }
    if (km->count >= MAX_BINDINGS) return;
    km->bindings[km->count].key = key;
    km->bindings[km->count].modifiers = modifiers;
    km->bindings[km->count].command = NULL;
    km->bindings[km->count].submap = submap;
    km->count++;
}

const char *keymap_lookup(Keymap *km, int key, int modifiers, Keymap **submap_out) {
    if (submap_out) *submap_out = NULL;
    for (int i = 0; i < km->count; i++) {
        if (km->bindings[i].key == key && km->bindings[i].modifiers == modifiers) {
            if (km->bindings[i].submap) {
                if (submap_out) *submap_out = km->bindings[i].submap;
                return NULL;
            }
            return km->bindings[i].command;
        }
    }
    return NULL;
}

/* Mode-specific keymaps loaded from Clojure */
#define MAX_MODE_KEYMAPS 16
static Keymap mode_keymaps[MAX_MODE_KEYMAPS];
static char mode_keymap_names[MAX_MODE_KEYMAPS][64];
static int mode_keymap_count = 0;

Keymap *keymap_find_mode(const char *name) {
    for (int i = 0; i < mode_keymap_count; i++) {
        if (strcmp(mode_keymap_names[i], name) == 0)
            return &mode_keymaps[i];
    }
    return NULL;
}

static Keymap *keymap_get_or_create_mode(const char *name) {
    Keymap *km = keymap_find_mode(name);
    if (km) return km;
    if (mode_keymap_count >= MAX_MODE_KEYMAPS) return NULL;
    int idx = mode_keymap_count++;
    keymap_init(&mode_keymaps[idx], name);
    snprintf(mode_keymap_names[idx], sizeof(mode_keymap_names[idx]), "%s", name);
    return &mode_keymaps[idx];
}

/* Persistent command name storage for EDN-loaded bindings */
#define MAX_CMD_STRINGS 512
static char cmd_strings[MAX_CMD_STRINGS][64];
static int cmd_string_count = 0;

static const char *intern_command_name(const char *name) {
    for (int i = 0; i < cmd_string_count; i++) {
        if (strcmp(cmd_strings[i], name) == 0)
            return cmd_strings[i];
    }
    if (cmd_string_count >= MAX_CMD_STRINGS) return name;
    snprintf(cmd_strings[cmd_string_count], sizeof(cmd_strings[0]), "%s", name);
    return cmd_strings[cmd_string_count++];
}

void keybindings_load_edn(const char *edn) {
    if (!edn) return;

    /* Find the vector start */
    const char *start = strchr(edn, '[');
    if (!start) return;

    size_t len = strlen(start);
    size_t consumed = 0;
    EdnVal *root = edn_parse(start, len, &consumed);
    if (!root || root->type != EDN_VECTOR) {
        edn_free(root);
        return;
    }

    /* Reset keymaps */
    keymap_init(&global_keymap, "global");
    keymap_init(&cx_keymap, "C-x");
    keymap_init(&ch_keymap, "C-h");
    keymap_init(&f1_keymap, "F1");
    mode_keymap_count = 0;

    /* Parse each binding: [keymap-name key modifiers command] */
    for (int i = 0; i < root->vec.count; i++) {
        EdnVal *entry = root->vec.items[i];
        if (!entry || entry->type != EDN_VECTOR || entry->vec.count < 4)
            continue;

        EdnVal *km_name_v = entry->vec.items[0];
        EdnVal *key_v = entry->vec.items[1];
        EdnVal *mod_v = entry->vec.items[2];
        EdnVal *cmd_v = entry->vec.items[3];

        if (!km_name_v || km_name_v->type != EDN_STRING) continue;
        if (!key_v || key_v->type != EDN_INT) continue;
        if (!mod_v || mod_v->type != EDN_INT) continue;

        const char *km_name = km_name_v->str;
        int key = (int)key_v->num;
        int mods = (int)mod_v->num;

        /* Handle prefix binding */
        if (strcmp(km_name, "prefix") == 0) {
            if (cmd_v->type == EDN_STRING && strcmp(cmd_v->str, "cx") == 0) {
                keymap_bind_prefix(&global_keymap, key, mods, &cx_keymap);
            } else if (cmd_v->type == EDN_STRING && strcmp(cmd_v->str, "ch") == 0) {
                keymap_bind_prefix(&global_keymap, key, mods, &ch_keymap);
            } else if (cmd_v->type == EDN_STRING && strcmp(cmd_v->str, "f1") == 0) {
                keymap_bind_prefix(&global_keymap, key, mods, &f1_keymap);
            }
            continue;
        }

        if (!cmd_v || cmd_v->type != EDN_STRING) continue;
        const char *cmd = intern_command_name(cmd_v->str);

        /* Determine target keymap */
        Keymap *target = NULL;
        if (strcmp(km_name, "global") == 0) {
            target = &global_keymap;
        } else if (strcmp(km_name, "cx") == 0) {
            target = &cx_keymap;
        } else if (strcmp(km_name, "ch") == 0) {
            target = &ch_keymap;
        } else if (strcmp(km_name, "f1") == 0) {
            target = &f1_keymap;
        } else if (strncmp(km_name, "mode:", 5) == 0) {
            target = keymap_get_or_create_mode(km_name + 5);
        }

        if (target) {
            keymap_bind(target, key, mods, cmd);
        }
    }

    edn_free(root);
}

const char *key_name(int key, int modifiers) {
    static char buf[32];
    char *p = buf;

    if (modifiers & MOD_CTRL) { *p++ = 'C'; *p++ = '-'; }
    if (modifiers & MOD_META) { *p++ = 'M'; *p++ = '-'; }

    if (key >= 32 && key < 127) {
        *p++ = (char)key;
        *p = '\0';
    } else {
        switch (key) {
            case HK_BACKSPACE: strcpy(p, "Backspace"); break;
            case HK_DELETE: strcpy(p, "Delete"); break;
            case HK_UP: strcpy(p, "Up"); break;
            case HK_DOWN: strcpy(p, "Down"); break;
            case HK_LEFT: strcpy(p, "Left"); break;
            case HK_RIGHT: strcpy(p, "Right"); break;
            case HK_HOME: strcpy(p, "Home"); break;
            case HK_END: strcpy(p, "End"); break;
            case HK_PGUP: strcpy(p, "PgUp"); break;
            case HK_PGDN: strcpy(p, "PgDn"); break;
            case HK_TAB: strcpy(p, "Tab"); break;
            case HK_SHIFT_TAB: strcpy(p, "Shift-Tab"); break;
            case HK_ENTER: strcpy(p, "Enter"); break;
            default: snprintf(p, sizeof(buf) - (size_t)(p - buf), "<%d>", key); break;
        }
    }
    return buf;
}

static int reverse_lookup_keymap(Keymap *km, const char *prefix,
                                  const char *command,
                                  KeyBindingResult *results, int max, int count) {
    for (int i = 0; i < km->count && count < max; i++) {
        if (km->bindings[i].command &&
            strcmp(km->bindings[i].command, command) == 0) {
            results[count].key = km->bindings[i].key;
            results[count].modifiers = km->bindings[i].modifiers;
            results[count].prefix = prefix;
            count++;
        }
    }
    return count;
}

int keymap_reverse_lookup(const char *command, KeyBindingResult *results, int max_results) {
    int count = 0;
    /* Search global keymap */
    count = reverse_lookup_keymap(&global_keymap, "", command, results, max_results, count);
    /* Search C-x keymap */
    count = reverse_lookup_keymap(&cx_keymap, "C-x ", command, results, max_results, count);
    /* Search C-h keymap */
    count = reverse_lookup_keymap(&ch_keymap, "C-h ", command, results, max_results, count);
    /* Search F1 keymap */
    count = reverse_lookup_keymap(&f1_keymap, "F1 ", command, results, max_results, count);
    /* Search mode keymaps */
    for (int i = 0; i < mode_keymap_count && count < max_results; i++) {
        count = reverse_lookup_keymap(&mode_keymaps[i], mode_keymap_names[i],
                                       command, results, max_results, count);
    }
    return count;
}

const char *keybinding_format(KeyBindingResult *r) {
    static char buf[64];
    const char *kn = key_name(r->key, r->modifiers);
    if (r->prefix[0]) {
        snprintf(buf, sizeof(buf), "%s%s", r->prefix, kn);
    } else {
        snprintf(buf, sizeof(buf), "%s", kn);
    }
    return buf;
}
