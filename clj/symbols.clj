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
