#include "mode.h"
#include "effects.h"
#include "util.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

MajorMode major_modes[MODE_COUNT];

/* Mode-specific keymaps */
static Keymap markdown_keymap;
static Keymap git_keymap;
static Keymap clojure_keymap;
static Keymap grep_keymap;
static Keymap buflist_keymap;

static const char *c_exts[] = {".c", ".h", ".cc", ".cpp", ".hpp", NULL};
static const char *clj_exts[] = {".clj", ".cljs", ".cljc", ".edn", ".bb", NULL};
static const char *bash_exts[] = {".sh", ".bash", ".zsh", NULL};
static const char *md_exts[] = {".md", ".markdown", NULL};

void modes_init(void) {
    /* Fundamental mode */
    major_modes[MODE_FUNDAMENTAL] = (MajorMode){
        .id = MODE_FUNDAMENTAL,
        .name = "Fundamental",
        .syntax_lang = LANG_NONE,
        .keymap = NULL,
        .extensions = NULL,
    };

    /* C mode */
    major_modes[MODE_C] = (MajorMode){
        .id = MODE_C,
        .name = "C",
        .syntax_lang = LANG_C,
        .keymap = NULL,
        .extensions = c_exts,
    };

    /* Clojure mode */
    keymap_init(&clojure_keymap, "clojure");
    /* C-j for eval-last-sexp is already global, but ensure it's here too */
    major_modes[MODE_CLOJURE] = (MajorMode){
        .id = MODE_CLOJURE,
        .name = "Clojure",
        .syntax_lang = LANG_CLOJURE,
        .keymap = &clojure_keymap,
        .extensions = clj_exts,
    };

    /* Bash mode */
    major_modes[MODE_BASH] = (MajorMode){
        .id = MODE_BASH,
        .name = "Bash",
        .syntax_lang = LANG_BASH,
        .keymap = NULL,
        .extensions = bash_exts,
    };

    /* Markdown mode */
    keymap_init(&markdown_keymap, "markdown");
    keymap_bind(&markdown_keymap, HK_ENTER, 0, "markdown-follow-link");
    keymap_bind(&markdown_keymap, HK_TAB, 0, "markdown-next-link");
    keymap_bind(&markdown_keymap, 'l', 0, "markdown-go-back");
    keymap_bind(&markdown_keymap, 'n', 0, "markdown-next-heading");
    keymap_bind(&markdown_keymap, 'p', 0, "markdown-prev-heading");

    major_modes[MODE_MARKDOWN] = (MajorMode){
        .id = MODE_MARKDOWN,
        .name = "Markdown",
        .syntax_lang = LANG_MARKDOWN,
        .keymap = &markdown_keymap,
        .extensions = md_exts,
    };

    /* Git status mode */
    keymap_init(&git_keymap, "git-status");
    keymap_bind(&git_keymap, 's', 0, "git-stage");
    keymap_bind(&git_keymap, 'u', 0, "git-unstage");
    keymap_bind(&git_keymap, 'q', 0, "git-quit");
    keymap_bind(&git_keymap, 'g', 0, "git-refresh");
    keymap_bind(&git_keymap, 'd', 0, "git-diff");
    keymap_bind(&git_keymap, HK_TAB, 0, "git-toggle-section");
    keymap_bind(&git_keymap, HK_ENTER, 0, "git-visit-file");

    major_modes[MODE_GIT_STATUS] = (MajorMode){
        .id = MODE_GIT_STATUS,
        .name = "Git-Status",
        .syntax_lang = LANG_NONE,
        .keymap = &git_keymap,
        .extensions = NULL,
    };

    /* Shell mode */
    major_modes[MODE_SHELL] = (MajorMode){
        .id = MODE_SHELL,
        .name = "Shell",
        .syntax_lang = LANG_NONE,
        .keymap = NULL,
        .extensions = NULL,
    };

    /* Buffer list mode */
    keymap_init(&buflist_keymap, "buffer-list");
    keymap_bind(&buflist_keymap, HK_ENTER, 0, "buflist-visit");
    keymap_bind(&buflist_keymap, 'D', 0, "buflist-mark-delete");
    keymap_bind(&buflist_keymap, 'x', 0, "buflist-execute");
    keymap_bind(&buflist_keymap, 'q', 0, "buflist-quit");

    major_modes[MODE_BUFFER_LIST] = (MajorMode){
        .id = MODE_BUFFER_LIST,
        .name = "Buffer-List",
        .syntax_lang = LANG_NONE,
        .keymap = &buflist_keymap,
        .extensions = NULL,
    };

    /* Diff mode */
    major_modes[MODE_DIFF] = (MajorMode){
        .id = MODE_DIFF,
        .name = "Diff",
        .syntax_lang = LANG_DIFF,
        .keymap = NULL,
        .extensions = NULL,
    };

    /* Grep mode */
    keymap_init(&grep_keymap, "grep");
    keymap_bind(&grep_keymap, HK_ENTER, 0, "grep-visit");
    keymap_bind(&grep_keymap, 'q', 0, "grep-quit");
    keymap_bind(&grep_keymap, 'g', 0, "grep-refresh");

    major_modes[MODE_GREP] = (MajorMode){
        .id = MODE_GREP,
        .name = "Grep",
        .syntax_lang = LANG_NONE,
        .keymap = &grep_keymap,
        .extensions = NULL,
    };

    /* Help mode */
    major_modes[MODE_HELP] = (MajorMode){
        .id = MODE_HELP,
        .name = "Help",
        .syntax_lang = LANG_HELP,
        .keymap = NULL,
        .extensions = NULL,
    };
}

