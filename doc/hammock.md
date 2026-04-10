# Hammock Editor

A Clojure-oriented text editor inspired by Emacs, using SCI (Small Clojure Interpreter) compiled as a GraalVM native-image shared library.

## Getting Started

Open a file: `./hammock filename.txt`
Open scratch buffer: `./hammock`

## Key Bindings

### Movement

| Key       | Command              |
|-----------|----------------------|
| C-f       | Forward character    |
| C-b       | Backward character   |
| C-n       | Next line            |
| C-p       | Previous line        |
| C-a       | Beginning of line    |
| C-e       | End of line          |
| M-f       | Forward word         |
| M-b       | Backward word        |
| M-} / M-e | Forward paragraph   |
| M-{ / M-a | Backward paragraph  |
| M-<       | Beginning of buffer  |
| M->       | End of buffer        |
| C-v       | Scroll down          |
| M-v       | Scroll up            |
| Arrow keys | Movement            |
| Home/End  | Beginning/end of line |
| PgUp/PgDn | Scroll up/down       |

### Editing

| Key       | Command              |
|-----------|----------------------|
| C-d       | Delete forward       |
| Delete    | Delete forward       |
| Backspace | Delete backward      |
| C-k       | Kill line            |
| C-SPC     | Set mark             |
| C-w       | Kill region          |
| M-w       | Copy region          |
| C-y       | Yank (paste)         |
| C-/       | Undo                 |
| C-u       | Undo                 |

### Files and Buffers

| Key       | Command              |
|-----------|----------------------|
| C-x C-s   | Save buffer          |
| C-x C-f   | Find (open) file     |
| C-x b     | Switch buffer        |
| C-x C-b   | List buffers         |
| C-x k     | Kill buffer          |
| C-x C-c   | Quit                 |

### Windows

| Key       | Command              |
|-----------|----------------------|
| C-x 2     | Split below          |
| C-x 3     | Split right          |
| C-x 0     | Delete window        |
| C-x 1     | Delete other windows |
| C-x o     | Other window         |

### Search and Navigation

| Key       | Command              |
|-----------|----------------------|
| C-s       | Incremental search forward  |
| C-r       | Incremental search backward |
| M-.       | Find definition      |
| M-,       | Pop mark (go back)   |
| M-g       | Recursive grep       |
| C-g       | Cancel / Quit        |

### Commands and Shell

| Key       | Command              |
|-----------|----------------------|
| M-x       | Execute command by name |
| M-!       | Run shell command    |

### Clojure

| Key       | Command              |
|-----------|----------------------|
| C-j       | Eval last sexp       |

### Help

| Key       | Command              |
|-----------|----------------------|
| F1 k      | Describe key         |
| F1 f      | Describe function    |

### Git (in git-status buffer)

| Key       | Command              |
|-----------|----------------------|
| C-x g     | Open git status      |
| s         | Stage file           |
| u         | Unstage file         |
| c         | Commit               |
| d         | Show diff            |
| g         | Refresh              |
| q         | Quit git mode        |
| Tab       | Next section         |
| Enter     | Visit file           |

### Diff Mode

| Key       | Command              |
|-----------|----------------------|
| q         | Quit diff            |

### Markdown Mode

| Key       | Command              |
|-----------|----------------------|
| Enter     | Follow link          |
| Tab       | Next link            |
| n         | Next heading         |
| p         | Previous heading     |
| l         | Go back              |

### Buffer List Mode

| Key       | Command              |
|-----------|----------------------|
| Enter     | Visit buffer         |
| D         | Mark for deletion    |
| x         | Execute deletions    |
| q         | Quit buffer list     |

### Grep Mode

| Key       | Command              |
|-----------|----------------------|
| Enter     | Visit match          |
| q         | Quit grep            |
| g         | Refresh              |

## Modes

Hammock automatically detects the major mode from file extensions:

| Extension                | Mode     |
|--------------------------|----------|
| .c, .h, .cc, .cpp, .hpp | C        |
| .clj, .cljs, .cljc, .edn, .bb | Clojure |
| .sh, .bash, .zsh        | Bash     |
| .md, .markdown           | Markdown |

Special modes (not file-based): Fundamental, Git-Status, Shell, Buffer-List, Diff, Grep, Help.

## Clojure Integration

The *scratch* buffer runs [SCI](https://github.com/babashka/sci) Clojure (embedded via GraalVM native-image). Type an expression and press C-j to evaluate it.

All Clojure data structures work:

```clojure
; Lists
(+ 1 2 3)

; Vectors
[1 2 3]

; Maps
{:name "Hammock" :version "0.1.0"}

; Sets
#{1 2 3}

; Atoms (state persists across evaluations)
(def counter (atom 0))
(swap! counter inc)
@counter
```

## Links

Markdown mode supports two link styles:

- Standard: [Babashka](https://babashka.org)
- Bidirectional: [[another-note]]

Press Enter on a link to follow it. Press `l` to go back.

## Shell Mode

Run `M-x shell` to start an interactive shell in a buffer.

## Dependencies

- [GraalVM CE](https://www.graalvm.org/) with native-image (provided by `nix develop`)
- ncurses
- C11 compiler
