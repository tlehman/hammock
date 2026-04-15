#include "font_lock.h"
#include "effects.h"
#include "util.h"
#ifndef FONT_LOCK_TEST_BUILD
#include "sci.h"
#endif
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static TokenType face_from_keyword(const char *kw) {
    if (!kw) return TOK_NORMAL;
    if (!strcmp(kw, "normal"))       return TOK_NORMAL;
    if (!strcmp(kw, "keyword"))      return TOK_KEYWORD;
    if (!strcmp(kw, "string"))       return TOK_STRING;
    if (!strcmp(kw, "comment"))      return TOK_COMMENT;
    if (!strcmp(kw, "type"))         return TOK_TYPE;
    if (!strcmp(kw, "function"))     return TOK_FUNCTION;
    if (!strcmp(kw, "number"))       return TOK_NUMBER;
    if (!strcmp(kw, "preproc"))      return TOK_PREPROC;
    if (!strcmp(kw, "heading1"))     return TOK_HEADING1;
    if (!strcmp(kw, "heading2"))     return TOK_HEADING2;
    if (!strcmp(kw, "heading3"))     return TOK_HEADING3;
    if (!strcmp(kw, "link"))         return TOK_LINK;
    if (!strcmp(kw, "code"))         return TOK_CODE;
    if (!strcmp(kw, "bold"))         return TOK_BOLD;
    if (!strcmp(kw, "italic"))       return TOK_ITALIC;
    if (!strcmp(kw, "diff-add"))     return TOK_DIFF_ADD;
    if (!strcmp(kw, "diff-del"))     return TOK_DIFF_DEL;
    if (!strcmp(kw, "diff-header")) return TOK_DIFF_HEADER;
    if (!strcmp(kw, "math"))         return TOK_MATH;
    if (!strcmp(kw, "math-delim"))  return TOK_MATH_DELIM;
    /* Warn once per unknown */
    static char *seen[32];
    static int  n_seen = 0;
    for (int i = 0; i < n_seen; i++) if (!strcmp(seen[i], kw)) return TOK_NORMAL;
    if (n_seen < 32) seen[n_seen++] = hstrdup(kw);
    fprintf(stderr, "[font-lock] unknown face: :%s\n", kw);
    return TOK_NORMAL;
}

typedef struct {
    regex_t re;
    int    *group_faces;
    int     n_groups;
    bool    compiled;
    bool    subgroup_mode;  /* true if face spec was a vector (per-subgroup) */
} FlRule;

typedef struct {
    char  *name;
    char  *line_comment;
    char  *block_open;
    char  *block_close;
    char   string_delims[4];
    int    n_string_delims;
    char   char_delim;
    char   string_escape;
    FlRule *rules;
    int     n_rules;
    regex_t *fence_open;
    regex_t *fence_close;
    int      fence_lang_group;
    struct { char *tag; char *mode_name; } *fence_langs;
    int      n_fence_langs;
    bool     use_legacy_help;
} FlMode;

#define FL_MAX_MODES 32
static FlMode *g_modes[FL_MAX_MODES];
static int    g_mode_count = 0;

#define FL_MAX_REGIONS 16
typedef struct { int start; int end; } FlRegion;

static FlMode *mode_find_by_name(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < g_mode_count; i++) {
        if (g_modes[i] && g_modes[i]->name && strcmp(g_modes[i]->name, name) == 0)
            return g_modes[i];
    }
    return NULL;
}

static void add_span(LineHighlight *hl, int start, int length, TokenType type) {
    if (length <= 0) return;
    if (hl->count >= MAX_SPANS) return;
    hl->spans[hl->count].start  = start;
    hl->spans[hl->count].length = length;
    hl->spans[hl->count].type   = type;
    hl->count++;
}

/* Forward decls */
static void fl_mode_free(FlMode *m);
static FlMode *fl_mode_from_edn(EdnVal *entry);
static bool fl_rule_compile(EdnVal *entry, FlRule *out,
                             const char *mode_name, int rule_idx);
static const char *fl_lang_to_name(SyntaxLang l);
static int lang_index_for_mode_name(const char *name);

static char *dup_string_val(EdnVal *v) {
    if (!v || v->type != EDN_STRING) return NULL;
    return hstrdup(v->str);
}

