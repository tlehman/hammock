# Hammock Architecture

## Overview

Hammock is a terminal text editor with a two-layer architecture: a minimal C kernel for performance-critical operations, and a Clojure scripting layer (via SCI compiled as a GraalVM native-image shared library) for editor logic. The two layers communicate via in-process function calls through libsci.

## Layer Diagram

```
+-----------------------------------------------------------+
|                  Clojure Layer (SCI / libsci)              |
|                                                           |
|  +---------------+  +-----------------+  +--------------+ |
|  | state.clj     |  | commands.clj    |  | keybinds.clj | |
|  |               |  |                 |  |              | |
|  | *editor*      |  | 45+ commands    |  | global keys  | |
|  | *commands*    |  | dispatch fn     |  | C-x prefix   | |
|  | *keybinds*    |  | git commands    |  | F1 prefix    | |
|  | *modes*       |  | buflist cmds    |  | mode keys    | |
|  |               |  | markdown cmds   |  | export fn    | |
|  +---------------+  +-----------------+  +--------------+ |
|                                                           |
|  +---------------+  +-----------------+  +--------------+ |
|  | effects.clj   |  | modes.clj       |  | git.clj      | |
|  |               |  |                 |  | markdown.clj | |
|  | constructors  |  | 11 major modes  |  | core.clj     | |
|  | state access  |  |                 |  |              | |
|  +---------------+  +-----------------+  +--------------+ |
+-----------------------------------------------------------+
                            |
                    in-process calls
                     (libsci / SCI)
                            |
                            v
+-----------------------------------------------------------+
|                       C Kernel                            |
|                                                           |
|  +---------------+  +-----------------+  +--------------+ |
|  | main.c        |  | effects.c       |  | buffer.c     | |
|  |               |  |                 |  |              | |
|  | main loop     |  | EDN parser      |  | gap buffer   | |
|  | startup       |  | effect exec     |  | point/mark   | |
|  | signal hdl    |  | state snapshot  |  | undo system  | |
|  | mouse events  |  |                 |  | file I/O     | |
|  +---------------+  +-----------------+  +--------------+ |
|                                                           |
|  +---------------+  +-----------------+  +--------------+ |
|  | display.c     |  | input.c         |  | keymap.c     | |
|  |               |  |                 |  |              | |
|  | ncurses       |  | raw stdin       |  | keymap bind  | |
|  | syntax hl     |  | key events      |  | prefix keys  | |
|  | modeline      |  | mouse events    |  | EDN loader   | |
|  | minibuffer    |  |                 |  |              | |
|  +---------------+  +-----------------+  +--------------+ |
|                                                           |
|  +---------------+  +-----------------+  +--------------+ |
|  | sci.c         |  | window.c        |  | mode.c       | |
|  |               |  |                 |  |              | |
|  | GraalVM init  |  | split tree      |  | mode detect  | |
|  | sci_eval()    |  | geometry        |  | EDN loader   | |
|  | sexp extract  |  | scrolling       |  |              | |
|  +---------------+  +-----------------+  +--------------+ |
|                                                           |
|  +---------------+  +-----------------+  +--------------+ |
|  | command.c     |  | shell.c         |  | syntax.c     | |
|  |               |  |                 |  |              | |
|  | cmd registry  |  | PTY mgmt       |  | per-language  | |
|  | minibuffer    |  | shell I/O       |  | tokenizer    | |
|  | completion    |  |                 |  |              | |
|  +---------------+  +-----------------+  +--------------+ |
|                                                           |
|  +---------------+  +-----------------+                   |
|  | git.c         |  | markdown.c      |                   |
|  |               |  |                 |                   |
|  | status parse  |  | link parsing    |                   |
|  | git commands  |  |                 |                   |
|  +---------------+  +-----------------+                   |
+-----------------------------------------------------------+
```

## IPC: C and Clojure Communication

