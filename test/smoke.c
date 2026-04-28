/* Non-interactive smoke test for the Clojure layer.
 *
 * Links against libsci.dylib and drives the same load sequence as main.c,
 * then evaluates a handful of probes that would have caught historical bugs
 * (SCI load-time errors, missing commands, Java-interop misfires, keybinding
 * export shape). Exits non-zero if any probe fails.
 *
 * Build and run via:   make check
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graal_isolate.h"
#include "libsci.h"

static graal_isolatethread_t *thread = NULL;
static int failures = 0;

static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)len, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}

static void load_file(const char *path) {
    char *content = read_file(path);
    if (!content) {
        printf("  FAIL load %s (cannot read file)\n", path);
        failures++;
        return;
    }
    char *r = libsci_eval_string(thread, content);
    free(content);
    if (!r) {
        printf("  FAIL load %s (libsci_eval_string returned NULL)\n", path);
        failures++;
        return;
    }
    printf("  PASS load %s\n", path);
}

static void expect_true(const char *label, const char *expr) {
    char *r = libsci_eval_string(thread, (char *)expr);
    int ok = r && strcmp(r, "true") == 0;
    if (ok) {
        printf("  PASS %s\n", label);
    } else {
        printf("  FAIL %s\n    expr:   %s\n    result: %s\n",
               label, expr, r ? r : "(null)");
        failures++;
    }
}

int main(void) {
    graal_isolate_t *isolate = NULL;
    if (graal_create_isolate(NULL, &isolate, &thread) != 0) {
        fprintf(stderr, "graal_create_isolate failed\n");
        return 1;
    }
    if (libsci_init(thread) != 0) {
        fprintf(stderr, "libsci_init failed\n");
        return 1;
    }

    puts("Loading Clojure namespaces via clj/loadup.clj manifest...");
    load_file("clj/loadup.clj");

    /* Read the file list back from the manifest and load each entry. */
    char *count_raw = libsci_eval_string(thread, "(count hammock.loadup/files)");
    int count = count_raw ? atoi(count_raw) : 0;
    for (int i = 0; i < count; i++) {
        char expr[64];
        snprintf(expr, sizeof(expr), "(nth hammock.loadup/files %d)", i);
        char *path_raw = libsci_eval_string(thread, expr);
        if (!path_raw) continue;
        /* SCI serializes string values with surrounding quotes. Strip them. */
        size_t plen = strlen(path_raw);
        if (plen >= 2 && path_raw[0] == '"' && path_raw[plen - 1] == '"') {
            path_raw[plen - 1] = '\0';
            load_file(path_raw + 1);
        } else {
            load_file(path_raw);
        }
    }

    puts("\nlatex2unicode library...");
    expect_true("latex2unicode namespace loaded",
                "(some? (find-ns 'latex2unicode))");
    expect_true("(latex2unicode \"\\\\alpha\") = \"α\"",
                "(= \"α\" (latex2unicode/latex2unicode \"\\\\alpha\"))");
    expect_true("(latex2unicode \"\\\\mathcal{H}\") = \"ℋ\"",
                "(= \"ℋ\" (latex2unicode/latex2unicode \"\\\\mathcal{H}\"))");
    expect_true("(latex2unicode \"a^{12}\") expands braced superscript",
                "(= \"a¹²\" (latex2unicode/latex2unicode \"a^{12}\"))");

    puts("\nSymbol index probes...");
    expect_true("symbols/ensure! returns a map",
                "(map? (hammock.symbols/ensure!))");
    expect_true("namespaces index is non-empty",
                "(pos? (count (:namespaces (hammock.symbols/ensure!))))");
    expect_true("modules index is non-empty",
                "(pos? (count (:modules (hammock.symbols/ensure!))))");
    expect_true("commands index is non-empty",
                "(pos? (count (:commands (hammock.symbols/ensure!))))");

    puts("\nLine parsers...");
    expect_true("namespace-at-line round trip",
                "(= \"hammock.commands\" (hammock.symbols/namespace-at-line \"  hammock.commands             (84)\"))");
    expect_true("name-at-line round trip",
                "(= \"forward-char\" (hammock.symbols/name-at-line \"  [cmd ] forward-char                 — Move point forward one character.\"))");
    expect_true("apropos-name-at-line round trip",
                "(= \"point-to-line-end\" (hammock.symbols/apropos-name-at-line \"  [defn] hammock.effects          /point-to-line-end        — (no docstring)\"))");

    puts("\nCommand registration...");
    expect_true("browse-symbols registered",
                "(contains? @hammock.state/*commands* \"browse-symbols\")");
    expect_true("apropos registered",
                "(contains? @hammock.state/*commands* \"apropos\")");
    expect_true("symbrowse-visit registered",
                "(contains? @hammock.state/*commands* \"symbrowse-visit\")");
    expect_true("browse-symbols dispatch returns a vector",
                "(vector? (hammock.commands/dispatch \"browse-symbols\"))");

    puts("\nKeybinding export shape...");
    expect_true("C-h prefix entry exported",
                "(boolean (some #(and (= \"prefix\" (first %)) (= \"ch\" (last %))) (hammock.keybindings/export)))");
    expect_true("C-h s binding exported",
                "(boolean (some #(and (= \"ch\" (first %)) (= \"browse-symbols\" (last %))) (hammock.keybindings/export)))");
    expect_true("F1 s binding exported",
                "(boolean (some #(and (= \"f1\" (first %)) (= \"browse-symbols\" (last %))) (hammock.keybindings/export)))");
    expect_true("C-h e binding exported",
                "(boolean (some #(and (= \"ch\" (first %)) (= \"view-messages\" (last %))) (hammock.keybindings/export)))");

    puts("\nfont-lock export shape...");
    expect_true("syntax-modes namespace loaded",
                "(some? (find-ns 'hammock.syntax-modes))");
    expect_true("syntax-modes/export returns a vector",
                "(vector? (hammock.syntax-modes/export))");
    expect_true("export has at least 6 modes",
                "(>= (count (hammock.syntax-modes/export)) 6)");
    expect_true("C mode exported with :syntax-table and :font-lock-keywords",
                "(boolean (let [c (first (filter #(= \"C\" (:name %)) (hammock.syntax-modes/export)))] "
                "(and c (map? (:syntax-table (:syntax c))) (vector? (:font-lock-keywords (:syntax c))))))");
    expect_true("Markdown mode carries :fence",
                "(boolean (some #(and (= \"Markdown\" (:name %)) (:fence (:syntax %))) (hammock.syntax-modes/export)))");
    expect_true("Help mode uses :builtin-help engine",
                "(boolean (some #(and (= \"Help\" (:name %)) (= :builtin-help (:engine (:syntax %)))) (hammock.syntax-modes/export)))");

    puts("\nKill ring / yank-pop...");
    expect_true("yank-pop registered",
                "(contains? @hammock.state/*commands* \"yank-pop\")");
    expect_true("backward-kill-word registered",
                "(contains? @hammock.state/*commands* \"backward-kill-word\")");
    expect_true("yank-pop dispatch returns [[:yank-pop]]",
                "(= [[:yank-pop]] (hammock.commands/dispatch \"yank-pop\"))");
    expect_true("backward-kill-word dispatch builds set-mark/backward-word/kill-region",
                "(= [[:set-mark] [:point-backward-word] [:kill-region]] (hammock.commands/dispatch \"backward-kill-word\"))");
    /* M-Backspace = HK_BACKSPACE (0x1001 = 4097) with MOD_META (2). */
    expect_true("M-Backspace bound to backward-kill-word",
                "(boolean (some #(and (= \"global\" (first %)) (= 4097 (nth % 1)) (= 2 (nth % 2)) (= \"backward-kill-word\" (last %))) (hammock.keybindings/export)))");
    /* M-y = 121 with MOD_META (2). */
    expect_true("M-y bound to yank-pop",
                "(boolean (some #(and (= \"global\" (first %)) (= 121 (nth % 1)) (= 2 (nth % 2)) (= \"yank-pop\" (last %))) (hammock.keybindings/export)))");

    puts("\nMarkdown table alignment...");
    expect_true("table-row? detects pipe row",
                "(hammock.markdown/table-row? \"| A | B |\")");
    expect_true("table-row? rejects plain text",
                "(not (hammock.markdown/table-row? \"hello world\"))");
    expect_true("separator-row? detects |---|---|",
                "(hammock.markdown/separator-row? \"|---|---|\")");
    expect_true("separator-row? honors alignment colons",
                "(hammock.markdown/separator-row? \"| :--- | ---: | :---: |\")");
    expect_true("find-table-bounds returns the contiguous block",
                "(= [0 2] (hammock.markdown/find-table-bounds "
                "  [\"| A | B |\" \"|---|---|\" \"| 1 | 2 |\" \"\"] 1))");
    expect_true("align-table-at pads cells to equal widths",
                "(let [t \"| A | BB |\\n|---|---|\\n| 1 | 2 |\\n\" "
                "      r (hammock.markdown/align-table-at t 1 0)] "
                "  (and (some? r) "
                "       (clojure.string/includes? (:new-text r) \"| A | BB |\") "
                "       (clojure.string/includes? (:new-text r) \"| 1 | 2  |\")))");
    expect_true("align-table-at returns nil off a table",
                "(nil? (hammock.markdown/align-table-at \"hello\\nworld\" 1 0))");
    /* Stub row recognition: a half-typed row starting with `|` is still a
     * table row and gets scaffolded with empty cells. */
    expect_true("table-row? recognizes a stub `|` row",
                "(hammock.markdown/table-row? \"|\")");
    expect_true("align-table-at scaffolds an empty row from a stub `|`",
                "(let [t \"| name | age |\\n|------|-----|\\n|\" "
                "      r (hammock.markdown/align-table-at t 3 1)] "
                "  (and (some? r) "
                "       (clojure.string/includes? (:new-text r) \"|      |     |\")))");
    expect_true("align-table-at on stub row places point in cell 0",
                "(let [t \"| name | age |\\n|------|-----|\\n|\" "
                "      r (hammock.markdown/align-table-at t 3 1)] "
                "  (= 32 (:new-point r)))");
    /* Cell navigation: cursor in cell 0 advances to cell 1. */
    expect_true("align-table-at advances point to next cell",
                "(let [t \"| foo | bar |\\n|-----|-----|\\n| 1 | 2 |\\n\" "
                "      r (hammock.markdown/align-table-at t 1 3)] "
                "  (= 8 (:new-point r)))");
    /* Wrap: cursor in last cell of last row appends a new empty row. */
    expect_true("align-table-at appends a new row when wrapping past last cell",
                "(let [t \"| a | b |\\n|---|---|\\n| 1 | 2 |\" "
                "      r (hammock.markdown/align-table-at t 3 9)] "
                "  (and (some? r) "
                "       (= 4 (count (clojure.string/split-lines (:new-text r)))) "
                "       (clojure.string/ends-with? (:new-text r) \"|   |   |\")))");
    /* Backward navigation: cell 1 → cell 0. */
    expect_true("align-table-at :backward moves point one cell left",
                "(let [t \"| foo | bar |\\n|-----|-----|\\n| 1 | 2 |\\n\" "
                "      r (hammock.markdown/align-table-at t 1 9 :backward)] "
                "  (= 2 (:new-point r)))");
    /* Backward wrap: cell 0 of row 2 → last cell of row 0 (skipping separator). */
    expect_true("align-table-at :backward wraps past first cell to previous data row's last cell",
                "(let [t \"| a | b |\\n|---|---|\\n| 1 | 2 |\\n\" "
                "      r (hammock.markdown/align-table-at t 3 2 :backward)] "
                "  (= 6 (:new-point r)))");
    expect_true("markdown-prev-cell registered",
                "(contains? @hammock.state/*commands* \"markdown-prev-cell\")");
    expect_true("markdown-shift-tab registered",
                "(contains? @hammock.state/*commands* \"markdown-shift-tab\")");
    /* Shift-Tab in markdown mode now dispatches to markdown-shift-tab. */
    expect_true("markdown Shift-Tab binding points at markdown-shift-tab",
                "(boolean (some #(and (= \"mode:markdown\" (first %)) "
                "                     (= 4109 (nth % 1)) "    /* HK_SHIFT_TAB = 0x100D */
                "                     (= \"markdown-shift-tab\" (last %))) "
                "               (hammock.keybindings/export)))");
    expect_true("markdown-align-table registered",
                "(contains? @hammock.state/*commands* \"markdown-align-table\")");
    expect_true("markdown-tab registered",
                "(contains? @hammock.state/*commands* \"markdown-tab\")");
    /* Tab in markdown mode now dispatches to markdown-tab, not -next-link. */
    expect_true("markdown Tab binding points at markdown-tab",
                "(boolean (some #(and (= \"mode:markdown\" (first %)) "
                "                     (= 4107 (nth % 1)) "    /* HK_TAB = 0x100B */
                "                     (= \"markdown-tab\" (last %))) "
                "               (hammock.keybindings/export)))");

    puts("\n*Messages* plumbing...");
    expect_true("view-messages registered",
                "(contains? @hammock.state/*commands* \"view-messages\")");
    expect_true("clear-messages registered",
                "(contains? @hammock.state/*commands* \"clear-messages\")");
    expect_true("view-messages dispatch emits buffer-switch",
                "(= :buffer-switch (first (first (hammock.commands/dispatch \"view-messages\"))))");
    expect_true("dispatch catches throwing command",
                "(do (swap! hammock.state/*commands* assoc \"__smoke-throw__\" {:fn (fn [] (throw (ex-info \"boom\" {})))}) (let [r (hammock.commands/dispatch \"__smoke-throw__\")] (and (vector? r) (= :message (first (first r))))))");

    libsci_shutdown(thread);
    graal_tear_down_isolate(thread);

    printf("\n%d failure(s)\n", failures);
    return failures > 0 ? 1 : 0;
}
