#include "mode.h"
#include "buffer.h"
#include "effects.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* String-keyed mode registry.
 * Indexed by insertion order; lookup is linear (14 modes, ~1 ns). */
static MajorMode mode_registry[MAX_MODES];
static int mode_count = 0;

/* Persistent storage for mode names (survives edn_free). */
static char mode_name_pool[MAX_MODES][32];

/* Extension pool shared across all modes. */
#define MAX_EXT_STRINGS 64
static char ext_storage[MAX_EXT_STRINGS][16];
static const char *ext_ptrs[MAX_EXT_STRINGS];
static int ext_pool_count = 0;

static const char *c_exts[] = {".c", ".h", ".cc", ".cpp", ".hpp", NULL};
static const char *clj_exts[] = {".clj", ".cljs", ".cljc", ".edn", ".bb", NULL};
static const char *bash_exts[] = {".sh", ".bash", ".zsh", NULL};
static const char *md_exts[] = {".md", ".markdown", NULL};

/* Register a mode. Returns the interned name pointer. */
static const char *register_mode(const char *name, SyntaxLang lang,
                                  Keymap *km, const char **exts) {
    if (mode_count >= MAX_MODES) return "Fundamental";
    int i = mode_count++;
    snprintf(mode_name_pool[i], sizeof(mode_name_pool[i]), "%s", name);
    mode_registry[i] = (MajorMode){
        .name       = mode_name_pool[i],
        .syntax_lang = lang,
        .keymap      = km,
        .extensions  = exts,
    };
    return mode_name_pool[i];
}

void modes_init(void) {
    mode_count = 0;
    register_mode("Fundamental", LANG_NONE,     NULL, NULL);
    register_mode("C",           LANG_C,        NULL, c_exts);
    register_mode("Clojure",     LANG_CLOJURE,  NULL, clj_exts);
    register_mode("Bash",        LANG_BASH,     NULL, bash_exts);
    register_mode("Markdown",    LANG_MARKDOWN, NULL, md_exts);
    register_mode("Git-Status",  LANG_NONE,     NULL, NULL);
    register_mode("Shell",       LANG_NONE,     NULL, NULL);
    register_mode("Buffer-List", LANG_NONE,     NULL, NULL);
    register_mode("Diff",        LANG_DIFF,     NULL, NULL);
    register_mode("Grep",        LANG_NONE,     NULL, NULL);
    register_mode("Help",        LANG_HELP,     NULL, NULL);
    register_mode("Symbol-Browser", LANG_NONE,  NULL, NULL);
    register_mode("Symbol-Detail",  LANG_NONE,  NULL, NULL);
    register_mode("Apropos",     LANG_NONE,     NULL, NULL);
}

MajorMode *mode_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < mode_count; i++) {
        if (strcasecmp(mode_registry[i].name, name) == 0)
            return &mode_registry[i];
    }
    return NULL;
}

const char *mode_detect(const char *filename) {
    if (!filename) return "Fundamental";
    for (int i = 0; i < mode_count; i++) {
        const char **exts = mode_registry[i].extensions;
        if (!exts) continue;
        for (const char **ext = exts; *ext; ext++) {
            if (str_ends_with(filename, *ext))
                return mode_registry[i].name;
        }
    }
    return "Fundamental";
}

SyntaxLang mode_syntax_for(const char *name) {
    MajorMode *m = mode_find(name);
    return m ? m->syntax_lang : LANG_NONE;
}

Keymap *mode_keymap_for(const char *name) {
    MajorMode *m = mode_find(name);
    return m ? m->keymap : NULL;
}

void buffer_set_mode(Buffer *buf, const char *name) {
    buf->mode_name = name ? name : "Fundamental";
}

/* Map syntax language name string to SyntaxLang enum. */
static SyntaxLang syntax_lang_from_name(const char *name) {
    if (!name || strcasecmp(name, "none") == 0) return LANG_NONE;
    if (strcasecmp(name, "c") == 0)        return LANG_C;
    if (strcasecmp(name, "clojure") == 0)  return LANG_CLOJURE;
    if (strcasecmp(name, "bash") == 0)     return LANG_BASH;
    if (strcasecmp(name, "markdown") == 0) return LANG_MARKDOWN;
    if (strcasecmp(name, "diff") == 0)     return LANG_DIFF;
    if (strcasecmp(name, "help") == 0)     return LANG_HELP;
    if (strcasecmp(name, "makefile") == 0) return LANG_MAKEFILE;
    return LANG_NONE;
}

void modes_load_edn(const char *edn) {
    if (!edn) return;
    const char *start = strchr(edn, '[');
    if (!start) return;

    size_t len = strlen(start);
    size_t consumed = 0;
    EdnVal *root = edn_parse(start, len, &consumed);
    if (!root || root->type != EDN_VECTOR) {
        edn_free(root);
        return;
    }

    /* Reset registry and extension pool. */
    mode_count = 0;
    ext_pool_count = 0;

    /* Parse each mode: [id "name" "syntax" ["ext1" "ext2"] "keymap-or-nil"]
     * The `id` field is ignored (legacy); we register by name. */
    for (int i = 0; i < root->vec.count; i++) {
        EdnVal *entry = root->vec.items[i];
        if (!entry || entry->type != EDN_VECTOR || entry->vec.count < 5)
            continue;

        EdnVal *name_v   = entry->vec.items[1];
        EdnVal *syntax_v = entry->vec.items[2];
        EdnVal *exts_v   = entry->vec.items[3];
        EdnVal *keymap_v = entry->vec.items[4];

        if (!name_v || name_v->type != EDN_STRING) continue;
        if (!syntax_v || syntax_v->type != EDN_STRING) continue;

        /* Build extensions array. */
        const char **ext_list = NULL;
        if (exts_v && exts_v->type == EDN_VECTOR && exts_v->vec.count > 0 &&
            ext_pool_count < MAX_EXT_STRINGS - 1) {
            int ext_start = ext_pool_count;
            for (int j = 0; j < exts_v->vec.count &&
                            ext_pool_count < MAX_EXT_STRINGS - 1; j++) {
                EdnVal *ext = exts_v->vec.items[j];
                if (ext && ext->type == EDN_STRING) {
                    snprintf(ext_storage[ext_pool_count],
                             sizeof(ext_storage[0]), "%s", ext->str);
                    ext_ptrs[ext_pool_count] = ext_storage[ext_pool_count];
                    ext_pool_count++;
                }
            }
            ext_ptrs[ext_pool_count++] = NULL;
            ext_list = &ext_ptrs[ext_start];
        }

        /* Find mode keymap. */
        Keymap *km = NULL;
        if (keymap_v && keymap_v->type == EDN_STRING &&
            strcmp(keymap_v->str, "nil") != 0) {
            km = keymap_find_mode(keymap_v->str);
        }

        register_mode(name_v->str,
                       syntax_lang_from_name(syntax_v->str),
                       km, ext_list);
    }

    edn_free(root);
}
