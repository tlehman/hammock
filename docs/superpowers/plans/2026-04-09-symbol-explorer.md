# Symbol Explorer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a browsable namespace/symbol explorer (`browse-symbols`, `C-h s`) and a flat apropos search (`apropos`, `C-h a`) to Hammock, targeting version 0.1.2.

**Architecture:** Almost all logic lives in Clojure. A new `clj/symbols.clj` builds and caches an index over `clj/*.clj`, `src/*.{c,h}`, and the live commands table. Two new commands in `clj/commands.clj` open two-pane and flat-list UIs using existing effects. One new C effect, `[:point-to-line N]`, is added so `symbrowse-visit` can jump to a specific line in a freshly loaded file. Three new major modes (`Symbol-Browser`, `Symbol-Detail`, `Apropos`) are added by extending the `MajorModeID` enum and registering them on both sides.

**Source root resolution:** Indexing is gated by a runtime check. The `source-root` helper resolves in this order: (1) `$HAMMOCK_SOURCE` env var if set, (2) `.` if `./clj/state.clj` exists (dev mode), (3) unresolved. When unresolved, Clojure-namespace and C-module indexing skip their filesystem walks and return empty maps. The commands index still populates from the live `@*commands*` atom but without `:file`/`:line`. A follow-up feature will populate a source root for shipped binaries (embed-and-extract, clone-on-first-use, or similar); it is out of scope for this plan.

**Tech Stack:** C11 (ncurses, gap buffer), Clojure via SCI (GraalVM native-image shared library), GNU make, no existing test framework. Shell interop from Clojure via `hammock.shell/exec` (takes a vector of argv, returns `{:out :err :exit}`, no shell expansion).

**Spec:** [`docs/superpowers/specs/2026-04-09-symbol-explorer-design.md`](../specs/2026-04-09-symbol-explorer-design.md)

**Note on testing:** This project has no test suite. Verification in each task uses one of: (a) `make` to confirm the build still works for C changes, (b) `./hammock -e '<expr>'` or similar runtime checks where applicable (the binary reads EDN on stdin via nothing like that today — see Task 3 for the actual scratch-buffer verification recipe), (c) a final manual smoke-test task. This is unavoidable given the project state; do not introduce a new test framework as part of this plan.

**File structure:**

| Path | Action | Responsibility |
|---|---|---|
| `clj/symbols.clj` | create | Symbol index, cached atom, builders for Clojure/C/commands, renderers |
| `clj/commands.clj` | modify | Add `browse-symbols`, `apropos`, and 6 in-buffer commands |
| `clj/keybindings.clj` | modify | Add bindings for `C-h s`, `C-h a`, `F1 s`, and three new mode keymaps |
| `clj/modes.clj` | modify | Add three new mode entries |
| `clj/core.clj` | modify | Bump version string |
| `src/effects.c` | modify | Add `[:point-to-line N]` effect |
| `src/mode.h` | modify | Extend `MajorModeID` enum with three new values |
| `NEWS.md` | modify | Add 0.1.2 release notes |
| `README.md` | modify | Cross off the TODO line |

---

## Task 1: Add `[:point-to-line N]` C effect

**Files:**
- Modify: `src/effects.c`

**Why first:** Everything else downstream depends on being able to jump to a line in a freshly loaded file. This is the smallest, most isolated change, so land it first to shake out any build issues.

- [ ] **Step 1: Read the context around the existing `point-set` handler**

Open `src/effects.c` and locate `else if (strcmp(op, "point-set") == 0)` near line 219. The new effect handler will sit immediately after the `point-to-buffer-end` handler (around line 246), alongside the other absolute-position operations.

- [ ] **Step 2: Add the new handler**

In `src/effects.c`, after the `point-to-buffer-end` block (which ends with `buf->point = buffer_length(buf);`), insert:

```c
    else if (strcmp(op, "point-to-line") == 0) {
        long long n = edn_int_val(effect->vec.items[1], 1);
        if (n < 1) n = 1;
        buf->point = 0;
        for (long long i = 1; i < n; i++) {
            size_t next = buffer_next_line_start(buf, buf->point);
            if (next == buf->point) break;
            buf->point = next;
        }
        win->target_col = -1;
    }
```

The `win->target_col = -1` matches what `reset-target-col` does and keeps subsequent vertical motion sensible.

- [ ] **Step 3: Build**

```
make
```

Expected: build succeeds, `./hammock` exists and is newer than before. No warnings introduced by this change.

- [ ] **Step 4: Smoke test the effect**

Launch `./hammock clj/commands.clj`, then in the `*scratch*` buffer type and evaluate with `C-j`:

```
(do [[:buffer-switch "commands.clj"] [:point-to-line 100]])
```

Switch to `commands.clj` (`C-x b commands.clj Enter`). Point should be at the start of line 100. If it is, the effect works.

- [ ] **Step 5: Commit**

```
git add src/effects.c
git commit -m "Add [:point-to-line N] effect for absolute line jumps"
```

---

## Task 2: Extend `MajorModeID` enum with the three new modes

**Files:**
- Modify: `src/mode.h`

**Why:** `modes_load_edn` in `src/mode.c` rejects any mode id `>= MODE_COUNT`. Without extending the enum the new Clojure-side mode definitions will be silently dropped.

- [ ] **Step 1: Edit the enum**

In `src/mode.h`, find the `MajorModeID` enum (around line 9):

```c
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
```

Replace with:

```c
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
    MODE_SYMBOL_BROWSER,
    MODE_SYMBOL_DETAIL,
    MODE_APROPOS,
    MODE_COUNT,
} MajorModeID;
```

- [ ] **Step 2: Build**

```
make
```

Expected: build succeeds. No new warnings. The static `major_modes[MODE_COUNT]` array in `src/mode.c` automatically grows with the new `MODE_COUNT` value (= 14).