static FlMode *fl_mode_from_edn(EdnVal *entry) {
    if (!entry || entry->type != EDN_MAP) return NULL;
    FlMode *m = hmalloc(sizeof(FlMode));
    memset(m, 0, sizeof(*m));

    EdnVal *name = edn_map_get(entry, "name");
    m->name = dup_string_val(name);
    if (!m->name) { fl_mode_free(m); return NULL; }

    EdnVal *syn = edn_map_get(entry, "syntax");
    if (!syn || syn->type != EDN_MAP) return m;

    EdnVal *engine = edn_map_get(syn, "engine");
    if (engine && engine->type == EDN_KEYWORD &&
        strcmp(engine->str, "builtin-help") == 0) {
        m->use_legacy_help = true;
        return m;
    }

    EdnVal *st = edn_map_get(syn, "syntax-table");
    if (st && st->type == EDN_MAP) {
        m->line_comment = dup_string_val(edn_map_get(st, "comment-line"));

        EdnVal *block = edn_map_get(st, "comment-block");
        if (block && block->type == EDN_VECTOR && block->vec.count == 2) {
            m->block_open  = dup_string_val(block->vec.items[0]);
            m->block_close = dup_string_val(block->vec.items[1]);
        }

        EdnVal *delims = edn_map_get(st, "string-delims");
        if (delims && delims->type == EDN_VECTOR) {
            for (int i = 0; i < delims->vec.count && m->n_string_delims < 4; i++) {
                EdnVal *c = delims->vec.items[i];
                if (c && c->type == EDN_CHAR)
                    m->string_delims[m->n_string_delims++] = (char)c->ch;
            }
        }

        EdnVal *cd = edn_map_get(st, "char-delim");
        if (cd && cd->type == EDN_CHAR) m->char_delim = (char)cd->ch;

        EdnVal *esc = edn_map_get(st, "string-escape");
        if (esc && esc->type == EDN_CHAR) m->string_escape = (char)esc->ch;
    }

    EdnVal *rules_v = edn_map_get(syn, "font-lock-keywords");
    if (rules_v && rules_v->type == EDN_VECTOR) {
        int n = rules_v->vec.count;
        m->rules = hmalloc(sizeof(FlRule) * (size_t)n);
        for (int i = 0; i < n; i++) {
            if (fl_rule_compile(rules_v->vec.items[i],
                                 &m->rules[m->n_rules], m->name, i)) {
                m->n_rules++;
            }
        }
    }

    /* Fence dispatch (e.g. Markdown ```lang ... ``` blocks) */
    EdnVal *fence = edn_map_get(syn, "fence");
    if (fence && fence->type == EDN_MAP) {
        EdnVal *openv  = edn_map_get(fence, "open");
        EdnVal *closev = edn_map_get(fence, "close");
        EdnVal *lgv    = edn_map_get(fence, "lang-group");
        EdnVal *langs  = edn_map_get(fence, "langs");

        if (openv && openv->type == EDN_STRING &&
            closev && closev->type == EDN_STRING) {
            regex_t *open_re  = hmalloc(sizeof(regex_t));
            regex_t *close_re = hmalloc(sizeof(regex_t));
            bool open_ok  = regcomp(open_re,  openv->str,  REG_EXTENDED) == 0;
            bool close_ok = open_ok &&
                            regcomp(close_re, closev->str, REG_EXTENDED) == 0;
            if (open_ok && close_ok) {
                m->fence_open  = open_re;
                m->fence_close = close_re;
                m->fence_lang_group = lgv && lgv->type == EDN_INT
                                       ? (int)lgv->num : 1;
            } else {
                fprintf(stderr, "[font-lock] fence regex compile failed: %s\n", m->name);
                if (open_ok) regfree(open_re);
                free(open_re);
                free(close_re);
            }
        }

        if (langs && langs->type == EDN_MAP && m->fence_open) {
            int n = langs->map.count;
            m->fence_langs = hmalloc(sizeof(*m->fence_langs) * (size_t)n);
            for (int i = 0; i < n; i++) {
                EdnVal *k = langs->map.keys[i];
                EdnVal *v = langs->map.vals[i];
                if (k && k->type == EDN_STRING && v && v->type == EDN_STRING) {
                    m->fence_langs[m->n_fence_langs].tag       = hstrdup(k->str);
                    m->fence_langs[m->n_fence_langs].mode_name = hstrdup(v->str);
                    m->n_fence_langs++;
                }
            }
        }
    }
    return m;
}

