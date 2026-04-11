# Hammock NEWS -- history of user-visible changes.

## Version 0.2.0 (2026-04-11)

### *Messages* buffer

- New `*Messages*` buffer captures every call to `message()` (both from C
  and from the Clojure `[:message ...]` effect). It is a real gap-buffer
  backed buffer: switch to it, scroll, search, run commands, just like any
  other buffer. Capped at 1000 lines with FIFO eviction of the oldest
  entries.
- New command `view-messages` (bound to `C-h e`, mirroring Emacs) switches
  to `*Messages*`. `clear-messages` erases it.
- `hammock.commands/dispatch` now wraps every Clojure command body in
  `try/catch`. Exceptions are converted to a `[:message]` effect so they
  surface in the echo area and in `*Messages*` instead of silently
  vanishing when `sci_eval` returns non-EDN error text. This fixes a
  long-standing debugging gap where Clojure errors during live reload
  simply disappeared.

### Bootstrap

- New `clj/loadup.clj` exports the load order as data (`hammock.loadup/files`).
  `main.c` reads the vector back and calls `sci_load_file` on each entry,
  mirroring Emacs's `lisp/loadup.el` pattern. Adding a new Clojure file is
  now a pure Clojure change: add it to the vector in one place. The old
  hardcoded list in `main.c` and `test/smoke.c` is gone.

### Cleanup

- Removed dead `src/markdown.c` / `src/markdown.h`: `markdown_link_at_point`
  and `markdown_link_free` had no callers; the live link-at-point logic
  moved to `clj/markdown.clj` in an earlier release.

## Version 0.1.3 (2026-04-10)

### Command line

- New `-e EXPR` flag for headless Clojure evaluation. `hammock -e '(+ 1 2)'`
  loads every `clj/*.clj` file in the same order as the interactive editor,
  evaluates the expression, prints the result, and exits without touching
  ncurses. Useful for scripting, quick experiments, and inspecting editor
  state from the shell (e.g.
  `hammock -e '(count @hammock.state/*commands*)'`).

### Display

- UTF-8 multi-byte characters are now emitted as a single write, so the
  terminal sees a complete sequence instead of byte fragments split by
  cursor-move escape codes. Non-ASCII glyphs (the logo, Unicode math,
  block-drawing symbols) now render correctly in every cell, not just
  at the start of a run.

### Welcome buffer

- New wider logo (56x28 block-symbol rendering) so the artwork isn't
  vertically stretched by the terminal's 2:1 cell geometry.
- The welcome buffer now shows the current Hammock version on the first
  line.
- Trimmed the "Get started" list to the two most useful entries:
  `browse-symbols` and the `*scratch*` buffer.

### Fixes

- Mode live-reload no longer corrupts memory. The extension pool was
  double-incrementing the NULL-terminator slot and never resetting on
  reload, eventually stomping adjacent static memory after repeated
  config changes.
- `eval-last-sexp` (`C-j`) now pushes an undo snapshot before evaluating,
  so inserted results can be undone like any other edit.
- Changes to the `*editor*` atom now bump `*config-version*`, so live
  reload picks up editor-state tweaks as well as keybinding, command,
  and mode changes.

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

## Version 0.1.1 (2026-04-09)

### Markdown Mode

- Inline math rendering: text between single `$` delimiters is now displayed
  with Unicode substitutions. Superscripts (`^0`..`^9`), subscripts
  (`_0`..`_9`), Greek letters (`\alpha`, `\Omega`, ...), and common
  operators (`\sum`, `\int`, `\infty`, `\leq`, `\to`, ...) render as their
  Unicode equivalents. The `$` delimiters are drawn dim so the math region
  stands out.

## Version 0.1.0 (2026-04-09)

First release of Hammock, a terminal text editor with a minimal C kernel
and a Clojure scripting layer via SCI (compiled as a GraalVM native-image
shared library).

### Architecture

- Two-layer design: functional core (Clojure), imperative shell (C)
- Gap buffer text storage with undo support
- In-process Clojure evaluation via libsci (no TCP/nREPL overhead)
- Effect vector protocol: Clojure commands return data, C executes it
- Live reload: `swap!` on atoms takes effect immediately, no restart needed

### Editing

- Full cursor movement: char, word, line, paragraph, buffer start/end
- Kill/yank with kill ring
- Mark and region (set-mark, kill-region, copy-region)
- Undo
- Incremental search forward and backward (C-s, C-r)
- Self-insert with tab stops
- Evaluate Clojure expressions in *scratch* buffer (C-j)

### Buffer Management

- Multiple buffers with linked-list storage
- Buffer list display (C-x C-b) with mark-for-delete and execute
- Find file (C-x C-f) with filename completion
- Switch buffer (C-x b) with buffer name completion
- Kill buffer (C-x k)
- Save buffer (C-x C-s)

### Window Management

- Horizontal split (C-x 2)
- Vertical split (C-x 3)
- Delete window (C-x 0)
- Delete other windows (C-x 1)
- Switch window (C-x o)

### Modes

- 11 major modes: Fundamental, C, Clojure, Bash, Markdown, Git-Status,
  Shell, Buffer-List, Diff, Grep, Help
- Automatic mode detection from file extension
- Mode-specific keymaps

### Syntax Highlighting

- Per-language tokenizer for C, Clojure, Bash, Markdown, and Diff
- Runs in C for performance

### Git Integration (magit-style)

- Git status buffer (C-x g): stage, unstage, diff, commit, refresh
- File-at-point operations in status buffer
- Diff viewing in split window
- Visit file from status buffer

### Markdown Mode

- Follow links at point (standard and bidirectional `[[ ]]` links)
- Open HTTP links in browser
- Navigate headings (next/previous)
- Jump to next link
- Link navigation history with go-back

### Shell

- PTY-based interactive shell in a buffer
- Shell command execution (M-!)

### Introspection

- Describe key (F1 k / C-h k): show what command a key runs
- Describe function (F1 f / C-h f): show command docstring
- View news (F1 n / C-h n): show release notes
- M-x with command name completion
- Find definition (M-.)
- Recursive grep (M-g)

### Keybindings

- Emacs-compatible default keybindings
- Prefix keys: C-x, F1, C-h
- Mode-specific overrides for Git-Status, Markdown, Diff, Grep, Buffer-List