### Channel
- Transport: in-process function calls via libsci (SCI compiled as GraalVM native-image shared library)
- No network: SCI runs in-process via a GraalVM isolate, no TCP/bencode/nREPL overhead
- State: Clojure atoms persist in the GraalVM isolate for the lifetime of the process

### Startup Sequence

```
main()
  |
  +-- commands_init(), shell_commands_init()  # register C commands
  +-- display_init(), input_init()            # ncurses up
  |
  +-- sci_init()                              # create GraalVM isolate
  |     |
  |     +-- graal_create_isolate()
  |     +-- libsci_init()
  |
  +-- sci_load_file clj/state.clj             # atoms defined
  +-- sci_load_file clj/effects.clj           # effect constructors
  +-- sci_load_file clj/git.clj               # git utilities
  +-- sci_load_file clj/markdown.clj          # markdown utilities
  +-- sci_load_file clj/commands.clj          # 45+ commands registered
  +-- sci_load_file clj/keybindings.clj       # keybinding table
  +-- sci_load_file clj/modes.clj             # mode definitions
  +-- sci_load_file clj/core.clj              # user API
  |
  +-- keybindings_load_edn(export)            # populate C keymap
  +-- modes_load_edn(export)                  # populate C mode table
  +-- export-command-metadata -> register      # Clojure cmds in C table
  +-- install-watches!                         # live reload via atoms
  |
  +-- create *scratch* buffer
  +-- enter main loop
```

### Main Loop: Command Dispatch

All commands go through a single unified `command_dispatch()` function. Each
command in the registry carries a `CommandDispatch` tag (`CMD_C_NATIVE` or
`CMD_CLOJURE`) that determines execution path:

```
+--------------------+
|  input_read()      |
|  (poll on stdin)   |
+--------+-----------+
         |
         v
+--------------------+     +-----------------------+
| keymap_lookup()    |---->| prefix key?           |
| (mode, then global)|     | wait for next key     |
+--------+-----------+     +-----------------------+
         |
         v
+--------------------+
| command found?     |
+----+----------+----+
     |yes       |no
     v          v
+--------------------+  +---------------+
| command_dispatch() |  | self-insert   |
|                    |  | or shell PTY  |
| entry->dispatch:   |  +---------------+
|  CMD_C_NATIVE:     |
|    fn()            |
|  CMD_CLOJURE:      |
|    snapshot + SCI   |
|    + effects_exec  |
+--------------------+
```

Between iterations, the main loop also checks `sci_get_state_version()` to
detect atom changes (live reload) and calls `shell_read_all()` for PTY I/O.

### Introspection

Hammock provides Emacs-like introspection via the F1 prefix:

- `describe-key` (F1 k): press a key to see what command it runs
- `describe-function` (F1 f): enter a command name to see its docstring

Every command carries metadata: name, docstring, source ("C" or "Clojure"),
and dispatch type. Keybindings support reverse lookup (command to keys).

### Effect Vector Protocol

Clojure commands return EDN vectors describing what C should do:

```clojure
;; A command is a function that returns an effect vector
(defcommand "forward-char"
  (fn [] [[:point-forward 1] [:reset-target-col]]))

(defcommand "kill-line"
  (fn [] [[:kill-line]]))

(defcommand "find-file"
  (fn [] [[:prompt "Find file: " "hammock.commands/find-file-cb" :file]]))
```

C parses the EDN and executes each effect in sequence. There are ~45 primitive effect types covering: point movement, text mutation, mark/region, kill/yank, undo, buffer management, window management, display, scrolling, search, prompts, shell, clipboard, and lifecycle.

### State Management with Atoms

```clojure
;; C pushes a metadata snapshot before each Clojure command
(defonce *editor* (atom {}))
;; Shape: {:current-buffer "*scratch*" :point 42 :length 200
;;         :mark 0 :mark-active false :modified false
;;         :read-only false :mode "Clojure" :filename nil
;;         :window-count 1 :top-line 0 :visible-rows 24
;;         :current-line "text at point" :line-number 1 :col 0
;;         :buffers [{:name "x" :size N :modified bool
;;                    :read-only bool :mode "X" :filename "path"} ...]}

;; Command registry: name -> fn returning effect vector
(defonce *commands* (atom {}))

;; Keybinding table: exported as EDN for C keymap at startup
(defonce *keybindings* (atom {}))

;; Mode definitions: exported as EDN for C mode table at startup
(defonce *modes* (atom {}))
```