static void fl_mode_free(FlMode *m) {
    if (!m) return;
    free(m->name);
    free(m->line_comment);
    free(m->block_open);
    free(m->block_close);
    for (int i = 0; i < m->n_rules; i++) {
        if (m->rules[i].compiled) regfree(&m->rules[i].re);
        free(m->rules[i].group_faces);
    }
    free(m->rules);
    if (m->fence_open)  { regfree(m->fence_open);  free(m->fence_open); }
    if (m->fence_close) { regfree(m->fence_close); free(m->fence_close); }
    for (int i = 0; i < m->n_fence_langs; i++) {
        free(m->fence_langs[i].tag);
        free(m->fence_langs[i].mode_name);
    }
    free(m->fence_langs);
    free(m);
}

/* Build a word-boundary alternation regex: [[:<:]](w1|w2|...)[[:>:]] */
static char *build_words_regex(EdnVal *words) {
    if (!words || words->type != EDN_VECTOR || words->vec.count == 0) return NULL;
    size_t cap = 64;
    size_t len = 0;
    char *buf = hmalloc(cap);
    const char *prefix = "[[:<:]](";
    size_t plen = strlen(prefix);
    memcpy(buf, prefix, plen); len = plen;

    bool first = true;
    for (int i = 0; i < words->vec.count; i++) {
        EdnVal *w = words->vec.items[i];
        if (!w || w->type != EDN_STRING) continue;
        size_t wlen = strlen(w->str);
        size_t need = len + wlen + 2 + 16;
        if (need > cap) { while (cap < need) cap *= 2; buf = hrealloc(buf, cap); }
        if (!first) buf[len++] = '|';
        first = false;
        memcpy(buf + len, w->str, wlen);
        len += wlen;
    }
    if (first) { free(buf); return NULL; }

    const char *suffix = ")[[:>:]]";
    size_t slen = strlen(suffix);
    if (len + slen + 1 > cap) { cap = len + slen + 1; buf = hrealloc(buf, cap); }
    memcpy(buf + len, suffix, slen);
    len += slen;
    buf[len] = '\0';
    return buf;
}

/* Compile one rule (vector or map) into an FlRule. Returns true on success. */
static bool fl_rule_compile(EdnVal *entry, FlRule *out,
                             const char *mode_name, int rule_idx) {
    memset(out, 0, sizeof(*out));

    /* Form 1: {:words [...] :face :f} */
    if (entry->type == EDN_MAP) {
        EdnVal *words = edn_map_get(entry, "words");
        EdnVal *face  = edn_map_get(entry, "face");
        if (!words || !face) return false;
        char *pat = build_words_regex(words);
        if (!pat) return false;
        int rc = regcomp(&out->re, pat, REG_EXTENDED);
        if (rc != 0) {
            char msg[128]; regerror(rc, &out->re, msg, sizeof(msg));
            fprintf(stderr, "[font-lock] regex compile failed: %s rule %d: %s\n",
                    mode_name, rule_idx, msg);
            free(pat);
            return false;
        }
        free(pat);
        out->group_faces = hmalloc(sizeof(int));
        out->group_faces[0] = face->type == EDN_KEYWORD
                              ? (int)face_from_keyword(face->str) : TOK_NORMAL;
        out->n_groups  = 1;
        out->compiled  = true;
        return true;
    }

    /* Form 2: [pattern :face]  or  Form 3: [pattern [:f1 :f2 nil]] */
    if (entry->type == EDN_VECTOR && entry->vec.count >= 2) {
        EdnVal *pat_v  = entry->vec.items[0];
        EdnVal *face_v = entry->vec.items[1];
        if (!pat_v || pat_v->type != EDN_STRING) return false;

        int rc = regcomp(&out->re, pat_v->str, REG_EXTENDED);
        if (rc != 0) {
            char msg[128]; regerror(rc, &out->re, msg, sizeof(msg));
            fprintf(stderr, "[font-lock] regex compile failed: %s rule %d: %s\n",
                    mode_name, rule_idx, msg);
            return false;
        }
        out->compiled = true;

        if (face_v->type == EDN_KEYWORD) {
            out->group_faces = hmalloc(sizeof(int));
            out->group_faces[0] = (int)face_from_keyword(face_v->str);
            out->n_groups  = 1;
            out->subgroup_mode = false;
        } else if (face_v->type == EDN_VECTOR) {
            int n = face_v->vec.count;
            out->group_faces = hmalloc(sizeof(int) * (size_t)n);
            for (int i = 0; i < n; i++) {
                EdnVal *g = face_v->vec.items[i];
                if (g && g->type == EDN_KEYWORD)
                    out->group_faces[i] = (int)face_from_keyword(g->str);
                else
                    out->group_faces[i] = -1;  /* nil → skip */
            }
            out->n_groups = n;
            out->subgroup_mode = true;
        } else {
            out->group_faces = hmalloc(sizeof(int));
            out->group_faces[0] = TOK_NORMAL;
            out->n_groups  = 1;
            out->subgroup_mode = false;
        }
        return true;
    }
    return false;
}