- [ ] **Step 3: Smoke test**

Launch `./hammock` and confirm it starts without errors. The three new slots will be zero-initialized until Clojure registers them in Task 9, which is fine for now (nothing switches to them yet).

- [ ] **Step 4: Commit**

```
git add src/mode.h
git commit -m "Extend MajorModeID enum with symbol-browser, symbol-detail, apropos"
```

---

## Task 3: Create `clj/symbols.clj` skeleton with cached atom and `ensure!`/`rebuild!`

**Files:**
- Create: `clj/symbols.clj`

**Why:** Gives later tasks a namespace to add to incrementally. Starts with the cache plumbing and empty builders so `ensure!` returns a valid shape from day one.

- [ ] **Step 1: Create the file with minimal content**

Create `clj/symbols.clj`:

```clojure
(ns hammock.symbols
  (:require [clojure.string :as str]
            [hammock.shell :as shell]
            [hammock.state :as state]))

;; Cached index. Shape:
;;   {:namespaces {ns-name [symbol-map ...]}
;;    :modules    {file-path [symbol-map ...]}
;;    :commands   [symbol-map ...]
;;    :source-root string-or-nil}
;;
;; Each symbol-map:
;;   {:kind      keyword    ; :cmd :defn :def :defonce :defmacro
;;                          ; :fn :c-macro :typedef :struct
;;    :name      string
;;    :namespace string
;;    :file      string
;;    :line      integer
;;    :doc       string}
(defonce index-atom (atom nil))

(defn- env [name]
  (let [out (:out (shell/exec ["sh" "-c" (str "printf %s \"${" name "}\"")]))]
    (when-not (str/blank? out) out)))

(defn- file-exists? [path]
  (zero? (:exit (shell/exec ["test" "-f" path]))))

(defn source-root
  "Resolve the directory that holds clj/ and src/. Returns a string or nil.
  Checks $HAMMOCK_SOURCE first, then the current working directory, then gives up."
  []
  (or (when-let [s (env "HAMMOCK_SOURCE")]
        (when (file-exists? (str s "/clj/state.clj")) s))
      (when (file-exists? "clj/state.clj") ".")))

(declare build-clojure-index build-c-index build-commands-index)

(defn rebuild! []
  (let [root (source-root)
        clj  (build-clojure-index root)
        c    (build-c-index root)
        cmds (build-commands-index clj)]
    (reset! index-atom
            {:namespaces  clj
             :modules     c
             :commands    cmds
             :source-root root})
    @index-atom))

(defn ensure! []
  (or @index-atom (rebuild!)))

;; Stubs filled in by later tasks.
(defn- build-clojure-index [_root] {})
(defn- build-c-index [_root] {})
(defn- build-commands-index [_clojure-index] [])
```

- [ ] **Step 2: Load the new namespace at startup**

The C kernel loads Clojure files on startup via a hardcoded sequence in `src/main.c` around lines 274-281:

```c
        free(sci_load_file("clj/state.clj"));
        free(sci_load_file("clj/effects.clj"));
        free(sci_load_file("clj/core.clj"));
        free(sci_load_file("clj/git.clj"));
        free(sci_load_file("clj/markdown.clj"));
        free(sci_load_file("clj/commands.clj"));
        free(sci_load_file("clj/keybindings.clj"));
        free(sci_load_file("clj/modes.clj"));
```

`hammock.symbols` must load **before** `clj/commands.clj` because Task 8 adds a `[hammock.symbols :as symbols]` require to that file. Insert the new line between `markdown.clj` and `commands.clj`:

```c
        free(sci_load_file("clj/state.clj"));
        free(sci_load_file("clj/effects.clj"));
        free(sci_load_file("clj/core.clj"));
        free(sci_load_file("clj/git.clj"));
        free(sci_load_file("clj/markdown.clj"));
        free(sci_load_file("clj/symbols.clj"));
        free(sci_load_file("clj/commands.clj"));
        free(sci_load_file("clj/keybindings.clj"));
        free(sci_load_file("clj/modes.clj"));
```

This requires a rebuild (`make`) because `src/main.c` changed.

- [ ] **Step 3: Build and run**

```
make
./hammock
```

Expected: Hammock starts normally, no errors in the minibuffer or on stderr.

- [ ] **Step 4: Verify the atom is present**

In `*scratch*`, type and evaluate with `C-j`:

```
(hammock.symbols/ensure!)
```

Expected output: `{:namespaces {}, :modules {}, :commands []}` (or similar EDN representation). Empty but non-nil.

- [ ] **Step 5: Commit**

```
git add clj/symbols.clj src/main.c
git commit -m "Add hammock.symbols namespace with index cache skeleton"
```

---

## Task 4: Implement Clojure namespace indexing in `build-clojure-index`

**Files:**
- Modify: `clj/symbols.clj`

- [ ] **Step 1: Replace the `build-clojure-index` stub**

In `clj/symbols.clj`, replace the `(defn- build-clojure-index [_root] {})` stub with the real implementation and its helpers. Add them above `rebuild!` (after the atom declaration and `declare`):

