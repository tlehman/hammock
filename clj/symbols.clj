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

(defn- build-commands-index [clojure-index]
  (let [cmd-index (->> (get clojure-index "hammock.commands" [])
                       (filter #(= :cmd (:kind %)))
                       (map (juxt :name identity))
                       (into {}))]
    (->> @state/*commands*
         (map (fn [[nm entry]]
                (let [doc (if (map? entry) (:doc entry) "")
                      located (get cmd-index nm)]
                  {:kind :cmd
                   :name nm
                   :namespace "commands"
                   :file (or (:file located) "")
                   :line (or (:line located) 0)
                   :doc (or doc "")})))
         (sort-by :name)
         vec)))

;; ---- Renderers ----

(defn- truncate [s n]
  (if (<= (count s) n) s (str (subs s 0 (max 0 (- n 1))) "…")))

(defn- kind-tag [k]
  (case k
    :cmd      "cmd "
    :defn     "defn"
    :def      "def "
    :defonce  "onc "
    :defmacro "mac "
    :fn       "fn  "
    :c-macro  "#def"
    :typedef  "typ "
    :struct   "str "
    "    "))

(defn- pad-right [s n]
  (let [s (truncate s n)]
    (str s (apply str (repeat (max 0 (- n (count s))) \space)))))

(def ^:private no-source-line
  "  (no source tree found — set HAMMOCK_SOURCE)\n")

(defn format-namespace-pane
  "Return the text for the left pane: three roots with their namespaces/modules
  and symbol counts."
  []
  (let [idx (ensure!)
        header "  Namespaces and modules (Enter to drill in)\n\n"
        sep   "\n"
        clj-lines (if (nil? (:source-root idx))
                    [no-source-line]
                    (for [[ns syms] (:namespaces idx)]
                      (format "  %s (%d)\n" (pad-right ns 28) (count syms))))
        c-lines   (if (nil? (:source-root idx))
                    [no-source-line]
                    (for [[path syms] (:modules idx)]
                      (format "  %s (%d)\n" (pad-right path 28) (count syms))))
        cmd-line  (format "  %s (%d)\n"
                          (pad-right "(all)" 28) (count (:commands idx)))]
    (str header
         "Clojure\n" (apply str clj-lines) sep
         "C\n" (apply str c-lines) sep
         "Commands\n" cmd-line)))

(defn- format-symbol-line [sym]
  (let [tag  (kind-tag (:kind sym))
        nm   (pad-right (:name sym) 28)
        info (let [d (:doc sym)]
               (if (str/blank? d) "(no docstring)" d))]
    (format "  [%s] %s — %s\n" tag nm (truncate info 120))))

(defn format-symbol-pane
  "Return the text for the right pane given a selected namespace/module name."
  [selector]
  (let [idx (ensure!)
        syms (cond
               (= selector "(all)")          (:commands idx)
               (contains? (:namespaces idx) selector)
               (get-in idx [:namespaces selector])
               (contains? (:modules idx) selector)
               (get-in idx [:modules selector])
               :else nil)]
    (cond
      (nil? syms)  (str "  (unknown: " selector ")\n")
      (empty? syms) "  (empty)\n"
      :else (apply str (map format-symbol-line syms)))))

(defn all-symbols-flat
  "Return a flat vector of every indexed symbol."
  []
  (let [idx (ensure!)]
    (concat (mapcat val (:namespaces idx))
            (mapcat val (:modules idx))
            (:commands idx))))

(defn apropos-match
  "Return symbols whose name or docstring contains `pattern` (case-insensitive)."
  [pattern]
  (let [needle (str/lower-case (or pattern ""))]
    (->> (all-symbols-flat)
         (filter (fn [{:keys [name doc namespace]}]
                   (or (str/includes? (str/lower-case (or name "")) needle)
                       (str/includes? (str/lower-case (or doc "")) needle)
                       (str/includes? (str/lower-case (or namespace "")) needle))))
         (sort-by (fn [s] [(:namespace s) (:name s)]))
         vec)))

(defn format-apropos
  "Return text for the *Apropos* buffer for a given pattern."
  [pattern]
  (let [hits (apropos-match pattern)]
    (if (empty? hits)
      (str "  No matches for \"" pattern "\"\n")
      (apply str
             (format "  Apropos: %s  (%d matches)\n\n" pattern (count hits))
             (map (fn [sym]
                    (format "  [%s] %s/%s — %s\n"
                            (kind-tag (:kind sym))
                            (pad-right (:namespace sym) 24)
                            (pad-right (:name sym) 24)
                            (truncate (let [d (:doc sym)]
                                        (if (str/blank? d) "(no docstring)" d))
                                      60)))
                  hits)))))

(defn name-at-line
  "Parse the symbol name out of a line rendered by format-symbol-line.
  Format: `  [kind] <name padded to 28> — <info>`."
  [line]
  (let [s (or line "")]
    (when (and (>= (count s) 40)
               (str/starts-with? s "  ["))
      (str/trim (subs s 9 37)))))

(defn namespace-at-line
  "Parse the namespace name out of a line rendered by format-namespace-pane.
  Format: `  <name padded to 28> (N)`."
  [line]
  (let [s (or line "")]
    (when (and (>= (count s) 4)
               (str/starts-with? s "  ")
               (not (#{"  C" "  Clojure" "  Commands"} s))
               (not (str/starts-with? s "  Namespaces"))
               (not (str/starts-with? s "  (no source tree")))
      (let [trimmed (str/trim (subs s 2))
            paren (.indexOf trimmed "(")]
        (if (neg? paren)
          trimmed
          (str/trim (subs trimmed 0 paren)))))))

(defn apropos-name-at-line
  "Parse the symbol name out of a line rendered by format-apropos.
  Format: `  [kind] <ns padded to 24>/<name padded to 24> — <doc>`."
  [line]
  (let [s (or line "")]
    (when (and (>= (count s) 40) (str/starts-with? s "  ["))
      (let [after-ns (subs s 9)
            slash (.indexOf after-ns "/")]
        (when (pos? slash)
          (let [name-region (subs after-ns (inc slash))
                dash (.indexOf name-region "—")]
            (str/trim (if (neg? dash) name-region (subs name-region 0 dash)))))))))

(defn lookup-symbol
  "Find a fully-qualified or bare symbol in the index and return its map."
  [ns-name sym-name]
  (let [idx (ensure!)
        candidates (concat (get-in idx [:namespaces ns-name])
                           (get-in idx [:modules ns-name])
                           (when (= ns-name "commands") (:commands idx))
                           (when (#{"Clojure" "C" "Commands"} ns-name)
                             (all-symbols-flat)))]
    (first (filter #(= sym-name (:name %)) candidates))))

(defn find-command
  "Find a command by name in the commands list."
  [sym-name]
  (let [idx (ensure!)]
    (first (filter #(= sym-name (:name %)) (:commands idx)))))