Key design: C owns the authoritative buffer text (gap buffer). Clojure receives metadata (point, mark, length, mode, modified), the current line text, and a buffer list with per-buffer metadata. This avoids serializing megabytes of text over nREPL on every keystroke while giving Clojure enough context for git, markdown, and buffer-list commands.

### Interactive Commands: Prompt Protocol

Commands needing user input return a `:prompt` effect:

```
Clojure                          C
-------                          -
find-file called
  |
  +--> [[:prompt "Find file: "
          "hammock.commands/find-file-cb"
          :file]]
                                 |
                          minibuffer_read()
                          user types path
                                 |
  find-file-cb("src/main.c") <--+
  |
  +--> [[:buffer-create "main.c"]
        [:buffer-load-file "src/main.c"]]
                                 |
                          effects_execute()
```

### Performance: Unified Dispatch

All commands go through `command_dispatch()`. Commands registered with
`CMD_C_NATIVE` execute as C function pointers (~0ms). Commands registered with
`CMD_CLOJURE` go through in-process SCI evaluation. The dispatch type is a
per-command property, not a separate fast-path array.

Commands with their own input loops (isearch, eval-last-sexp, minibuffer,
find-definition, rgrep) and performance-critical primitives (cursor movement,
deletion, scrolling) are implemented in C. Higher-level editor logic (git mode,
buffer list, markdown navigation) lives in Clojure. C commands take precedence
over Clojure commands with the same name (Clojure commands only register if no
C command exists with that name).

## File Map

### C Kernel (`src/`)

| File | Purpose | Lines |
|------|---------|-------|
| `main.c` | Main loop, startup, mouse, signal handling | ~520 |
| `effects.c` | EDN parser, effect executor, state snapshots | ~810 |
| `buffer.c` | Gap buffer: insert, delete, search, file I/O | ~410 |
| `window.c` | Window splits, geometry, scrolling | ~350 |
| `display.c` | ncurses rendering, syntax color, modeline | ~310 |
| `input.c` | Raw terminal input, key/mouse event parsing | ~220 |
| `keymap.c` | Keymap bind/lookup, EDN loader | ~340 |
| `command.c` | Unified command registry, minibuffer, completion, C commands | ~1300 |
| `mode.c` | Mode table, detection, EDN loader | ~280 |
| `sci.c` | GraalVM isolate, SCI eval, sexp extraction | ~200 |
| `syntax.c` | Per-language tokenizer (C, Clojure, Bash, Markdown, Diff, Help) | ~820 |
| `shell.c` | PTY management, shell I/O | ~210 |
| `git.c` | Git status parsing, stage/unstage/commit/diff utilities | ~230 |
| `markdown.c` | Markdown link parsing at point (bidir and standard links) | ~80 |
| `util.c` | Memory, kill ring, undo, string utilities | ~210 |

### Clojure Layer (`clj/`)

| File | Namespace | Purpose |
|------|-----------|---------|
| `state.clj` | `hammock.state` | Atom definitions, config version counter, atom watches |
| `effects.clj` | `hammock.effects` | Effect vector constructors, state accessors |
| `commands.clj` | `hammock.commands` | All commands: movement, editing, git, buflist, markdown |
| `keybindings.clj` | `hammock.keybindings` | Keybinding table with export to EDN |
| `modes.clj` | `hammock.modes` | Mode definitions with export to EDN |
| `git.clj` | `hammock.git` | Git operations via `hammock.shell/exec`, status parsing |
| `markdown.clj` | `hammock.markdown` | Link-at-point parsing, headings, TOC |
| `core.clj` | `hammock.core` | User-facing API, version, startup message |