```clojure
(def ^:private ns-re        #"^\(ns\s+([a-zA-Z0-9.\-]+)")
(def ^:private defn-re      #"^\(defn-?\s+([a-zA-Z0-9!?*+<>=\-]+)(?:\s+\"([^\"]*)\")?")
(def ^:private def-re       #"^\(def\s+([a-zA-Z0-9!?*+<>=\-]+)")
(def ^:private defonce-re   #"^\(defonce\s+([a-zA-Z0-9!?*+<>=\-]+)")
(def ^:private defmacro-re  #"^\(defmacro\s+([a-zA-Z0-9!?*+<>=\-]+)")
(def ^:private defcommand-re #"^\(defcommand\s+\"([^\"]+)\"(?:\s+\"([^\"]*)\")?")

(defn- slurp-file [path]
  (:out (shell/exec ["cat" path])))

(defn- list-clj-files [root]
  (when root
    (let [out (:out (shell/exec
                      ["find" (str root "/clj")
                       "-maxdepth" "1" "-name" "*.clj" "-type" "f"]))]
      (->> (str/split-lines out)
           (remove str/blank?)
           sort))))

(defn- parse-clj-file [path]
  (let [text (slurp-file path)
        lines (str/split-lines text)
        ns-line (some (fn [[idx line]]
                        (when-let [m (re-find ns-re line)]
                          [(second m) idx]))
                      (map-indexed vector lines))
        ns-name (first ns-line)]
    (when ns-name
      (let [symbols (keep-indexed
                      (fn [idx line]
                        (let [ln (inc idx)]
                          (cond
                            (re-find defcommand-re line)
                            (let [[_ nm doc] (re-find defcommand-re line)]
                              {:kind :cmd :name nm :namespace ns-name
                               :file path :line ln :doc (or doc "")})

                            (re-find defmacro-re line)
                            (let [[_ nm] (re-find defmacro-re line)]
                              {:kind :defmacro :name nm :namespace ns-name
                               :file path :line ln :doc ""})

                            (re-find defonce-re line)
                            (let [[_ nm] (re-find defonce-re line)]
                              {:kind :defonce :name nm :namespace ns-name
                               :file path :line ln :doc ""})

                            (re-find defn-re line)
                            (let [[_ nm doc] (re-find defn-re line)]
                              {:kind :defn :name nm :namespace ns-name
                               :file path :line ln :doc (or doc "")})

                            (re-find def-re line)
                            (let [[_ nm] (re-find def-re line)]
                              {:kind :def :name nm :namespace ns-name
                               :file path :line ln :doc ""}))))
                      lines)]
        [ns-name (vec symbols)]))))

(defn- build-clojure-index [root]
  (if (nil? root)
    {}
    (into (sorted-map)
          (keep parse-clj-file (list-clj-files root)))))
```

The recipe: for each file, find the `ns` declaration first, then walk lines and classify any line that matches one of the def-shapes. `re-find` is used twice per line where needed because SCI's regex bindings are compatible with `re-find` but not always with `re-matcher` groups; this keeps the code simple.

Note that `parse-clj-file` receives the absolute-ish path from `find`, which starts with the source root (e.g. `./clj/commands.clj` in dev mode, or `/home/tobi/src/hammock/clj/commands.clj` if `HAMMOCK_SOURCE` is set). Leave the path as-is; `buffer-load-file` handles both absolute and relative paths.

- [ ] **Step 2: Build and reload**

Clojure files are loaded at Hammock startup, so:

```
./hammock
```

- [ ] **Step 3: Verify in scratch**

In `*scratch*`, evaluate (`C-j` after each line):

```
(hammock.symbols/source-root)
```

Expected: `"."` (because `./clj/state.clj` exists when running from the project root).

```
(hammock.symbols/rebuild!)
(sort (keys (:namespaces @hammock.symbols/index-atom)))
```

Expected: a sorted sequence containing at least `"hammock.commands"`, `"hammock.core"`, `"hammock.git"`, `"hammock.keybindings"`, `"hammock.markdown"`, `"hammock.modes"`, `"hammock.state"`, `"hammock.symbols"`.

Also evaluate:

```
(count (get-in @hammock.symbols/index-atom [:namespaces "hammock.commands"]))
```

Expected: a number greater than 30 (every `defcommand` and `defn` in `clj/commands.clj`).

- [ ] **Step 4: Commit**

```
git add clj/symbols.clj
git commit -m "Index Clojure namespaces from clj/*.clj"
```

---

## Task 5: Implement C module indexing in `build-c-index`

**Files:**
- Modify: `clj/symbols.clj`

- [ ] **Step 1: Add C indexing helpers and replace the stub**

Add these helpers above `build-c-index`, and replace the stub. Keep them in the private section with the Clojure regexes:

```clojure
(def ^:private c-func-re    #"^[A-Za-z_][A-Za-z0-9_ \t*]*\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
(def ^:private c-define-re  #"^#define\s+([A-Za-z_][A-Za-z0-9_]*)")
(def ^:private c-typedef-re #"^typedef\s+.*\s([A-Za-z_][A-Za-z0-9_]*)\s*;")
(def ^:private c-struct-re  #"^\}\s*([A-Za-z_][A-Za-z0-9_]*)\s*;")

(def ^:private c-keywords
  #{"if" "else" "while" "for" "return" "sizeof" "switch" "case" "do"
    "break" "continue" "goto" "typedef" "struct" "union" "enum" "static"
    "extern" "const" "volatile" "register" "auto" "inline"})

(defn- list-c-files [root]
  (when root
    (let [out (:out (shell/exec
                      ["find" (str root "/src") "-maxdepth" "1" "-type" "f"
                       "(" "-name" "*.c" "-o" "-name" "*.h" ")"]))]
      (->> (str/split-lines out)
           (remove str/blank?)
           sort))))

(defn- classify-c-line [line]
  (cond
    (re-find c-define-re line)
    (let [[_ nm] (re-find c-define-re line)]
      {:kind :c-macro :name nm})

    (re-find c-typedef-re line)
    (let [[_ nm] (re-find c-typedef-re line)]
      {:kind :typedef :name nm})

    (re-find c-struct-re line)
    (let [[_ nm] (re-find c-struct-re line)]
      {:kind :struct :name nm})

    (re-find c-func-re line)
    (let [[_ nm] (re-find c-func-re line)]
      (when-not (contains? c-keywords nm)
        {:kind :fn :name nm}))))

(defn- parse-c-file [path]
  (let [text (slurp-file path)
        lines (str/split-lines text)
        symbols (keep-indexed
                  (fn [idx line]
                    (when-let [sym (classify-c-line line)]
                      (assoc sym
                             :namespace path
                             :file path
                             :line (inc idx)
                             :doc (str path ":" (inc idx)))))
                  lines)]
    [path (vec symbols)]))

(defn- build-c-index [root]
  (if (nil? root)
    {}
    (into (sorted-map) (map parse-c-file (list-c-files root)))))
```

