# Hammock editor

![logo](./logo.png)

This is an editor inspired by Rich Hickey's [Hammock Driven Development](https://www.youtube.com/watch?v=f84n5oFoZBc&t=53).

It's a Clojure-oriented editor inspired by Emacs, but it's not Emacs.

It's a Github-flavored-markdown-oriented editor. 

The *scratch* buffer is modeled after the Emacs Lisp scratch buffer, but it runs Clojure (evaluated by [SCI](https://github.com/babashka/sci) in-process via a GraalVM native-image shared library).

## Architecture

Hammock follows a **minimal C kernel + Clojure scripting** architecture. The C kernel handles only performance-critical primitives: ncurses display, gap buffer text storage, key input, in-process SCI calls, and cursor movement. Everything else lives in Clojure: commands, keybindings, modes, state, git integration, markdown navigation, and buffer management.

Commands defined in Clojure return **effect vectors**: data describing what C should do, rather than doing it directly. This is the "functional core, imperative shell" pattern. Clojure atoms manage editor state with `swap!` and `reset!`. Clojure commands that share a name with a C command automatically override it at startup.

See [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md) for the full design with diagrams.

## Building

```bash
nix develop        # enter dev shell with GraalVM (first time only)
make -C libsci     # build libsci.dylib (requires GraalVM native-image)
make               # builds ./hammock binary
make clean         # removes build/ dir and binary
```

Requires: C11 compiler, ncurses, GraalVM CE with native-image (provided by `nix develop`).

## How It Works

On startup, Hammock:
1. Initializes the C kernel (ncurses, input)
2. Loads Clojure modules (`clj/*.clj`) via in-process SCI (GraalVM native-image shared library)
3. Fetches keybinding and mode tables from Clojure as EDN
4. Enters the main loop

Each keystroke is dispatched through the keymap. Hot-path commands (cursor movement, self-insert) execute directly in C for zero latency. All other commands go through Clojure: C pushes a state snapshot (including current line text and buffer list) to the `*editor*` atom, evaluates the command via SCI, and executes the returned effect vector. Git, markdown, and buffer-list commands all run in Clojure.

Users can modify editor behavior live via `C-j` in the scratch buffer.

## TODO 
- [ ] feature: hammock source, load so M-. will work
- [ ] feature: add a namespace/symbol explorer, so you can navigate through all the namespaces and symbols and finally jump to definition (is apropos this?)
- [ ] feature: in markdown mode, math between dollar signs should render with Unicode in text mode, so $x^2$ shows as $x²$ but the dollar signs are a darker color
- [ ] feature: M-<backspace> deletes who
- [ ] feature: in *scratch* buffer, format all the outputted source code 
- [ ] feature: tab and code indenting correctly
- [ ] feature: add mermaidjs tui-rendering for markdown mode
- [ ] feature: add git fetch/push/pull to git mode
- [ ] feature: make q quit the git mode main buffer
- [ ] feature: interactive vs non-interactive fns
- [ ] feature: LSP support for other languages
- [ ] feature: vim-mode
- [ ] feature: Make a SQLite and CSV grid editor
- [ ] feature: add roam/obsidian style `[[ ]]` bidirectional linking for markdown
- [ ] feature: move git/markdown commands fully to Clojure (add query effects for buffer content access)
- [ ] feature: move isearch to Clojure (like Emacs isearch.el)
- [ ] feature: Add comprehensive list of features to README.md when the above tasks are done
- [ ] tests: performance tests for loading large files (consider a related approach to https://arxiv.org/abs/2004.02504)

## Dependencies
- [libsci](https://github.com/babashka/sci) Configurable Clojure/Script interpreter suitable for scripting and Clojure DSLs
