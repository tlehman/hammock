# Hammock NEWS -- history of user-visible changes.

## v0.2.4

- In Clojure mode, typing `)`, `]`, or `}` briefly highlights the
  matching opener for one second (string/comment/char-literal aware).
  When the match is off-screen, its line is echoed in the minibuffer.
  No flash if no match is found.
- **`C-j` (`eval-last-sexp`) now persists state changes.** `(def x 42)`
  and `(ns foo)` typed into `*scratch*` used to vanish on return: the
  v0.2.3 interruptible eval `fork()`ed a child to run SCI so `C-g`
  could `SIGKILL` it, but that also meant every side effect lived in
  the child's copy-on-write isolate and was discarded on `_exit`.
  `sci_eval_interruptible` in `src/sci.c` now runs the eval on a
  pthread attached to the main GraalVM isolate via
  `graal_attach_thread`, so `ns`/`def`/atom swaps survive the call.
  `C-g` no longer kills the eval (GraalVM native-image doesn't expose a
  safe thread interrupt, and aborting SCI mid-eval would leave the
  isolate in an inconsistent state); instead it detaches the UI from
  the running job and reports `Quit`. A subsequent `C-j` blocks at
  `isolate_mu` until the abandoned worker finishes, so a runaway user
  loop still pins the editor — this is the tradeoff for persistent
  state. Most interactive evals finish in microseconds and this is
  invisible.
- **`(ns foo)` inside `sci_eval` now actually registers the namespace.**
  `hammock -e '(do (prn (count (all-ns))) (ns foobar) (prn (count (all-ns))))'`
  used to print the same count twice: SCI only processes `ns` as a
  namespace-registering form when it's at the top level, but
  `SciLib.sciEvalString` was wrapping every eval in
  `(binding [*out* ... *err* ...] (do <code>))` to give `prn`/`println`
  a bound writer, demoting every user form out of top-level position.
  The wrapper is gone; instead, `sciInit` installs the capture writer
  as the root value of `clojure.core/*out*`/`*err*` once, by calling
  `sci.core/alter-var-root` from the host side (which sets
  `sci.impl.unrestrict/*unrestricted* true` and bypasses the built-in
  read-only guard). User code is now passed verbatim to `eval-string*`,
  so `ns`, `:require`, `:refer`, and other top-level-only side effects
  work regardless of nesting.

## Version 0.2.3 (2026-04-12)

### Input

- **Arrow keys, Home/End, Delete, PgUp/PgDn, F1, and modified arrows
  (Meta+/Ctrl+/Ctrl-Meta+) now move the cursor.** `input_read()` bypasses
  ncurses' keypad decoder because it reads raw stdin to intercept SGR
  mouse sequences and then feeds bytes back via `ungetch()` -- and
  ungetch'd bytes never trigger escape reassembly. A new
  `try_parse_vt100()` decoder in `src/input.c` handles both the CSI
  (`\033[A`) and SS3 (`\033OA`, emitted in application-cursor-keys mode)
  forms directly from the raw buffer before anything else.

### Evaluation

- **`sci_eval` errors now route to `*Messages*` and the minibuffer
  instead of being silently swallowed or masquerading as values.**
  `libsci.dylib` returns `sci_eval error: <msg>` strings rather than
  printing to stderr (which corrupted the ncurses display) or returning
  `"nil"` (which looked like a value). `cmd_eval_last_sexp` in
  `src/command.c` detects the prefix, calls `message()`, and no longer
  inserts error text as `; => ERROR: ...` in `*scratch*`. The Clojure
  command dispatch path in `command_dispatch` applies the same check
  before handing results to `effects_execute`.
- **`C-g` cancels a long-running `sci_eval`.** New
  `sci_eval_interruptible()` in `src/sci.c` forks a child that owns a
  CoW copy of the SCI isolate, polls both the result pipe and stdin,
  and `SIGKILL`s the child on `C-g` or `ESC`. The parent's isolate is
  untouched, so session state survives cancellation. Used by
  `eval-last-sexp` (`C-j`); paints `"Evaluating... (C-g to cancel)"`
  while the child runs.

### Markdown