The `-name "*.c" -o -name "*.h"` inside `( ... )` parentheses is important: without the parens, `find` binds `-o` more loosely than `-type f` and matches directories. Pass each paren as its own argv token.

The `c-keywords` set prevents lines like `if (...)` or `while (...)` from being classified as function definitions when the identifier that precedes `(` is a C keyword.

- [ ] **Step 2: Reload and verify**

```
./hammock
```

In `*scratch*`:

```
(hammock.symbols/rebuild!)
(keys (:modules @hammock.symbols/index-atom))
```

Expected: includes `"src/buffer.c"`, `"src/command.c"`, `"src/effects.c"`, `"src/mode.h"`, etc.

```
(count (get-in @hammock.symbols/index-atom [:modules "src/buffer.c"]))
```

Expected: more than 10. Inspect a few entries to spot-check classification:

```
(take 5 (get-in @hammock.symbols/index-atom [:modules "src/buffer.c"]))
```

- [ ] **Step 3: Commit**

```
git add clj/symbols.clj
git commit -m "Index C modules from src/*.{c,h}"
```

---

## Task 6: Implement `build-commands-index`

**Files:**
- Modify: `clj/symbols.clj`

- [ ] **Step 1: Replace the stub**

Replace `(defn- build-commands-index [_clojure-index] [])` with the real version:

```clojure
(defn- build-commands-index [clojure-index]
  (let [cmd-index (->> (get clojure-index "hammock.commands" [])
                       (filter #(= :cmd (:kind %)))
                       (map (juxt :name identity))
                       (into {}))]
    (->> @state/*commands*
         (map (fn [[nm entry]]
                (let [doc (if (map? entry) (:doc entry) "")
                      located (get cmd-index nm)]
                  {:kind :cmd
                   :name nm
                   :namespace "commands"
                   :file (or (:file located) "")
                   :line (or (:line located) 0)
                   :doc (or doc "")})))
         (sort-by :name)
         vec)))
```

This merges two sources: the live commands table (which includes both C-native and Clojure commands) with the file/line data from the already-built Clojure index.

- [ ] **Step 2: Reload and verify**

```
./hammock
```

In `*scratch*`:

```
(hammock.symbols/rebuild!)
(count (:commands @hammock.symbols/index-atom))
```

Expected: matches roughly the size of `@hammock.state/*commands*`.

```
(first (filter #(= "list-buffers" (:name %))
               (:commands @hammock.symbols/index-atom)))
```

Expected: a map with `:file "clj/commands.clj"` and a non-zero `:line`.

```
(first (filter #(= "forward-char" (:name %))
               (:commands @hammock.symbols/index-atom)))
```

Expected: `:file` will likely be `""` if `forward-char` is not defined in Clojure; otherwise points at `clj/commands.clj`.

- [ ] **Step 3: Commit**

```
git add clj/symbols.clj
git commit -m "Index commands table with Clojure source locations"
```

---

## Task 7: Add renderers to `clj/symbols.clj`

**Files:**
- Modify: `clj/symbols.clj`

- [ ] **Step 1: Add rendering functions at the bottom of the file**

Append to `clj/symbols.clj`:

