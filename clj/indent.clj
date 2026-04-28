(ns hammock.indent
  (:require [clojure.string :as str]))

;; ---- helpers ----

(defn leading-spaces [line]
  (count (take-while #(= % \space) line)))

(defn- prev-nonblank [lines idx]
  (loop [i (dec idx)]
    (if (< i 0)
      nil
      (if (str/blank? (nth lines i ""))
        (recur (dec i))
        (nth lines i "")))))

;; ---- Clojure indentation ----
;;
;; Scans text before the current line, tracking delimiter nesting while
;; skipping strings and line comments. Returns the column of the innermost
;; unclosed delimiter plus an offset: +2 for (, +1 for [ and {.

(defn- toplevel-start
  "Find the 0-based index of the last line before idx that opens a
   top-level form (starts with '('). Used as a scan anchor so we never
   need to walk the whole buffer."
  [lines idx]
  (loop [i (dec idx)]
    (if (< i 0)
      0
      (if (str/starts-with? (nth lines i "") "(")
        i
        (recur (dec i))))))

(defn clojure-indent [lines idx]
  (if (zero? idx)
    0
    (let [start  (toplevel-start lines idx)
          before (str/join "\n" (take (- idx start) (drop start lines)))]
      (loop [cs      (seq before)
             stack   []       ; each entry: {:col c :delim ch}
             string? false
             col     0]
        (if (empty? cs)
          (if (empty? stack)
            0
            (let [top (peek stack)]
              (if (= (:delim top) \()
                (+ (:col top) 2)
                (+ (:col top) 1))))
          (let [c       (first cs)
                rest-cs (rest cs)]
            (cond
              string?
              (cond
                (= c \\)       (recur (rest rest-cs) stack true (+ col 2))
                (= c \")       (recur rest-cs stack false (inc col))
                (= c \newline) (recur rest-cs stack true 0)
                :else          (recur rest-cs stack true (inc col)))

              (= c \")
              (recur rest-cs stack true (inc col))

              (= c \;)
              (let [after (drop-while #(not= % \newline) rest-cs)]
                (if (empty? after)
                  (recur '() stack false col)
                  (recur (rest after) stack false 0)))

              (or (= c \() (= c \[) (= c \{))
              (recur rest-cs (conj stack {:col col :delim c}) false (inc col))

              (or (= c \)) (= c \]) (= c \}))
              (recur rest-cs (if (empty? stack) stack (pop stack)) false (inc col))

              (= c \newline)
              (recur rest-cs stack false 0)

              :else
              (recur rest-cs stack false (inc col)))))))))

;; ---- C indentation ----
;;
;; Counts net { } depth in text before the current line, skipping strings
;; and // / /* */ comments. Multiplies by 4. Lines starting with } de-dent.

(defn- c-brace-depth [text]
  (loop [cs    (seq text)
         depth 0
         str?  false
         lcmt? false
         bcmt? false
         prev  nil]
    (if (empty? cs)
      depth
      (let [c       (first cs)
            rest-cs (rest cs)
            nxt     (first rest-cs)]
        (cond
          lcmt?
          (if (= c \newline)
            (recur rest-cs depth false false false nil)
            (recur rest-cs depth false true false c))

          bcmt?
          (if (and (= prev \*) (= c \/))
            (recur rest-cs depth false false false nil)
            (recur rest-cs depth false false true c))

          str?
          (cond
            (= c \\) (recur (rest rest-cs) depth true false false nil)
            (= c \") (recur rest-cs depth false false false nil)
            :else    (recur rest-cs depth true false false c))

          (and (= c \/) (= nxt \/))
          (recur (rest rest-cs) depth false true false nil)

          (and (= c \/) (= nxt \*))
          (recur (rest rest-cs) depth false false true nil)

          (= c \")
          (recur rest-cs depth true false false nil)

          (= c \{)
          (recur rest-cs (inc depth) false false false c)

          (= c \})
          (recur rest-cs (dec depth) false false false c)

          :else
          (recur rest-cs depth false false false c))))))

(defn c-indent [lines idx]
  (if (zero? idx)
    0
    (let [before   (str/join "\n" (take idx lines))
          depth    (max 0 (c-brace-depth before))
          line     (or (nth lines idx nil) "")
          trimmed  (str/triml line)
          closes?  (str/starts-with? trimmed "}")]
      (* 4 (if closes? (max 0 (dec depth)) depth)))))

;; ---- Bash indentation ----
;;
;; Increments depth on then/do (from if/for/while) and function bodies;
;; decrements on fi/done/esac/}. Indent unit is 2 spaces.

(defn bash-indent [lines idx]
  (let [depth
        (reduce
          (fn [d line]
            (cond
              (re-find #"\bthen\s*(?:#.*)?$" line) (inc d)
              (re-find #"\bdo\s*(?:#.*)?$" line)   (inc d)
              (str/starts-with? (str/trim line) "function ") (inc d)
              (re-find #"^\s*(?:fi|done|esac|\})" line) (dec d)
              :else d))
          0
          (take idx lines))
        line    (or (nth lines idx nil) "")
        closes? (re-find #"^\s*(?:fi|done|esac|\})" line)]
    (* 2 (max 0 (if closes? (dec depth) depth)))))

;; ---- Default: match previous non-blank line ----

(defn default-indent [lines idx]
  (if-let [prev (prev-nonblank lines idx)]
    (leading-spaces prev)
    0))

;; ---- Public API ----

(defn compute-indent
  "Return the correct leading-space count for line at 0-indexed `idx` in
  `lines` given the buffer mode name."
  [mode lines idx]
  (case mode
    "Clojure" (clojure-indent lines idx)
    "C"       (c-indent lines idx)
    "Bash"    (bash-indent lines idx)
    (default-indent lines idx)))
