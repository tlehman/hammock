# Hammock NEWS -- history of user-visible changes.

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