```clojure
;; ---- Renderers ----

(defn- truncate [s n]
  (if (<= (count s) n) s (str (subs s 0 (max 0 (- n 1))) "…")))

(defn- kind-tag [k]
  (case k
    :cmd      "cmd "
    :defn     "defn"
    :def      "def "
    :defonce  "onc "
    :defmacro "mac "
    :fn       "fn  "
    :c-macro  "#def"
    :typedef  "typ "
    :struct   "str "
    "    "))

(defn- pad-right [s n]
  (let [s (truncate s n)]
    (str s (apply str (repeat (max 0 (- n (count s))) \space)))))

(def ^:private no-source-line
  "  (no source tree found — set HAMMOCK_SOURCE)\n")

(defn format-namespace-pane
  "Return the text for the left pane: three roots with their namespaces/modules
  and symbol counts."
  []
  (let [idx (ensure!)
        header "  Namespaces and modules (Enter to drill in)\n\n"
        sep   "\n"
        clj-lines (if (nil? (:source-root idx))
                    [no-source-line]
                    (for [[ns syms] (:namespaces idx)]
                      (format "  %s (%d)\n" (pad-right ns 28) (count syms))))
        c-lines   (if (nil? (:source-root idx))
                    [no-source-line]
                    (for [[path syms] (:modules idx)]
                      (format "  %s (%d)\n" (pad-right path 28) (count syms))))
        cmd-line  (format "  %s (%d)\n"
                          (pad-right "(all)" 28) (count (:commands idx)))]
    (str header
         "Clojure\n" (apply str clj-lines) sep
         "C\n" (apply str c-lines) sep
         "Commands\n" cmd-line)))

(defn- format-symbol-line [sym]
  (let [tag  (kind-tag (:kind sym))
        nm   (pad-right (:name sym) 28)
        info (let [d (:doc sym)]
               (if (str/blank? d) "(no docstring)" d))]
    (format "  [%s] %s — %s\n" tag nm (truncate info 120))))

(defn format-symbol-pane
  "Return the text for the right pane given a selected namespace/module name."
  [selector]
  (let [idx (ensure!)
        syms (cond
               (= selector "(all)")          (:commands idx)
               (contains? (:namespaces idx) selector)
               (get-in idx [:namespaces selector])
               (contains? (:modules idx) selector)
               (get-in idx [:modules selector])
               :else nil)]
    (cond
      (nil? syms)  (str "  (unknown: " selector ")\n")
      (empty? syms) "  (empty)\n"
      :else (apply str (map format-symbol-line syms)))))

(defn all-symbols-flat
  "Return a flat vector of every indexed symbol."
  []
  (let [idx (ensure!)]
    (concat (mapcat val (:namespaces idx))
            (mapcat val (:modules idx))
            (:commands idx))))

(defn apropos-match
  "Return symbols whose name or docstring contains `pattern` (case-insensitive)."
  [pattern]
  (let [needle (str/lower-case (or pattern ""))]
    (->> (all-symbols-flat)
         (filter (fn [{:keys [name doc namespace]}]
                   (or (str/includes? (str/lower-case (or name "")) needle)
                       (str/includes? (str/lower-case (or doc "")) needle)
                       (str/includes? (str/lower-case (or namespace "")) needle))))
         (sort-by (fn [s] [(:namespace s) (:name s)]))
         vec)))

(defn format-apropos
  "Return text for the *Apropos* buffer for a given pattern."
  [pattern]
  (let [hits (apropos-match pattern)]
    (if (empty? hits)
      (str "  No matches for \"" pattern "\"\n")
      (apply str
             (format "  Apropos: %s  (%d matches)\n\n" pattern (count hits))
             (map (fn [sym]
                    (format "  [%s] %s/%s — %s\n"
                            (kind-tag (:kind sym))
                            (pad-right (:namespace sym) 24)
                            (pad-right (:name sym) 24)
                            (truncate (let [d (:doc sym)]
                                        (if (str/blank? d) "(no docstring)" d))
                                      60)))
                  hits)))))

(defn name-at-line
  "Parse the symbol name out of a line rendered by format-symbol-line.
  Format: `  [kind] <name padded to 28> — <info>`."
  [line]
  (let [s (or line "")]
    (when (and (>= (count s) 40)
               (str/starts-with? s "  ["))
      (str/trim (subs s 9 37)))))

(defn namespace-at-line
  "Parse the namespace name out of a line rendered by format-namespace-pane.
  Format: `  <name padded to 28> (N)`."
  [line]
  (let [s (or line "")]
    (when (and (>= (count s) 4)
               (str/starts-with? s "  ")
               (not (#{"  C" "  Clojure" "  Commands"} s))
               (not (str/starts-with? s "  Namespaces"))
               (not (str/starts-with? s "  (no source tree")))
      (let [trimmed (str/trim (subs s 2))
            paren (.indexOf trimmed "(")]
        (if (neg? paren)
          trimmed
          (str/trim (subs trimmed 0 paren)))))))

(defn apropos-name-at-line
  "Parse the symbol name out of a line rendered by format-apropos.
  Format: `  [kind] <ns padded to 24>/<name padded to 24> — <doc>`."
  [line]
  (let [s (or line "")]
    (when (and (>= (count s) 40) (str/starts-with? s "  ["))
      (let [after-ns (subs s 9)
            slash (.indexOf after-ns "/")]
        (when (pos? slash)
          (let [name-region (subs after-ns (inc slash))
                dash (.indexOf name-region "—")]
            (str/trim (if (neg? dash) name-region (subs name-region 0 dash)))))))))

(defn lookup-symbol
  "Find a fully-qualified or bare symbol in the index and return its map."
  [ns-name sym-name]
  (let [idx (ensure!)
        candidates (concat (get-in idx [:namespaces ns-name])
                           (get-in idx [:modules ns-name])
                           (when (= ns-name "commands") (:commands idx))
                           (when (#{"Clojure" "C" "Commands"} ns-name)
                             (all-symbols-flat)))]
    (first (filter #(= sym-name (:name %)) candidates))))

(defn find-command
  "Find a command by name in the commands list."
  [sym-name]
  (let [idx (ensure!)]
    (first (filter #(= sym-name (:name %)) (:commands idx)))))
```

- [ ] **Step 2: Reload and verify in scratch**

```
./hammock
```

In `*scratch*`:

```
(println (hammock.symbols/format-namespace-pane))
```

Expected: human-readable listing of namespaces/modules/commands with counts.

```
(println (hammock.symbols/format-symbol-pane "hammock.commands"))
```

Expected: a list of commands with `[cmd ]` tags.

```
(count (hammock.symbols/apropos-match "buffer"))
```

Expected: a positive integer (likely 20-50).

```
(hammock.symbols/namespace-at-line "  hammock.commands             (54)")
```

Expected: `"hammock.commands"`.

```
(hammock.symbols/name-at-line "  [cmd ] forward-char                — Move point forward one character")
```

Expected: `"forward-char"`.

- [ ] **Step 3: Commit**

```
git add clj/symbols.clj
git commit -m "Add namespace-pane, symbol-pane, and apropos renderers"
```

---

## Task 8: Add `browse-symbols` and related commands to `clj/commands.clj`

**Files:**
- Modify: `clj/commands.clj`

- [ ] **Step 1: Add a require**

At the top of `clj/commands.clj`, add `[hammock.symbols :as symbols]` to the `:require` vector. After editing, the require block looks like:

```clojure
(ns hammock.commands
  (:require [hammock.state :as state]
            [hammock.effects :as fx]
            [hammock.core :as core]
            [hammock.git :as git]
            [hammock.markdown :as md]
            [hammock.symbols :as symbols]
            [clojure.string :as str]))
```

- [ ] **Step 2: Append the new commands to the bottom of `commands.clj`**

