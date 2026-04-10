# Symbol Explorer & Apropos — Design

**Status:** approved
**Date:** 2026-04-09
**Target version:** 0.1.2

## Goal

Add a browsable namespace/symbol explorer plus a flat apropos search. Both let the user discover and jump to definitions across Clojure namespaces, C modules, and registered Hammock commands without already knowing the symbol name.

The existing `find-definition` (`M-.`) only works on the word at point. This design complements it with discovery tools for when you don't yet know what you're looking for.

## User-visible surface

### Commands

| Command | Binding | Behavior |
|---|---|---|
| `browse-symbols` | `C-h s`, `F1 s` | Two-pane hierarchical explorer |
| `apropos` | `C-h a` | Prompt for pattern, show flat filtered list |

Both commands become available via `M-x` as well.

### `browse-symbols` interaction

Pressing `C-h s` wipes other windows, creates two buffers `*Symbols*` (left) and `*Symbol-Detail*` (right) in a vertical split, and leaves focus in the left pane.

**Left pane (`Symbol-Browser` mode)** lists namespaces grouped under three roots:

```
Clojure
  hammock.commands        (54 symbols)
  hammock.git             (9 symbols)
  hammock.markdown        (5 symbols)
  hammock.symbols         (7 symbols)
  ...
C
  buffer.c                (23 symbols)
  command.c               (18 symbols)
  ...
Commands
  (all)                   (86 commands)
```

Keybindings (mode-local):

- `n` / `Down` — next line
- `p` / `Up` — previous line
- `Enter` — select the namespace at point; repopulate the right pane and move focus there
- `g` — rebuild the symbol index, re-render both panes
- `q` — destroy both buffers, return to the previously-current buffer

**Right pane (`Symbol-Detail` mode)** shows one line per symbol in the selected namespace:

```
[cmd ] forward-char                — Move point forward one character
[defn] parse-key-spec               — (no docstring)
[def ] HK_BACKSPACE                 — (no docstring)
[fn  ] buffer_create                — src/buffer.c:42
```

Columns:

- `[kind ]` — fixed 4-character left-justified kind tag
- name — padded to ~28 columns
- `— ` separator
- docstring first line, or `file:line` for C symbols and `(no docstring)` otherwise, truncated to window width

Kinds: `cmd`, `defn`, `def `, `onc` (defonce), `mac` (Clojure defmacro), `fn  `, `#def` (C `#define`), `typ` (typedef), `str` (struct).

Keybindings (mode-local):

- `n` / `p` / arrows — line movement
- `Enter` — jump to the symbol's definition (`file:line`) and close both explorer buffers
- `Tab` / `Backtab` — `window-other` (back to the namespace pane)
- `g` — rebuild and re-render
- `q` — destroy both buffers

### `apropos` interaction

Pressing `C-h a` prompts `Apropos: ` in the minibuffer. On submit, creates `*Apropos*` in a split-below window (matching grep/diff convention) and fills it with every indexed symbol whose `[kind ns/name doc]` string contains the pattern case-insensitively. Focus moves to the new window.

Keybindings (`Apropos` mode):

- `n` / `p` / arrows — line movement
- `Enter` — jump to definition
- `g` — re-prompt for a new pattern and refilter
- `q` — `window-delete`

## Architecture

Almost entirely Clojure. One small new C effect (`[:point-to-line N]`) is added because there is currently no way to jump to an absolute line number in a freshly loaded buffer. Everything else lives in `clj/`.

### New file: `clj/symbols.clj` (namespace `hammock.symbols`)

Responsibilities:

- Build and cache the symbol index.
- Render the three view strings (namespace pane, symbol pane, apropos results).
- Look up a namespace's symbol list by name.
- Look up a symbol's `file:line` by name.

Public API:

```
(def ^:private *index* (atom nil))

(defn rebuild! [])             ;; force fresh build
(defn ensure! [])              ;; build if empty; return current index
(defn namespaces [])           ;; ordered seq of namespace/module names with roots
(defn symbols-of [ns-name])    ;; seq of symbol maps for a namespace
(defn apropos-match [pattern]) ;; flat seq of symbol maps, case-insensitive substring

(defn format-namespace-pane [])
(defn format-symbol-pane [ns-name])
(defn format-apropos [pattern])
```