static int scan_syntactic(const FlMode *m, const char *line, int len,
                          int state_in, int *state_out,
                          LineHighlight *hl, FlRegion *regions) {
    int n_regions = 0;
    int i = 0;
    int code_start = 0;
    int state = state_in;

    if (state & FL_STATE_BLOCK_COMMENT) {
        int start = 0;
        size_t bcl = m->block_close ? strlen(m->block_close) : 0;
        while (bcl > 0 && i + (int)bcl <= len) {
            if (strncmp(line + i, m->block_close, bcl) == 0) {
                int end = i + (int)bcl;
                add_span(hl, start, end - start, TOK_COMMENT);
                i = end;
                state &= ~FL_STATE_BLOCK_COMMENT;
                code_start = i;
                goto normal;
            }
            i++;
        }
        add_span(hl, start, len - start, TOK_COMMENT);
        *state_out = state;
        return 0;
    }

normal:
    while (i < len) {
        if (m->line_comment) {
            size_t lcl = strlen(m->line_comment);
            if (i + (int)lcl <= len &&
                strncmp(line + i, m->line_comment, lcl) == 0) {
                if (i > code_start && n_regions < FL_MAX_REGIONS) {
                    regions[n_regions].start = code_start;
                    regions[n_regions].end   = i;
                    n_regions++;
                }
                add_span(hl, i, len - i, TOK_COMMENT);
                *state_out = state;
                return n_regions;
            }
        }

        if (m->block_open) {
            size_t bol = strlen(m->block_open);
            if (i + (int)bol <= len &&
                strncmp(line + i, m->block_open, bol) == 0) {
                if (i > code_start && n_regions < FL_MAX_REGIONS) {
                    regions[n_regions].start = code_start;
                    regions[n_regions].end   = i;
                    n_regions++;
                }
                int cstart = i;
                i += (int)bol;
                size_t bcl = m->block_close ? strlen(m->block_close) : 0;
                bool closed = false;
                while (bcl > 0 && i + (int)bcl <= len) {
                    if (strncmp(line + i, m->block_close, bcl) == 0) {
                        i += (int)bcl;
                        add_span(hl, cstart, i - cstart, TOK_COMMENT);
                        code_start = i;
                        closed = true;
                        break;
                    }
                    i++;
                }
                if (!closed) {
                    add_span(hl, cstart, len - cstart, TOK_COMMENT);
                    state |= FL_STATE_BLOCK_COMMENT;
                    *state_out = state;
                    return n_regions;
                }
                continue;
            }
        }

        bool is_sdelim = false;
        for (int d = 0; d < m->n_string_delims; d++) {
            if (line[i] == m->string_delims[d]) { is_sdelim = true; break; }
        }
        if (is_sdelim) {
            if (i > code_start && n_regions < FL_MAX_REGIONS) {
                regions[n_regions].start = code_start;
                regions[n_regions].end   = i;
                n_regions++;
            }
            char delim = line[i];
            int sstart = i;
            i++;
            while (i < len && line[i] != delim) {
                if (m->string_escape && line[i] == m->string_escape && i + 1 < len) i++;
                i++;
            }
            if (i < len) i++;
            add_span(hl, sstart, i - sstart, TOK_STRING);
            code_start = i;
            continue;
        }

        if (m->char_delim && line[i] == m->char_delim) {
            if (i > code_start && n_regions < FL_MAX_REGIONS) {
                regions[n_regions].start = code_start;
                regions[n_regions].end   = i;
                n_regions++;
            }
            int cstart = i;
            i++;
            if (i < len && line[i] == m->string_escape) i++;
            if (i < len) i++;
            if (i < len && line[i] == m->char_delim) i++;
            add_span(hl, cstart, i - cstart, TOK_STRING);
            code_start = i;
            continue;
        }

        i++;
    }

    if (len > code_start && n_regions < FL_MAX_REGIONS) {
        regions[n_regions].start = code_start;
        regions[n_regions].end   = len;
        n_regions++;
    }
    *state_out = state;
    return n_regions;
}

