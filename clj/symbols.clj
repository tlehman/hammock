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

(declare build-commands-index)

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

;; Stub filled in by a later task.
(defn- build-commands-index [_clojure-index] [])