### Index data shape

```
{:namespaces   {"hammock.commands" [symbol ...] ...}
 :modules      {"src/buffer.c"     [symbol ...] ...}
 :commands     [symbol ...]}

symbol = {:kind      keyword     ; :cmd :defn :def :defonce :defmacro
                                 ; :fn :c-macro :typedef :struct
          :name      string
          :namespace string      ; "hammock.commands" | "src/buffer.c" | "commands"
          :file      string      ; relative path for jumping
          :line      integer     ; 1-indexed
          :doc       string      ; first-line docstring, may be ""
          }
```

### Source root resolution

Indexing needs to find `clj/` and `src/` at runtime. For a binary invoked outside the project tree, these paths don't exist. The v1 behavior is:

1. If the environment variable `HAMMOCK_SOURCE` is set, use it as the source root.
2. Otherwise, if `./clj/state.clj` exists relative to the current working directory, use `.` (dev mode, running from the project root).
3. Otherwise, source root is unresolved.

When the source root is unresolved, the index builds with empty `:namespaces` and `:modules` maps. `:commands` is still populated from the live `@*commands*` atom (since that data is in-process and doesn't need filesystem access), but individual command entries have no `:file`/`:line`. Opening `browse-symbols` or `apropos` displays the explorer normally; the left pane shows only the `Commands` root, and the right pane shows `(no source tree found — set HAMMOCK_SOURCE to a checkout of hammock)` under the `Clojure` and `C` headers.

This keeps the feature functional in the distribution binary (command-level introspection works), while surfacing a clear message that the richer browsing requires a source tree. A follow-up feature (embedded-source shipping or clone-on-first-use) is planned separately and will populate the source root transparently.

### Indexing strategy

**Clojure (`clj/*.clj`):** if source root is resolved, shell out to `find <root>/clj -maxdepth 1 -name '*.clj' -type f`, then read each file via `hammock.shell/exec ["cat" path]`. Walk lines with regex:

- `^\(ns\s+([a-zA-Z0-9.\-]+)` → namespace name (the first such match in the file)
- `^\(defn-?\s+([a-zA-Z0-9!?*+<>=\-]+)\s*(?:"([^"]*)")?` → `:defn`
- `^\(def\s+([a-zA-Z0-9!?*+<>=\-]+)` → `:def`
- `^\(defmacro\s+([a-zA-Z0-9!?*+<>=\-]+)` → `:defmacro`
- `^\(defonce\s+([a-zA-Z0-9!?*+<>=\-]+)` → `:defonce`
- `^\(defcommand\s+"([^"]+)"\s*(?:"([^"]*)")?` → `:cmd`, with the literal command name and docstring

Line number is the 1-indexed position of the matching line. Multi-line docstrings are truncated to whatever appears on the same line as the form (first line only for v1).

**C (`src/*.c`, `src/*.h`):** if source root is resolved, shell out to `find <root>/src -maxdepth 1 -type f -name '*.c' -o -name '*.h'`, then for each file run a single `grep -n -E` with a combined pattern covering:

- Function definition: `^[a-zA-Z_][^=;()]*\b[a-zA-Z_][a-zA-Z0-9_]*\s*\(` — capture the last identifier before `(`
- `#define`: `^#define\s+([A-Z_][A-Z0-9_]*)`
- `typedef`: `^typedef\s+.*\s([a-zA-Z_][a-zA-Z0-9_]*)\s*;`
- Struct/enum trailer: `^}\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*;`

Classify each matched line in Clojure based on which pattern it fits. Doc field for C symbols stores `file:line` so the right-pane formatter can fall back to it when there's no docstring source.

**Commands:** snapshot `@hammock.state/*commands*`. For each entry `[name {:fn _ :doc doc}]`, look up `name` in the already-built Clojure index (specifically under `:cmd` entries of `hammock.commands`). If found, copy its `:file`/`:line`; otherwise mark as `(no source — C native)`. These become the entries under the `Commands` root.

### Laziness and freshness

- `ensure!` builds on first call, reuses on subsequent.
- `rebuild!` drops the cache and re-indexes.
- `g` in either explorer mode calls `rebuild!` and re-renders.
- No automatic invalidation. Source edits won't invalidate the cache until the user presses `g`.

### Command wiring (`clj/commands.clj`)

Add:

```
(defcommand "browse-symbols" "Open the namespace/symbol explorer." ...)
(defcommand "apropos" "Prompt for a pattern and list matching symbols." ...)
(defcommand "symbrowse-select" "Populate the right pane with symbols of the namespace at point." ...)
(defcommand "symbrowse-visit"  "Jump to the definition of the symbol at point in the explorer." ...)
(defcommand "symbrowse-refresh" "Rebuild the symbol index and re-render." ...)
(defcommand "symbrowse-quit"   "Close the symbol explorer." ...)
(defcommand "apropos-visit"    "Jump to the definition of the symbol at point." ...)
(defcommand "apropos-quit"     "Close the apropos window." ...)
```

Plus the `apropos-cb` helper (non-command) that consumes the `:prompt` result.

All of these return effect vectors only. The sole new effect is `[:point-to-line N]`, described below.

### Window layout effects

`browse-symbols` effect sequence:

```clojure
[[:window-delete-others]
 [:buffer-create "*Symbols*"]
 [:buffer-switch "*Symbols*"]
 [:buffer-set-read-only false]
 [:buffer-set-contents <namespace-pane>]
 [:point-to-buffer-start]
 [:buffer-set-modified false]
 [:buffer-set-read-only true]
 [:buffer-set-mode "Symbol-Browser"]
 [:window-split-right]
 [:window-other]
 [:buffer-create "*Symbol-Detail*"]
 [:buffer-switch "*Symbol-Detail*"]
 [:buffer-set-read-only false]
 [:buffer-set-contents <first-symbol-pane>]
 [:point-to-buffer-start]
 [:buffer-set-modified false]
 [:buffer-set-read-only true]
 [:buffer-set-mode "Symbol-Detail"]
 [:window-other]]
```

`symbrowse-select` sequence (user pressed Enter in the left pane):

```clojure
[[:window-other]
 [:buffer-set-read-only false]
 [:buffer-set-contents <symbols-for-selected-ns>]
 [:point-to-buffer-start]
 [:buffer-set-modified false]
 [:buffer-set-read-only true]]
```

`symbrowse-visit` (user pressed Enter in the right pane): parse the name out of the current line using fixed column slicing (the same trick `buflist-name-at-point` uses), look up the full symbol entry in the index, then:

```clojure
[[:window-delete-others]
 [:buffer-create <basename>]
 [:buffer-switch <basename>]
 [:buffer-load-file <file>]
 [:point-to-line <line>]
 [:buffer-destroy "*Symbol-Detail*"]
 [:buffer-destroy "*Symbols*"]]
```

The `[:point-to-line N]` effect is new (see "New effect" below). It is the only addition required on the C side.

### New effect: `[:point-to-line N]`

Add to `src/effects.c` alongside `point-set`:

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
}
```

About 10 lines. It takes a 1-indexed line number and walks from buffer start using the existing `buffer_next_line_start`. This is the one and only C change in this spec.

### Mode registration (`clj/modes.clj` and `src/mode.h`/`src/mode.c`)

Existing modes use capitalized display names (`"Git-Status"`, `"Grep"`) with lowercase keymap references (`"git-status"`, `"grep"`). Following that convention, add three new entries to `mode-definitions`:

```clojure
{:id 11 :name "Symbol-Browser" :syntax "none" :extensions [] :keymap "symbol-browser"}
{:id 12 :name "Symbol-Detail"  :syntax "none" :extensions [] :keymap "symbol-detail"}
{:id 13 :name "Apropos"        :syntax "none" :extensions [] :keymap "apropos"}
```

Because `MajorModeID` in `src/mode.h` is a fixed enum and `MODE_COUNT` is compile-time, the new mode IDs must also be added to the C side: extend the `MajorModeID` enum and the `major_modes[]` table in `src/mode.c`. The keymap-from-Clojure mechanism (`keybindings_load_edn`) handles the new keymap names automatically via the `mode:<name>` prefix, so no new keymap objects are needed in C.

The `buffer-set-mode` effect in `src/effects.c` already does a case-insensitive name lookup, so setting `"Symbol-Browser"` from Clojure will find the new mode as soon as it is registered in C.

### Keymap (`clj/keybindings.clj`)

Additions to `ch-bindings`:

```clojure
["s" "browse-symbols"]
["a" "apropos"]
```

Additions to `f1-bindings`:

```clojure
["s" "browse-symbols"]
```

Additions to `mode-bindings`:

```clojure
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
                  ["q"     "apropos-quit"]]