/* Fallback highlighter for Help-mode buffers (find-doc / describe-key output).
 * The layout-aware logic doesn't fit the regex + syntax-table model, so it lives
 * here as a hand-rolled function that is invoked via the :builtin-help sentinel. */
static LineHighlight highlight_help_fallback(const char *line, int len, int state_in) {
    LineHighlight hl = {.count = 0, .state = state_in};
    if (len == 0) return hl;

    /* Separator lines (find-doc output: "-------------------------") */
    if (len >= 3 && line[0] == '-') {
        bool is_sep = true;
        for (int i = 0; i < len; i++) {
            if (line[i] != '-') { is_sep = false; break; }
        }
        if (is_sep) {
            add_span(&hl, 0, len, TOK_COMMENT);
            return hl;
        }
    }

    /* Section headers: lines ending with ":" that don't start with space */
    if (line[0] != ' ' && len > 1 && line[len - 1] == ':') {
        /* Highlight the whole line as heading, but pick out quoted strings */
        int i = 0;
        while (i < len) {
            if (line[i] == '"') {
                int start = i;
                i++;
                while (i < len && line[i] != '"') i++;
                if (i < len) i++; /* skip closing quote */
                add_span(&hl, start, i - start, TOK_STRING);
            } else {
                int start = i;
                while (i < len && line[i] != '"') i++;
                add_span(&hl, start, i - start, TOK_HEADING2);
            }
        }
        return hl;
    }

    /* Namespace-qualified Clojure names (e.g., "clojure.core/map") on non-indented lines */
    if (line[0] != ' ') {
        /* Check for ns/name pattern: word chars, dots, slashes */
        bool has_slash = false;
        bool has_dot = false;
        int end = 0;
        while (end < len && (isalnum(line[end]) || line[end] == '.' ||
               line[end] == '/' || line[end] == '-' || line[end] == '_' ||
               line[end] == '?' || line[end] == '!' || line[end] == '*'))
            end++;
        for (int j = 0; j < end; j++) {
            if (line[j] == '/') has_slash = true;
            if (line[j] == '.') has_dot = true;
        }
        if (has_slash && has_dot && end > 0) {
            add_span(&hl, 0, end, TOK_FUNCTION);
            return hl;
        }
    }

    /* Command table rows: "  command-name      C-x k    docstring" */
    if (len > 4 && line[0] == ' ' && line[1] == ' ' && line[2] != ' ') {
        /* Command name: from col 2 to first run of 2+ spaces */
        int name_start = 2;
        int i = name_start;
        while (i < len && !(line[i] == ' ' && i + 1 < len && line[i + 1] == ' '))
            i++;
        int name_end = i;
        add_span(&hl, name_start, name_end - name_start, TOK_FUNCTION);

        /* Skip whitespace to find keybinding */
        while (i < len && line[i] == ' ') i++;
        if (i < len) {
            int bind_start = i;
            /* Keybinding: next token before another run of 2+ spaces */
            while (i < len && !(line[i] == ' ' && i + 1 < len && line[i + 1] == ' '))
                i++;
            int bind_end = i;
            /* Only color as keybinding if it looks like one (contains C- or M- or is short) */
            if (bind_end - bind_start > 0 && bind_end - bind_start < 20) {
                add_span(&hl, bind_start, bind_end - bind_start, TOK_KEYWORD);
            }
        }
        return hl;
    }

    /* Indented text from find-doc */
    if (len > 2 && line[0] == ' ' && line[1] == ' ') {
        int i = 2;
        while (i < len && line[i] == ' ') i++;

        /* Lines starting with ( — delegate to Clojure highlighter */
        if (i < len && line[i] == '(') {
            LineHighlight clj = font_lock_highlight_by_lang(LANG_CLOJURE, line + i, len - i, 0);
            for (int s = 0; s < clj.count; s++) {
                add_span(&hl, clj.spans[s].start + i, clj.spans[s].length, clj.spans[s].type);
            }
            return hl;
        }

        /* Docstring body: highlight :keywords inline */
        while (i < len) {
            if (line[i] == ':' && i + 1 < len && isalpha(line[i + 1])) {
                int start = i;
                i++;
                while (i < len && (isalnum(line[i]) || line[i] == '-' ||
                       line[i] == '_' || line[i] == '/' || line[i] == '?'))
                    i++;
                add_span(&hl, start, i - start, TOK_TYPE);
            } else {
                i++;
            }
        }
        return hl;
    }

    /* "key-desc runs the command cmd-name" from describe-key */
    if (len > 5) {
        const char *runs = strstr(line, " runs the command ");
        if (runs) {
            int key_end = (int)(runs - line);
            add_span(&hl, 0, key_end, TOK_KEYWORD);
            int cmd_start = key_end + 18; /* length of " runs the command " */
            if (cmd_start < len) {
                add_span(&hl, cmd_start, len - cmd_start, TOK_FUNCTION);
            }
            return hl;
        }
    }

    /* "  Key bindings: ..." or "  Implemented in: ..." or "  Dispatch: ..." labels */
    if (len > 4 && line[0] == ' ' && line[1] == ' ') {
        const char *colon = strchr(line + 2, ':');
        if (colon && colon - line < 20) {
            int label_end = (int)(colon - line) + 1;
            add_span(&hl, 2, label_end - 2, TOK_HEADING3);
            /* Value after the colon */
            int val_start = label_end;
            while (val_start < len && line[val_start] == ' ') val_start++;
            if (val_start < len) {
                add_span(&hl, val_start, len - val_start, TOK_KEYWORD);
            }
            return hl;
        }
    }

    return hl;
}