Append (keeping the existing `;; ---- Version ----` section last or moving the new block above it — either is fine, just be consistent):

```clojure
;; ---- Symbol explorer ----

(defn- first-namespace-for-pane []
  (let [idx (symbols/ensure!)]
    (or (first (keys (:namespaces idx)))
        (first (keys (:modules idx)))
        "(all)")))

(defn- symbol-browser-layout-effects []
  (let [ns-text (symbols/format-namespace-pane)
        first-ns (first-namespace-for-pane)
        sym-text (symbols/format-symbol-pane first-ns)]
    [[:window-delete-others]
     [:buffer-create "*Symbols*"]
     [:buffer-switch "*Symbols*"]
     [:buffer-set-read-only false]
     [:buffer-set-contents ns-text]
     [:point-to-buffer-start]
     [:buffer-set-modified false]
     [:buffer-set-read-only true]
     [:buffer-set-mode "Symbol-Browser"]
     [:window-split-right]
     [:window-other]
     [:buffer-create "*Symbol-Detail*"]
     [:buffer-switch "*Symbol-Detail*"]
     [:buffer-set-read-only false]
     [:buffer-set-contents sym-text]
     [:point-to-buffer-start]
     [:buffer-set-modified false]
     [:buffer-set-read-only true]
     [:buffer-set-mode "Symbol-Detail"]
     [:window-other]]))

(defcommand "browse-symbols"
  "Open the namespace/symbol explorer."
  (fn []
    (symbols/ensure!)
    (symbol-browser-layout-effects)))

(defcommand "symbrowse-select"
  "Populate the right pane with symbols of the namespace at point."
  (fn []
    (let [line (fx/current-line)
          selector (symbols/namespace-at-line line)]
      (if selector
        (let [text (symbols/format-symbol-pane selector)]
          [[:window-other]
           [:buffer-set-read-only false]
           [:buffer-set-contents text]
           [:point-to-buffer-start]
           [:buffer-set-modified false]
           [:buffer-set-read-only true]])
        [[:message "Not on a namespace line"]]))))

(defn- visit-symbol-effects [sym]
  (let [file (:file sym)
        line (:line sym)
        base (when file (last (str/split file #"/")))]
    (cond
      (or (str/blank? file) (zero? (or line 0)))
      [[:message (str "No source location for " (:name sym))]]

      :else
      [[:window-delete-others]
       [:buffer-destroy "*Symbol-Detail*"]
       [:buffer-destroy "*Symbols*"]
       [:buffer-create base]
       [:buffer-switch base]
       [:buffer-load-file file]
       [:point-to-line line]])))

(defcommand "symbrowse-visit"
  "Jump to the definition of the symbol at point in the explorer."
  (fn []
    (let [line (fx/current-line)
          nm (symbols/name-at-line line)]
      (if (str/blank? nm)
        [[:message "No symbol at point"]]
        (let [idx (symbols/ensure!)
              hit (or (symbols/find-command nm)
                      (first (filter #(= nm (:name %))
                                     (mapcat val (:namespaces idx))))
                      (first (filter #(= nm (:name %))
                                     (mapcat val (:modules idx)))))]
          (if hit
            (visit-symbol-effects hit)
            [[:message (str "Symbol not in index: " nm)]]))))))

(defcommand "symbrowse-refresh"
  "Rebuild the symbol index and re-render the explorer."
  (fn []
    (symbols/rebuild!)
    (symbol-browser-layout-effects)))

(defcommand "symbrowse-quit"
  "Close the symbol explorer."
  (fn []
    [[:buffer-destroy "*Symbol-Detail*"]
     [:buffer-destroy "*Symbols*"]
     [:window-delete-others]
     [:buffer-switch "*scratch*"]]))
```

- [ ] **Step 3: Reload and smoke test (partial)**

```
./hammock
```

`browse-symbols` has no binding yet, so call it via `M-x`: press `M-x`, type `browse-symbols`, `Enter`. Expected: two panes appear. Left shows the hierarchy, right shows symbols for the first namespace.