- **`[[wikilink]]` visits jump to the target file immediately, without
  corrupting the source buffer.** The effect sequence emitted by
  `markdown-follow-link` had `buffer-load-file` running before
  `buffer-switch`, so the file content loaded into the buffer the user
  was *leaving* (e.g. README.md got CLAUDE.md's bytes under the name
  "README.md") and the user landed on an empty buffer. Reordered to
  `[buffer-create, buffer-switch, buffer-load-file]`, and added an
  already-in-buffer-list short-circuit that just switches, so re-visiting
  a link reuses the existing buffer instead of clobbering in-progress
  edits or creating duplicates. Same fix applied to `find-file-cb` (which
  was also missing a `buffer-switch` entirely) and `git-visit-file`.

### Kill ring

- **`M-y` cycles back through the kill ring after `C-y`.** The
  existing 16-slot `KillRing` in `src/util.c` was already fed by
  `kill-line`, `kill-region`, and `kill-ring-save`, but `yank` only
  ever read the most recent entry. A new `kill_ring_nth(kr, offset)`
  lets `:yank-pop` walk backward. Static yank state in `src/effects.c`
  (`yank_state_buf`, `yank_state_start`, `yank_state_end`,
  `yank_state_offset`) records where and how much `:yank` inserted;
  `:yank-pop` deletes that span and replaces it with the next-older
  kill, advancing the offset. Any other mutating effect (`:insert`,
  `:delete-forward`, `:delete-backward`, `:kill-region`, `:kill-line`,
  `:copy-region`, `:undo`, `:buffer-set-contents`, `:buffer-switch`,
  `:buffer-load-file`, `:buffer-append-text`) plus the hot-path
  `self_insert` in `main.c` calls `yank_state_invalidate()`, so
  `M-y` reports "Previous command was not a yank" exactly when Emacs
  would.
- **`M-<Backspace>` kills the word before point.** New Clojure
  command `backward-kill-word` composes existing effects:
  `[[:set-mark] [:point-backward-word] [:kill-region]]`. The kill
  lands in the ring (and the system clipboard), so `C-y` / `M-y`
  can recall it. Two smaller plumbing fixes were needed: (1) the
  `src/input.c` M-Backspace handler used to strip `MOD_META` to
  work around spurious ESC+127 on old terminals. That strip is
  gone, so `M-Backspace` is now distinguishable from plain
  `Backspace`. (2) `parse-key-spec` in `clj/keybindings.clj` did
  not handle `M-<special>` (it would decode "M-Backspace" as
  `M-B`). It now recognizes Backspace/Delete/Up/Down/Left/Right/
  Home/End/PgUp/PgDn/Tab/Enter with a modifier prefix.

### Git

- **`git log` view: `l` in `*git-status*` opens a read-only
  `*git-log*` buffer with recent commits** (`git log --oneline -50`).
  The buffer lives in a new `Git-Log` mode (id 14 in `clj/modes.clj`)
  with `g` to refresh and `q` to close. The `git/git-log` helper in
  `clj/git.clj` had been dormant since v0.1.0; it finally has a
  caller.