/* Run every rule against [rstart, rend) from current offset; shortest-start-position
 * wins across rules, ties broken by rule order. Emits spans for whole-match (single
 * face) or per-subgroup (multi face vector). */
static void run_keyword_pass(const FlMode *m, const char *line, int len,
                             int rstart, int rend, LineHighlight *hl) {
    (void)len;
    int off = rstart;
    while (off < rend) {
        int best_rule  = -1;
        int best_start = rend;
        regmatch_t best_m[8] = {{0}};

        for (int r = 0; r < m->n_rules; r++) {
            if (!m->rules[r].compiled) continue;
            regmatch_t mt[8] = {{0}};
            int flags = (off > 0) ? REG_NOTBOL : 0;
            if (regexec(&m->rules[r].re, line + off, 8, mt, flags) != 0) continue;
            int mstart = (int)mt[0].rm_so + off;
            int mend   = (int)mt[0].rm_eo + off;
            if (mend > rend) continue;
            if (mstart < best_start) {
                best_rule  = r;
                best_start = mstart;
                memcpy(best_m, mt, sizeof(best_m));
            }
        }

        if (best_rule < 0) return;

        const FlRule *R = &m->rules[best_rule];
        if (!R->subgroup_mode && R->n_groups == 1 && R->group_faces[0] >= 0) {
            int sstart = (int)best_m[0].rm_so + off;
            int slen   = (int)(best_m[0].rm_eo - best_m[0].rm_so);
            add_span(hl, sstart, slen, (TokenType)R->group_faces[0]);
        } else {
            for (int i = 0; i < R->n_groups; i++) {
                int gi = i + 1;
                if (gi >= 8) break;
                if (R->group_faces[i] < 0) continue;
                if (best_m[gi].rm_so < 0) continue;
                int sstart = (int)best_m[gi].rm_so + off;
                int slen   = (int)(best_m[gi].rm_eo - best_m[gi].rm_so);
                add_span(hl, sstart, slen, (TokenType)R->group_faces[i]);
            }
        }

        int advance = (int)best_m[0].rm_eo + off;
        if (advance <= off) advance = off + 1;
        off = advance;
    }
}

