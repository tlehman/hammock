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

(declare build-c-index build-commands-index)

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
(defn- build-c-index [_root] {})
(defn- build-commands-index [_clojure-index] [])
