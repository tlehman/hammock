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