```

## Edge cases

- **Empty namespace.** A namespace with zero matched forms still appears in the list and shows `(empty)` in the right pane.
- **Unknown symbol in commands.** A command with no Clojure source (C-native) falls back to `describe-function`-style doc display with no jump target; pressing Enter shows a `No source location` message.
- **Missing source tree.** When neither `HAMMOCK_SOURCE` nor `./clj/state.clj` is present, the explorer opens with only the `Commands` root populated. The Clojure and C sections show a `(no source tree found — set HAMMOCK_SOURCE)` message. Jump-to-definition on command entries with no `:file` shows `No source location for <name>` in the minibuffer.
- **Stale cache after edit.** User must press `g` to refresh; not automatic.
- **Repeated open.** Running `browse-symbols` twice is safe: `delete-other-windows` + `buffer-create` (which is a no-op if the buffer exists) rebuilds the layout without rebuilding the index.
- **Path assumptions.** File paths in the index are relative to the project root (the directory Hammock was launched from). Jumping via `buffer-load-file` uses the same resolution as `find-file` and `grep-visit`, so behavior is consistent.
- **Binding conflicts.** `q` already appears in `git-status`, `diff`, `grep`, `buffer-list` mode keymaps. Adding it to the three new modes continues that convention.

## Version bump and release notes

- `clj/core.clj`: `(defn hammock-version [] "0.1.1")` → `"0.1.2"`.
- `NEWS.md`: add a `## Version 0.1.2 (2026-04-09)` section with an "Introspection" subsection describing `browse-symbols` (`C-h s`), `apropos` (`C-h a`), and the fact that they share a cached index refreshable via `g`.
- `README.md`: mark the `feature: add a namespace/symbol explorer...` TODO item as done (strike through or remove the line).

