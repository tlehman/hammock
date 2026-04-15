;; clj/syntax-modes.clj — per-mode font-lock tables consumed by
;; src/font_lock.c via (hammock.syntax-modes/export).
;;
;; Rule forms:
;;   {:words [w1 w2] :face :f}
;;   [pattern :face]            ; whole match
;;   [pattern [:f1 :f2 nil]]    ; vec[i] colors subgroup i+1; nil skips
;;
;; Regex dialect: POSIX ERE (libc regex.h). Word boundaries are [[:<:]]
;; and [[:>:]] — portable on macOS and Linux libc. \b is NOT portable.
;; The :words form auto-wraps with those boundaries; raw regex rules use
;; them directly.
(ns hammock.syntax-modes
  (:require [clojure.string :as str]))

(def c-keywords
  ["auto" "break" "case" "char" "const" "continue" "default" "do"
   "double" "else" "enum" "extern" "float" "for" "goto" "if"
   "inline" "int" "long" "register" "return" "short" "signed"
   "sizeof" "static" "struct" "switch" "typedef" "typeof" "union"
   "unsigned" "void" "volatile" "while"
   "bool" "true" "false" "NULL"
   "int8_t" "int16_t" "int32_t" "int64_t"
   "uint8_t" "uint16_t" "uint32_t" "uint64_t"
   "size_t" "ssize_t" "pid_t"])

(def c-types ["FILE" "DIR" "va_list"])

(def c-mode
  {:name "C"
   :syntax
   ;; Strings are matched by a regex rule below (not the syntax-table)
   ;; so preprocessor lines like `#include "util.h"` stay intact as a
   ;; single region and get one uniform :preproc color across the whole
   ;; line. (C strings don't span lines, so we don't lose multi-line
   ;; state by pulling string detection out of the syntax-table.)
   {:syntax-table {:comment-line  "//"
                   :comment-block ["/*" "*/"]
                   :char-delim    \'
                   :string-escape \\}
    :font-lock-keywords
    [;; Preprocessor line — wins over string/function rules via shortest-start.
     ["^[[:space:]]*#.*$"                                   :preproc]
     ;; String literal with escape handling
     ["\"([^\"\\\\]|\\\\.)*\""                              :string]
     {:words c-keywords :face :keyword}
     {:words c-types    :face :type}
     ["[[:<:]]0[xX][[:xdigit:]]+[uUlL]*[[:>:]]"             :number]
     ["[[:<:]][[:digit:]]+(\\.[[:digit:]]*)?([eE][+-]?[[:digit:]]+)?[uUlL]*[[:>:]]"  :number]
     ["([[:alpha:]_][[:alnum:]_]*)[[:space:]]*\\("          [:function]]]}})

(def clj-keywords
  ["def" "defn" "defn-" "defmacro" "defonce" "defmulti" "defmethod"
   "defprotocol" "defrecord" "deftype" "defstruct"
   "fn" "let" "loop" "recur" "do" "if" "if-let" "if-not"
   "when" "when-let" "when-not" "when-first"
   "cond" "condp" "case"
   "for" "doseq" "dotimes" "while"
   "try" "catch" "finally" "throw"
   "ns" "require" "import" "use" "refer"
   "atom" "deref" "swap!" "reset!" "compare-and-set!"
   "apply" "map" "filter" "reduce" "into" "comp" "partial"
   "first" "rest" "cons" "conj" "assoc" "dissoc" "get"
   "str" "println" "prn" "pr-str"
   "nil" "true" "false"])

;; Clojure symbol characters. Used to build a custom word boundary that
;; keeps hyphenated names like `ns-name` together (POSIX [[:<:]]/[[:>:]]
;; treats `-` as non-word, so `ns` inside `ns-name` would match).
;; `-` is placed at the start of the character class so it is treated
;; as a literal, not a range.
(def ^:private clj-sym-chars "-A-Za-z0-9_!?*+/")