void font_lock_reload(void) {
#ifndef FONT_LOCK_TEST_BUILD
    if (!sci_is_ready()) return;
    /* sci_eval returns the EDN representation directly (matching the pattern
     * used by keybindings_load_edn / modes_load_edn). Wrapping in pr-str would
     * double-quote the result and break the parser. */
    char *edn = sci_eval("(hammock.syntax-modes/export)");
    if (!edn) return;
    char *start = strchr(edn, '[');
    if (start) font_lock_load_from_edn_string(start);
    free(edn);
#endif
}

int font_lock_load_from_edn_string(const char *edn_text) {
    if (!edn_text) return -1;

    size_t consumed = 0;
    EdnVal *root = edn_parse(edn_text, strlen(edn_text), &consumed);
    if (!root || root->type != EDN_VECTOR) {
        edn_free(root);
        return -1;
    }

    for (int i = 0; i < g_mode_count; i++) { fl_mode_free(g_modes[i]); g_modes[i] = NULL; }
    g_mode_count = 0;

    for (int i = 0; i < root->vec.count && g_mode_count < FL_MAX_MODES; i++) {
        FlMode *m = fl_mode_from_edn(root->vec.items[i]);
        if (m) g_modes[g_mode_count++] = m;
    }
    edn_free(root);
    return 0;
}

void font_lock_shutdown(void) {
    for (int i = 0; i < g_mode_count; i++) { fl_mode_free(g_modes[i]); g_modes[i] = NULL; }
    g_mode_count = 0;
}

LineHighlight font_lock_highlight_named(const char *mode_name,
                                        const char *line, int len,
                                        int state_in) {
    LineHighlight hl = { .count = 0, .state = state_in };
    FlMode *m = mode_find_by_name(mode_name);
    if (!m) return hl;

    /* Help mode: layout-aware, doesn't use the regex engine. */
    if (m->use_legacy_help) return highlight_help_fallback(line, len, state_in);

    /* Inside a fenced code block: check for close, else delegate to inner mode. */
    if ((state_in & FL_STATE_CODE_FENCE) && m->fence_close) {
        regmatch_t mt[4];
        if (regexec(m->fence_close, line, 4, mt, 0) == 0) {
            add_span(&hl, 0, len, TOK_CODE);
            hl.state = state_in & ~(FL_STATE_CODE_FENCE | FL_FENCE_LANG_MASK);
            return hl;
        }
        int idx = (state_in & FL_FENCE_LANG_MASK) >> FL_FENCE_LANG_SHIFT;
        const char *inner = fl_lang_to_name((SyntaxLang)idx);
        if (inner && strcmp(inner, mode_name) != 0) {
            LineHighlight inner_hl = font_lock_highlight_named(inner, line, len, 0);
            for (int i = 0; i < inner_hl.count; i++)
                add_span(&hl, inner_hl.spans[i].start,
                              inner_hl.spans[i].length,
                              inner_hl.spans[i].type);
        } else {
            add_span(&hl, 0, len, TOK_CODE);
        }
        hl.state = state_in;
        return hl;
    }

    /* Opening fence detection */
    if (m->fence_open) {
        regmatch_t mt[4];
        if (regexec(m->fence_open, line, 4, mt, 0) == 0) {
            add_span(&hl, 0, len, TOK_CODE);
            int tag_idx = m->fence_lang_group;
            int inner_idx = 0;
            if (tag_idx >= 0 && tag_idx < 4 && mt[tag_idx].rm_so >= 0) {
                int tstart = (int)mt[tag_idx].rm_so;
                int tend   = (int)mt[tag_idx].rm_eo;
                int tlen   = tend - tstart;
                for (int i = 0; i < m->n_fence_langs; i++) {
                    if ((int)strlen(m->fence_langs[i].tag) == tlen &&
                        strncmp(line + tstart, m->fence_langs[i].tag, (size_t)tlen) == 0) {
                        inner_idx = lang_index_for_mode_name(m->fence_langs[i].mode_name);
                        break;
                    }
                }
            }
            hl.state = FL_STATE_CODE_FENCE | (inner_idx << FL_FENCE_LANG_SHIFT);
            return hl;
        }
    }

    /* Standard two-pass: syntactic then keyword. */
    int state_out = state_in;
    FlRegion regions[FL_MAX_REGIONS];
    int n_regions = scan_syntactic(m, line, len, state_in, &state_out, &hl, regions);

    for (int r = 0; r < n_regions; r++)
        run_keyword_pass(m, line, len, regions[r].start, regions[r].end, &hl);

    /* Sort spans by start for downstream consumers. */
    for (int i = 1; i < hl.count; i++) {
        SyntaxSpan s = hl.spans[i];
        int j = i - 1;
        while (j >= 0 && hl.spans[j].start > s.start) {
            hl.spans[j + 1] = hl.spans[j];
            j--;
        }
        hl.spans[j + 1] = s;
    }

    hl.state = state_out;
    return hl;
}