- **`git fetch`/`pull`/`push` from the status buffer.** `f` fetches,
  `F` pulls, `P` pushes (Magit's single-key convention) under
  `mode:git-status`. A new `exec-out-err` helper in `clj/git.clj`
  concatenates `:out` and `:err` from `shell/exec` because git writes
  network progress to stderr. The combined message surfaces in the
  minibuffer and `*Messages*`, then `*git-status*` refreshes via
  `git-status-effects`, matching the `git-commit-cb` pattern.
- **`q` now actually quits git mode.** `git-quit` previously just
  switched away from `*git-status*` and left it in the buffer list.
  It now switches to the first non-git buffer (fallback `*scratch*`),
  collapses any diff split via `window-delete-others`, and destroys
  `*git-status*`, `*git-diff*`, and `*git-log*`. The
  switch-before-destroy order is mandatory because `buffer-destroy`
  (`src/effects.c:469`) silently skips the current buffer.

## Version 0.2.2 (2026-04-12)

### Lisp-authoritative architecture

- **Mode-enum drop.** The compile-time `MajorModeID` enum in `src/mode.h`
  is gone. `Buffer.major_mode` (int) is now `Buffer.mode_name`
  (const char *). The mode registry in `src/mode.c` is a string-keyed
  linear-scan array rebuilt from `clj/modes.clj` on live reload.
  Adding a new mode is now a pure Clojure change: no C enum to extend.

- **Keybindings are 100% Clojure-owned.** The 98-line
  `keybindings_init()` function (hardcoded C key bindings) has been
  deleted. All keybindings load exclusively via
  `keybindings_load_edn()` from `clj/keybindings.clj`. The C keymap
  structs are a read-only cached snapshot populated by Clojure. SCI is
  now required for normal operation.

- **Per-domain snapshot protocol.** `clj/state.clj` now tracks
  per-domain version counters (`:keymaps`, `:modes`, `:commands`,
  `:windows`) via `*config-versions*`. The main loop checks the global
  `*config-version*` as a fast poll, then fetches per-domain versions
  to rebuild only changed snapshots. Each rebuild is wrapped in a perf
  probe (`snapshot-rebuild:keymaps`, `snapshot-rebuild:modes`).

### Perf

- Zero keystroke-latency regression across all changes. Bench results
  (p50 within noise floor of v0.2.1 baseline):
  forward-char 0 ns, next-line 11-14 µs, beginning-of-line 0 ns.

## Version 0.2.1 (2026-04-12)

### Perf harness

- New perf subsystem (`src/perf.c`, `src/perf.h`) with three modes that
  share a single EDN output shape and a single pair of probes around
  `command_dispatch` in `src/command.c`. When disabled, each probe is a
  one-branch no-op (~1 ns). Monotonic clock via `clock_gettime`.
- **Bench mode**: `hammock --bench perf/scripts/<file>.edn` loads SCI
  headlessly (no ncurses), walks a flat vector of directive sub-vectors
  (`[:fixture ...]`, `[:warmup N]`, `[:iterations N]`, `[:case "label"
  "command"]`), warms up, times each case, and writes aggregated
  min/p50/p90/p99/max/mean ns to `perf/runs/<ts>-<host>.edn`. Runner
  lives in `run_bench` in `src/main.c`.
- **PTY mode**: new `test/pty_bench.c` external driver (built via
  `make pty-bench`) forks `./hammock` under a pseudo-terminal, feeds a
  plain-text script of `wait-for` / `send` / `measure` directives,
  and times by the 5 ms idle-window method. Useful for catching
  regressions that cross ncurses, which the bench mode skips.
- **Ambient mode**: `HAMMOCK_PERF=/path/to/log.edn ./hammock` streams
  one `{:label ... :ns ...}` map per dispatched command to the log
  file, capped at 50k samples. `(hammock.perf/summarize "/path")`
  aggregates the stream into the same shape as bench mode.
- New `clj/perf.clj` namespace: `load-run`, `diff-runs`, `report`
  (color-coded before/after table), `summarize`. Lives under SCI and
  uses `hammock.shell/exec` + `read-string` since `slurp` and
  `clojure.edn` are not available in our native-image SCI build.

### Harness scripts and fixtures

- Generated fixtures under `perf/fixtures/` (not committed): small
  (100 lines), medium (10k), large (100k). Built by `make perf-fixtures`.
- Committed scripts under `perf/scripts/`: `keystroke-latency.edn`,
  `cursor-macro.edn`, `dispatch-mix.edn`, plus stubs for
  `snapshot-rebuild.edn` and `live-reload.edn` which v0.2.2 will fill
  in once the window/keymap snapshot protocol lands.

### Makefile

- New phony targets: `perf-fixtures`, `perf-run`, `perf-baseline`,
  `perf-diff`, `pty-bench`, `perf-pty`. `perf-baseline` tags output
  files as `perf/baselines/v<version>-<host>-<script>.edn` and is
  meant to be run once per host and committed.

### EDN parser

- `src/effects.c` now skips `;` line comments inside `skip_whitespace`,
  matching the EDN spec. Previously the parser rejected any input with
  comments, which blocked perf scripts from having headers.

### Help text

- `hammock --help` now advertises `--bench PATH` and the `HAMMOCK_PERF`
  env var alongside the existing `-e EXPR` / `-v` / `-h` flags.

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