(def clojure-mode
  {:name "Clojure"
   :syntax
   {:syntax-table {:comment-line  ";"
                   :string-delims [\"]
                   :string-escape \\}
    :font-lock-keywords
    [;; Keyword-name match with symbol-aware boundaries.
     ;; Subgroup 1 = pre-boundary, 2 = the keyword, 3 = post-boundary.
     [(str "(^|[^" clj-sym-chars "])("
           (str/join "|" clj-keywords)
           ")([^" clj-sym-chars "]|$)")
      [nil :keyword nil]]
     [":[[:alpha:]][[:alnum:]_./:?!-]*"                    :type]
     ["#\"[^\"]*\""                                        :string]
     ["[[:<:]]-?[[:digit:]]+([./][[:digit:]]+)?N?M?[[:>:]]" :number]]}})

(def bash-keywords
  ["if" "then" "else" "elif" "fi"
   "for" "in" "do" "done"
   "while" "until"
   "case" "esac"
   "function" "return" "exit"
   "local" "export" "declare" "readonly" "typeset"
   "source" "eval" "exec"
   "echo" "printf" "read"
   "true" "false"
   "break" "continue" "shift"])

(def bash-mode
  {:name "Bash"
   :syntax
   {:syntax-table {:comment-line  "#"
                   :string-delims [\" \']
                   :string-escape \\}
    :font-lock-keywords
    [{:words bash-keywords :face :keyword}
     ["\\$\\{[^}]*\\}"                    :type]
     ["\\$[[:alnum:]_]+"                  :type]
     ["[[:<:]][[:digit:]]+[[:>:]]"         :number]]}})

(def markdown-mode
  {:name "Markdown"
   :syntax
   {:syntax-table {}
    :font-lock-keywords
    [["^######.*$"                              :heading3]
     ["^#####.*$"                               :heading3]
     ["^####.*$"                                :heading3]
     ["^###.*$"                                 :heading3]
     ["^##.*$"                                  :heading2]
     ["^#.*$"                                   :heading1]
     ["^>.*$"                                   :comment]
     ["`[^`]*`"                                 :code]
     ["\\*\\*[^*]+\\*\\*"                        :bold]
     ["__[^_]+__"                               :bold]
     ["\\*[^*[:space:]][^*]*\\*"                :italic]
     ["(!?)\\[[^]]*\\]\\([^)]*\\)"              :link]
     ["\\[\\[[^]]+\\]\\]"                       :link]]
    :fence {:open       "^```([[:alnum:]]*)[[:space:]]*$"
            :close      "^```[[:space:]]*$"
            :lang-group 1
            :langs      {"c" "C" "cpp" "C"
                         "sh" "Bash" "bash" "Bash" "zsh" "Bash"
                         "clj" "Clojure" "clojure" "Clojure"
                         "diff" "Diff"
                         "md" "Markdown" "markdown" "Markdown"}}}})

(def diff-mode
  {:name "Diff"
   :syntax
   {:syntax-table {}
    :font-lock-keywords
    [["^\\+\\+\\+.*$" :diff-header]
     ["^---.*$"       :diff-header]
     ["^@@.*$"        :diff-header]
     ["^diff.*$"      :diff-header]
     ["^index.*$"     :diff-header]
     ["^\\+.*$"       :diff-add]
     ["^-.*$"         :diff-del]]}})

(def help-mode
  {:name "Help"
   :syntax {:engine :builtin-help}})

(def make-keywords
  ["ifeq" "ifneq" "ifdef" "ifndef" "else" "endif"
   "include" "-include" "sinclude" "export" "unexport"
   "define" "endef" "override" "private" "vpath" "undefine"])

(def makefile-mode
  {:name "Makefile"
   :syntax
   {:syntax-table {:comment-line  "#"
                   :string-delims [\" \']
                   :string-escape \\}
    :font-lock-keywords
    [;; Automatic variables: $@ $< $^ $? $* $+ $| $%
     ["\\$[@<^?*+|%]"                                 :type]
     ;; Function call $(fn arg...): subgroup 1 = function name
     ["\\$\\(([[:alpha:]][[:alnum:]-]*)[[:space:]]"  [:function]]
     ;; Variable reference: $(NAME) or ${NAME}
     ["\\$[({][[:alnum:]_]+[)}]"                      :type]
     ;; Directive keywords (ifeq, include, export, …)
     {:words make-keywords :face :keyword}
     ;; Target line: "name:" or ".PHONY:" at start of line
     ["^\\.?[[:alpha:]][[:alnum:]._-]*[[:space:]]*:" :function]
     ;; Numbers
     ["[[:<:]][[:digit:]]+[[:>:]]"                    :number]]}})

(def modes
  [c-mode clojure-mode bash-mode markdown-mode diff-mode help-mode makefile-mode])

(defn export [] modes)