/* Legacy SyntaxLang enum → mode name. Used by the transition path and later
 * by the replacement syntax_highlight_line once Task 11 deletes the old C
 * tokenizers. */
static const char *fl_lang_to_name(SyntaxLang l) {
    switch (l) {
    case LANG_C:        return "C";
    case LANG_CLOJURE:  return "Clojure";
    case LANG_BASH:     return "Bash";
    case LANG_MARKDOWN: return "Markdown";
    case LANG_DIFF:     return "Diff";
    case LANG_HELP:     return "Help";
    case LANG_MAKEFILE: return "Makefile";
    default:            return NULL;
    }
}

/* Inverse of fl_lang_to_name: mode-name string → SyntaxLang enum index. Used
 * to pack the inner language for a fenced block into the 3-bit field in the
 * line state. Unknown names map to 0 (LANG_NONE). */
static int lang_index_for_mode_name(const char *name) {
    if (!name) return 0;
    if (!strcmp(name, "C"))        return LANG_C;
    if (!strcmp(name, "Clojure"))  return LANG_CLOJURE;
    if (!strcmp(name, "Bash"))     return LANG_BASH;
    if (!strcmp(name, "Markdown")) return LANG_MARKDOWN;
    if (!strcmp(name, "Diff"))     return LANG_DIFF;
    if (!strcmp(name, "Help"))     return LANG_HELP;
    if (!strcmp(name, "Makefile")) return LANG_MAKEFILE;
    return 0;
}

/* Public: highlight a line by SyntaxLang enum. Not called by display.c yet
 * (the old syntax.c still owns syntax_highlight_line); Task 11 will flip
 * display.c over once the legacy tokenizers are deleted. */
LineHighlight font_lock_highlight_by_lang(SyntaxLang lang, const char *line,
                                          int len, int state_in) {
    const char *name = fl_lang_to_name(lang);
    if (!name) {
        LineHighlight hl = { .count = 0, .state = state_in };
        return hl;
    }
    return font_lock_highlight_named(name, line, len, state_in);
}

/* Public dispatch entry. Replaces the legacy implementation that lived in
 * src/syntax.c. Routes through font_lock_highlight_named via the
 * SyntaxLang → mode-name bridge. */
LineHighlight syntax_highlight_line(SyntaxLang lang, const char *line,
                                    int len, int state_in) {
    return font_lock_highlight_by_lang(lang, line, len, state_in);
}

/* Public detect. Copied verbatim from the original src/syntax.c, unchanged. */
SyntaxLang syntax_detect(const char *filename) {
    if (!filename) return LANG_NONE;

    if (str_ends_with(filename, ".c") || str_ends_with(filename, ".h") ||
        str_ends_with(filename, ".cc") || str_ends_with(filename, ".cpp") ||
        str_ends_with(filename, ".hpp"))
        return LANG_C;

    if (str_ends_with(filename, ".clj") || str_ends_with(filename, ".cljs") ||
        str_ends_with(filename, ".cljc") || str_ends_with(filename, ".edn") ||
        str_ends_with(filename, ".bb"))
        return LANG_CLOJURE;

    if (str_ends_with(filename, ".sh") || str_ends_with(filename, ".bash") ||
        str_ends_with(filename, ".zsh"))
        return LANG_BASH;

    if (str_ends_with(filename, ".md") || str_ends_with(filename, ".markdown"))
        return LANG_MARKDOWN;

    return LANG_NONE;
}