Move down a few lines in the left pane with `C-n`, then `M-x symbrowse-select` (the mode binding isn't registered until Task 10, so go via `M-x` for now). The right pane should update. Pressing `Enter` doesn't do the visit yet (still bound to default `newline`, which is blocked on a read-only buffer) — verify that `M-x symbrowse-visit` works when focus is in the right pane on a valid symbol line.

`M-x symbrowse-quit` to close. Verify both buffers are gone from `C-x C-b`.

- [ ] **Step 4: Commit**

```
git add clj/commands.clj
git commit -m "Add browse-symbols and symbrowse-* commands"
```

---

## Task 9: Add `apropos` command to `clj/commands.clj`

**Files:**
- Modify: `clj/commands.clj`

- [ ] **Step 1: Append the apropos commands below the symbrowse block**

```clojure
;; ---- Apropos ----

(defn- apropos-layout-effects [pattern]
  (let [text (symbols/format-apropos pattern)]
    [[:buffer-create "*Apropos*"]
     [:window-split-below]
     [:window-other]
     [:buffer-switch "*Apropos*"]
     [:buffer-set-read-only false]
     [:buffer-set-contents text]
     [:point-to-buffer-start]
     [:buffer-set-modified false]
     [:buffer-set-read-only true]
     [:buffer-set-mode "Apropos"]]))

(defcommand "apropos"
  "Prompt for a pattern and list matching symbols."
  (fn []
    [[:prompt "Apropos: " "hammock.commands/apropos-cb" :none]]))

(defn apropos-cb [pattern]
  (symbols/ensure!)
  (apropos-layout-effects pattern))

(defcommand "apropos-visit"
  "Jump to the definition of the symbol at point in the apropos buffer."
  (fn []
    (let [line (fx/current-line)
          nm (symbols/apropos-name-at-line line)]
      (if (str/blank? nm)
        [[:message "No symbol at point"]]
        (let [idx (symbols/ensure!)
              hit (or (symbols/find-command nm)
                      (first (filter #(= nm (:name %))
                                     (mapcat val (:namespaces idx))))
                      (first (filter #(= nm (:name %))
                                     (mapcat val (:modules idx)))))]
          (if hit
            (into [[:buffer-destroy "*Apropos*"]] (visit-symbol-effects hit))
            [[:message (str "Symbol not in index: " nm)]]))))))

(defcommand "apropos-quit"
  "Close the apropos window."
  (fn [] [[:buffer-destroy "*Apropos*"] [:window-delete]]))
```

- [ ] **Step 2: Reload and smoke test**

```
./hammock
```

`M-x apropos`, type `buffer`, `Enter`. A split-below window should appear with a list of matches. Move point to a line and run `M-x apropos-visit` — that should close the apropos buffer and jump to the definition. `M-x apropos-quit` should also work from the apropos window.

- [ ] **Step 3: Commit**

```
git add clj/commands.clj
git commit -m "Add apropos command with flat filtered listing"
```

---

## Task 10: Register the three new modes in `clj/modes.clj`

**Files:**
- Modify: `clj/modes.clj`

- [ ] **Step 1: Edit `mode-definitions`**

In `clj/modes.clj`, replace the `mode-definitions` vector (around line 12) to include the three new entries:

```clojure
(def mode-definitions
  [{:id 0 :name "Fundamental" :syntax "none"     :extensions [] :keymap nil}
   {:id 1 :name "C"           :syntax "c"        :extensions [".c" ".h" ".cc" ".cpp" ".hpp"] :keymap nil}
   {:id 2 :name "Clojure"     :syntax "clojure"  :extensions [".clj" ".cljs" ".cljc" ".edn" ".bb"] :keymap "clojure"}
   {:id 3 :name "Bash"        :syntax "bash"     :extensions [".sh" ".bash" ".zsh"] :keymap nil}
   {:id 4 :name "Markdown"    :syntax "markdown" :extensions [".md" ".markdown"] :keymap "markdown"}
   {:id 5 :name "Git-Status"  :syntax "none"     :extensions [] :keymap "git-status"}
   {:id 6 :name "Shell"       :syntax "none"     :extensions [] :keymap nil}
   {:id 7 :name "Buffer-List" :syntax "none"     :extensions [] :keymap nil}
   {:id 8 :name "Diff"        :syntax "diff"     :extensions [] :keymap "diff"}
   {:id 9 :name "Grep"        :syntax "none"     :extensions [] :keymap "grep"}
   {:id 10 :name "Help"       :syntax "help"     :extensions [] :keymap nil}
   {:id 11 :name "Symbol-Browser" :syntax "none" :extensions [] :keymap "symbol-browser"}
   {:id 12 :name "Symbol-Detail"  :syntax "none" :extensions [] :keymap "symbol-detail"}
   {:id 13 :name "Apropos"        :syntax "none" :extensions [] :keymap "apropos"}])
```

- [ ] **Step 2: Verify**

```
./hammock
```

`M-x browse-symbols`. Check the modeline of the left pane — it should now say `Symbol-Browser`, and the right pane `Symbol-Detail`. `M-x apropos`, pattern `x`, the new buffer modeline should say `Apropos`.

- [ ] **Step 3: Commit**

```
git add clj/modes.clj
git commit -m "Register Symbol-Browser, Symbol-Detail, Apropos modes"
```

---

## Task 11: Add keybindings in `clj/keybindings.clj`

**Files:**
- Modify: `clj/keybindings.clj`

- [ ] **Step 1: Add to `ch-bindings` and `f1-bindings`**

Find the `ch-bindings` vector (around line 165) and add two entries. After editing:

```clojure
(def ch-bindings
  [["k" "describe-key"]
   ["f" "describe-function"]
   ["n" "view-news"]
   ["s" "browse-symbols"]
   ["a" "apropos"]])
```

Find the `f1-bindings` vector just above it and add one entry:

```clojure
(def f1-bindings
  [["k" "describe-key"]
   ["f" "describe-function"]
   ["n" "view-news"]
   ["s" "browse-symbols"]])
```

- [ ] **Step 2: Add the three new mode keymaps to `mode-bindings`**

Find `mode-bindings` (around line 170) and add three entries. After editing, the map contains:

```clojure
(def mode-bindings
  {"markdown"   [["Enter" "markdown-follow-link"]
                 ["Tab"   "markdown-next-link"]
                 ["l"     "markdown-go-back"]
                 ["n"     "markdown-next-heading"]
                 ["p"     "markdown-prev-heading"]]
   "git-status" [["s"     "git-stage"]
                 ["u"     "git-unstage"]
                 ["c"     "git-commit"]
                 ["q"     "git-quit"]
                 ["g"     "git-refresh"]
                 ["d"     "git-diff"]
                 ["Tab"   "git-toggle-section"]
                 ["Enter" "git-visit-file"]]
   "clojure"      []
   "diff"         [["q"     "diff-quit"]]
   "grep"         [["Enter" "grep-visit"]
                   ["q"     "grep-quit"]
                   ["g"     "grep-refresh"]]
   "buffer-list"  [["Enter" "buflist-visit"]
                   ["D"     "buflist-mark-delete"]
                   ["x"     "buflist-execute"]
                   ["q"     "buflist-quit"]]
   "symbol-browser" [["n"     "next-line"]
                     ["p"     "previous-line"]
                     ["Enter" "symbrowse-select"]
                     ["g"     "symbrowse-refresh"]
                     ["q"     "symbrowse-quit"]]
   "symbol-detail"  [["n"     "next-line"]
                     ["p"     "previous-line"]
                     ["Enter" "symbrowse-visit"]
                     ["Tab"   "other-window"]
                     ["g"     "symbrowse-refresh"]
                     ["q"     "symbrowse-quit"]]
   "apropos"        [["Enter" "apropos-visit"]
                     ["g"     "apropos"]
                     ["q"     "apropos-quit"]]})
```

- [ ] **Step 3: Smoke test the bindings**

```
./hammock
```

- `C-h s` opens the explorer.
- In the left pane, `n` moves down, `Enter` on a namespace populates the right pane.
- In the right pane, `n` moves down, `Enter` on a Clojure symbol closes the explorer and jumps to the file/line.
- `C-h s` again, then `g` re-indexes (should be near-instant on subsequent calls), then `q` closes cleanly.
- `C-h a`, type `buffer`, `Enter`, navigate, `Enter` to jump, `q` to close.
- `F1 s` also opens the explorer.

- [ ] **Step 4: Commit**

```
git add clj/keybindings.clj
git commit -m "Bind browse-symbols (C-h s, F1 s) and apropos (C-h a)"
```

---

## Task 12: Version bump, news entry, README update

**Files:**
- Modify: `clj/core.clj`
- Modify: `NEWS.md`
- Modify: `README.md`

- [ ] **Step 1: Bump the version**

In `clj/core.clj`, change:

```clojure
(defn hammock-version [] "0.1.1")
```

to:

```clojure
(defn hammock-version [] "0.1.2")
```

- [ ] **Step 2: Prepend a new NEWS section**

At the top of `NEWS.md`, just below the `# Hammock NEWS` header and above the existing `## Version 0.1.1` block, insert:

```markdown
## Version 0.1.2 (2026-04-09)

### Introspection

- New namespace/symbol explorer: `C-h s` (also `F1 s`) opens a two-pane
  buffer listing Clojure namespaces, C modules, and registered commands,
  with symbol counts. `Enter` on a namespace drills into its symbols;
  `Enter` on a symbol jumps to its definition. `g` rebuilds the index,
  `q` closes the explorer.
- New `apropos` command (`C-h a`) prompts for a pattern and shows a flat
  list of matching symbols across every indexed source. `Enter` jumps to
  the definition.
- Both commands share a cached index built from `clj/*.clj`, `src/*.{c,h}`,
  and the live commands table, refreshable via `g` inside either buffer.
- New effect `[:point-to-line N]` jumps to an absolute 1-indexed line
  number, used by the explorer's jump-to-definition.

```

- [ ] **Step 3: Update README**

In `README.md`, find the line:

```markdown
- [ ] feature: add a namespace/symbol explorer, so you can navigate through all the namespaces and symbols and finally jump to definition (is apropos this?)
```

and change it to:

```markdown
- [x] feature: add a namespace/symbol explorer, so you can navigate through all the namespaces and symbols and finally jump to definition
```

- [ ] **Step 4: Verify**

```
make
./hammock
```

In `*scratch*`, evaluate `(hammock.core/hammock-version)` with `C-j`. Expected: `"0.1.2"`.

`M-x version` should display `Hammock 0.1.2`.

`F1 n` (view news) should show the new 0.1.2 section at the top.

- [ ] **Step 5: Commit**

```
git add clj/core.clj NEWS.md README.md
git commit -m "Release 0.1.2: symbol explorer and apropos"
```

---

## Task 13: Final manual smoke test

**Files:** none — this task is verification only.

- [ ] **Step 1: Clean build**

```
make clean && make
```

Expected: clean build with no warnings introduced by this plan.

- [ ] **Step 2: Walk the test plan from the spec**

Perform each step from the "Test plan" section of `docs/superpowers/specs/2026-04-09-symbol-explorer-design.md`:

1. Launch `./hammock` from the project root.
2. `C-h s`. Two panes appear; left has `Clojure`/`C`/`Commands` headers with entries underneath; right shows the first namespace's symbols.
3. `n`/`p` in the left pane, then `Enter` on another namespace. Right pane updates, focus moves there.
4. `Enter` on a Clojure `defcommand` symbol (e.g. `forward-char` or `list-buffers`). Explorer closes, buffer jumps to the correct line in `clj/commands.clj`.
5. `C-h s` again, navigate to `C` section, pick a `.c` file, `Enter`, then `Enter` on a function. Jumps to the correct line.
6. `C-h s` again, `g` — should return quickly and leave the list unchanged.
7. `C-h a`, type `buffer`, `Enter`. `*Apropos*` appears with matches from all roots. `Enter` to visit, `q` to close.
8. `M-.` on a symbol in any source file — the pre-existing `find-definition` still works.
9. In `*scratch*`, `C-j` on `(count (:namespaces (hammock.symbols/ensure!)))` — result > 0.
10. `M-x version` reports `Hammock 0.1.2`.
11. **No-source-tree fallback:** in a different terminal, `cd /tmp && /absolute/path/to/hammock/hammock`. Press `C-h s`. The explorer should open with the `Clojure` and `C` sections showing `(no source tree found — set HAMMOCK_SOURCE)`, while the `Commands` root still lists every command. `q` to quit.
12. **HAMMOCK_SOURCE override:** still in `/tmp`, quit and relaunch with `HAMMOCK_SOURCE=/absolute/path/to/hammock ./hammock`. `C-h s` should now show full Clojure and C listings. `q` to quit.

- [ ] **Step 3: Fix any failures and recommit**

If any step fails, diagnose and fix. Commit fixes as separate commits with messages describing the specific failure and fix. Do not amend earlier commits.

- [ ] **Step 4: Final verification**

```
git log --oneline main..HEAD
```

Expected: a clean series of ~12 commits, each corresponding to one task.

```
git status
```

Expected: `nothing to commit, working tree clean`.

The feature is done.
