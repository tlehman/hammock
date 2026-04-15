;; clj/loadup.clj — bootstrap manifest for Hammock's Clojure layer.
;; Inspired by Emacs lisp/loadup.el, but SCI lacks `load-file`, so this file
;; just exports the load order as data. main.c (and test/smoke.c) read the
;; vector via `sci_eval` and then `sci_load_file` each entry in order.
;;
;; Order matters: each file may declare symbols referenced by later files.
;; Adding a new Clojure file is a pure Clojure change: add it here.
(ns hammock.loadup)

(def files
  ["clj/state.clj"
   "clj/effects.clj"
   "clj/core.clj"
   "clj/git.clj"
   "clj/markdown.clj"
   "clj/symbols.clj"
   "clj/commands.clj"
   "clj/keybindings.clj"
   "clj/syntax-modes.clj"
   "clj/modes.clj"
   "clj/perf.clj"])