## Test plan

No automated tests (project has none yet). Manual verification:

1. Build `hammock`, launch from project root.
2. Press `C-h s`. Verify two panes appear, left shows `Clojure`/`C`/`Commands` roots with namespaces, right shows symbols of the first namespace.
3. `n`/`p` to a different namespace in the left pane; press `Enter`. Verify right pane updates and focus moves there.
4. Press `Enter` on a Clojure `defcommand` symbol. Verify the explorer closes and the buffer jumps to the correct line in `clj/commands.clj`.
5. Press `C-h s` again, navigate to `C` → `buffer.c`, `Enter`, then `Enter` on a function. Verify jump.
6. Press `C-h s` again, then `g`. Verify a message or immediate re-render; list should be unchanged if no files were edited.
7. Press `C-h a`, type `buffer`, submit. Verify `*Apropos*` window appears with matches across all three roots. `Enter` to visit, `q` to close.
8. Verify `M-.` on a symbol in a source file still works unchanged.
9. In the scratch buffer, evaluate `(count (:namespaces (hammock.symbols/ensure!)))` with `C-j` and verify the result is greater than zero.
10. Run `M-x version` and confirm it reports `Hammock 0.1.2`.
11. Change directory to `/tmp`, launch the binary by absolute path, press `C-h s`. Verify the explorer shows the `Commands` root populated and the `Clojure`/`C` sections show `(no source tree found — set HAMMOCK_SOURCE)`.
12. Launch with `HAMMOCK_SOURCE=<project-root> ./hammock` from `/tmp` and verify `C-h s` shows the full listings.