MajorModeID mode_detect(const char *filename) {
    if (!filename) return MODE_FUNDAMENTAL;

    for (int m = 0; m < MODE_COUNT; m++) {
        const char **exts = major_modes[m].extensions;
        if (!exts) continue;
        for (const char **ext = exts; *ext; ext++) {
            if (str_ends_with(filename, *ext))
                return (MajorModeID)m;
        }
    }

    return MODE_FUNDAMENTAL;
}

const char *mode_name(MajorModeID id) {
    if (id >= 0 && id < MODE_COUNT)
        return major_modes[id].name;
    return "Unknown";
}

SyntaxLang mode_syntax(MajorModeID id) {
    if (id >= 0 && id < MODE_COUNT)
        return major_modes[id].syntax_lang;
    return LANG_NONE;
}

void buffer_set_mode(Buffer *buf, MajorModeID mode) {
    buf->major_mode = mode;
}

/* Map syntax language name string to SyntaxLang enum */
static SyntaxLang syntax_lang_from_name(const char *name) {
    if (!name || strcasecmp(name, "none") == 0) return LANG_NONE;
    if (strcasecmp(name, "c") == 0) return LANG_C;
    if (strcasecmp(name, "clojure") == 0) return LANG_CLOJURE;
    if (strcasecmp(name, "bash") == 0) return LANG_BASH;
    if (strcasecmp(name, "markdown") == 0) return LANG_MARKDOWN;
    if (strcasecmp(name, "diff") == 0) return LANG_DIFF;
    if (strcasecmp(name, "help") == 0) return LANG_HELP;
    return LANG_NONE;
}

/* Persistent extension string storage for EDN-loaded modes */
#define MAX_EXT_STRINGS 64
static char ext_storage[MAX_EXT_STRINGS][16];
static const char *ext_ptrs[MAX_EXT_STRINGS];
static int ext_storage_count = 0;

/* Mode name storage */
static char mode_name_storage[MODE_COUNT][32];

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

    /* Reset extension pool: this function is called on every live-reload,
     * so without this the pool grows unboundedly and eventually stomps
     * adjacent static memory. */
    ext_storage_count = 0;

    /* Parse each mode: [id "name" "syntax" ["ext1" "ext2"] "keymap-or-nil"] */
    for (int i = 0; i < root->vec.count; i++) {
        EdnVal *entry = root->vec.items[i];
        if (!entry || entry->type != EDN_VECTOR || entry->vec.count < 5)
            continue;

        EdnVal *id_v = entry->vec.items[0];
        EdnVal *name_v = entry->vec.items[1];
        EdnVal *syntax_v = entry->vec.items[2];
        EdnVal *exts_v = entry->vec.items[3];
        EdnVal *keymap_v = entry->vec.items[4];

        if (!id_v || id_v->type != EDN_INT) continue;
        if (!name_v || name_v->type != EDN_STRING) continue;
        if (!syntax_v || syntax_v->type != EDN_STRING) continue;

        int id = (int)id_v->num;
        if (id < 0 || id >= MODE_COUNT) continue;

        /* Store mode name persistently */
        snprintf(mode_name_storage[id], sizeof(mode_name_storage[id]), "%s", name_v->str);

        /* Build extensions array */
        const char **ext_list = NULL;
        if (exts_v && exts_v->type == EDN_VECTOR && exts_v->vec.count > 0 &&
            ext_storage_count < MAX_EXT_STRINGS - 1) {
            int ext_start = ext_storage_count;
            for (int j = 0; j < exts_v->vec.count && ext_storage_count < MAX_EXT_STRINGS - 1; j++) {
                EdnVal *ext = exts_v->vec.items[j];
                if (ext && ext->type == EDN_STRING) {
                    snprintf(ext_storage[ext_storage_count], sizeof(ext_storage[0]),
                             "%s", ext->str);
                    ext_ptrs[ext_storage_count] = ext_storage[ext_storage_count];
                    ext_storage_count++;
                }
            }
            ext_ptrs[ext_storage_count++] = NULL;
            ext_list = &ext_ptrs[ext_start];
        }

        /* Find mode keymap */
        Keymap *km = NULL;
        if (keymap_v && keymap_v->type == EDN_STRING &&
            strcmp(keymap_v->str, "nil") != 0) {
            km = keymap_find_mode(keymap_v->str);
        }

        major_modes[id] = (MajorMode){
            .id = (MajorModeID)id,
            .name = mode_name_storage[id],
            .syntax_lang = syntax_lang_from_name(syntax_v->str),
            .keymap = km,
            .extensions = ext_list,
        };
    }

    edn_free(root);
}
