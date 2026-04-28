(ns hammock.markdown
  (:require [clojure.string :as str]))

;; Markdown utilities and link parsing for markdown-mode commands.

(defn heading-level [line]
  (let [trimmed (str/triml line)]
    (count (take-while #(= % \#) trimmed))))

(defn extract-links [text]
  (re-seq #"\[([^\]]+)\]\(([^)]+)\)" text))

(defn extract-headings [text]
  (->> (str/split-lines text)
       (filter #(str/starts-with? (str/triml %) "#"))
       (map (fn [line]
              {:level (heading-level line)
               :text (str/replace (str/triml line) #"^#+\s*" "")}))))

(defn toc [text]
  (->> (extract-headings text)
       (map (fn [{:keys [level text]}]
              (str (apply str (repeat (* 2 (dec level)) " ")) "- " text)))
       (str/join "\n")))

;; ---- Tables ----

(defn table-row?
  "True if the line plausibly belongs to a markdown table — starts with `|`
   after optional leading whitespace. Permissive on purpose: a half-typed
   row like `|` or `| foo` is still a table row, so `Tab` can scaffold and
   navigate it."
  [line]
  (and (string? line)
       (str/starts-with? (str/triml line) "|")))

(defn parse-cells
  "Split a table row into trimmed cell strings, dropping the empty cells
   produced by leading and trailing pipes."
  [line]
  (let [trimmed (str/triml line)
        parts (str/split trimmed #"\|" -1)
        ;; Drop leading empty cell from the outer pipe.
        parts (if (and (seq parts) (str/blank? (first parts)))
                (vec (rest parts))
                (vec parts))
        ;; Drop trailing empty cell from the outer pipe (trailing whitespace
        ;; on the row also produces a final blank cell).
        parts (if (and (seq parts) (str/blank? (last parts)))
                (subvec parts 0 (dec (count parts)))
                parts)]
    (mapv str/trim parts)))

(defn separator-row?
  "True if the line is a GFM separator row: every cell matches `:?-+:?`."
  [line]
  (let [cells (parse-cells line)]
    (and (seq cells)
         (every? (fn [c] (re-matches #":?-+:?" c)) cells))))

(defn find-table-bounds
  "Given a vector of lines and a 0-indexed line, return [start end] (both
   inclusive) of the contiguous table-row block containing it, or nil if
   the line is not a table row."
  [lines line-idx]
  (when (and (< line-idx (count lines))
             (table-row? (nth lines line-idx)))
    (let [start (loop [i line-idx]
                  (if (and (pos? i) (table-row? (nth lines (dec i))))
                    (recur (dec i))
                    i))
          end (loop [i line-idx]
                (if (and (< (inc i) (count lines))
                         (table-row? (nth lines (inc i))))
                  (recur (inc i))
                  i))]
      [start end])))

(defn column-alignments
  "Read alignment markers from separator-row cells. Returns a vector of
   :left / :right / :center, one entry per cell."
  [separator-cells]
  (mapv (fn [c]
          (let [t (str/trim c)
                left? (str/starts-with? t ":")
                right? (str/ends-with? t ":")]
            (cond
              (and left? right?) :center
              right?             :right
              :else              :left)))
        separator-cells))

(defn- pad-cell
  "Pad a cell string to width using the given alignment."
  [cell width align]
  (let [c (count cell)
        diff (max 0 (- width c))]
    (case align
      :right  (str (apply str (repeat diff \space)) cell)
      :center (let [l (quot diff 2)
                    r (- diff l)]
                (str (apply str (repeat l \space)) cell
                     (apply str (repeat r \space))))
      ;; :left and any unknown
      (str cell (apply str (repeat diff \space))))))

(defn- render-body-row
  "Render a non-separator row as `| cell | cell | … |`."
  [cells widths aligns]
  (let [parts (map-indexed
               (fn [i c]
                 (pad-cell c (nth widths i) (nth aligns i :left)))
               cells)]
    (str "| " (str/join " | " parts) " |")))

(defn- render-separator-row
  "Render the separator row so each inter-pipe region matches the body
   row's column width (`width + 2` chars). Honors alignment colons."
  [widths aligns]
  (let [parts (map-indexed
               (fn [i w]
                 (let [a (nth aligns i :left)
                       inner (+ w 2)]
                   (case a
                     :center (str ":" (apply str (repeat (- inner 2) \-)) ":")
                     :right  (str (apply str (repeat (- inner 1) \-)) ":")
                     :left   (apply str (repeat inner \-))
                     (apply str (repeat inner \-)))))
               widths)]
    (str "|" (str/join "|" parts) "|")))

(defn- count-pipes-before
  "Count `|` characters in `line` strictly before column `col`."
  [line col]
  (let [n (max 0 (min (or col 0) (count line)))]
    (count (filter #(= % \|) (subs line 0 n)))))

(defn- cell-content-col
  "Column where cell N's content starts in a rendered row formatted as
   `| cell0 | cell1 | … |`. Cell 0 starts at column 2 (after `| `)."
  [widths n]
  (+ 2 (reduce + 0 (map #(+ % 3) (take n widths)))))

(defn- next-non-separator-row
  "Smallest row-local index strictly greater than `cur` that isn't `sep-idx`,
   or nil if none."
  [num-rows sep-idx cur]
  (some (fn [i]
          (when (and (> i cur) (not= i sep-idx))
            i))
        (range num-rows)))

(defn- prev-non-separator-row
  "Largest row-local index strictly less than `cur` that isn't `sep-idx`,
   or nil if none."
  [sep-idx cur]
  (some (fn [i]
          (when (and (< i cur) (not= i sep-idx))
            i))
        (reverse (range cur))))

(defn align-table-at
  "Given full buffer text, a 1-based line number, and column, find the
   markdown table at that line and return a map with the buffer text
   rewritten so the table is padded rectangularly, plus the new point
   moved one cell in `direction` (`:forward` for `Tab`, `:backward`
   for `Shift-Tab`). Defaults to `:forward`.

   Forward (`Tab`):
   - Stub / incomplete rows (fewer parsed cells than the table's
     column count) are scaffolded with empty cells; the cursor lands
     at the first missing cell.
   - On a complete row, the cursor advances to the next cell based on
     how many `|`s are to its left.
   - From the last cell of the last data row, a new empty row is
     appended and the cursor lands in its first cell.
   - On a separator row, the cursor jumps to the next data row's
     first cell.

   Backward (`Shift-Tab`):
   - From any cell, the cursor moves to the previous cell.
   - From the first cell, the cursor wraps to the last cell of the
     previous data row. From the very first data row's first cell it
     stays put.
   - On a separator row, it jumps to the previous data row's last
     cell.

   Returns nil if the line is not on a table row.
   Result shape: {:new-text str :new-point int}."
  ([text line-num col]
   (align-table-at text line-num col :forward))
  ([text line-num col direction]
  (when (and (string? text) (pos? line-num))
    (let [lines (str/split text #"\n" -1)
          li (dec line-num)]
      (when-let [bounds (find-table-bounds lines li)]
        (let [start (first bounds)
              end (second bounds)
              table-lines (subvec (vec lines) start (inc end))
              parsed (mapv parse-cells table-lines)
              sep-local-idx (first (keep-indexed
                                    (fn [i ln]
                                      (when (separator-row? ln) i))
                                    table-lines))
              ncols (max 1 (reduce max 0 (map count parsed)))
              padded-rows (mapv (fn [r]
                                  (vec (concat r (repeat (- ncols (count r)) ""))))
                                parsed)
              ;; Widths come from data rows only — the separator's dashes
              ;; expand to match the body, not the other way around.
              content-rows (keep-indexed
                            (fn [i r] (when (not= i sep-local-idx) r))
                            padded-rows)
              widths (vec (for [i (range ncols)]
                            (max 1 (reduce max 0
                                           (map #(count (nth % i "")) content-rows)))))
              aligns (let [as (if sep-local-idx
                                (column-alignments (nth parsed sep-local-idx))
                                [])]
                       (vec (concat as (repeat (max 0 (- ncols (count as))) :left))))
              base-table-lines (vec
                                (map-indexed
                                 (fn [i cells]
                                   (if (= i sep-local-idx)
                                     (render-separator-row widths aligns)
                                     (render-body-row cells widths aligns)))
                                 padded-rows))

              ;; ---- Cell navigation ----
              cur-local (- li start)
              orig-line (nth lines li)
              orig-cells (nth parsed cur-local)
              parsed-count (count orig-cells)
              is-separator? (= cur-local sep-local-idx)
              is-incomplete? (and (not is-separator?) (< parsed-count ncols))
              pipes-before (count-pipes-before orig-line col)

              [tgt-row tgt-cell append?]
              (case direction
                :backward
                (cond
                  is-separator?
                  (if-let [p (prev-non-separator-row sep-local-idx cur-local)]
                    [p (dec ncols) false]
                    [cur-local 0 false])

                  (<= pipes-before 1)
                  (if-let [p (prev-non-separator-row sep-local-idx cur-local)]
                    [p (dec ncols) false]
                    [cur-local 0 false])

                  :else
                  [cur-local (- pipes-before 2) false])

                ;; :forward (default)
                (cond
                  is-separator?
                  (if-let [n (next-non-separator-row (count parsed) sep-local-idx cur-local)]
                    [n 0 false]
                    [cur-local 0 false])

                  is-incomplete?
                  [cur-local (min parsed-count (dec ncols)) false]

                  (>= pipes-before ncols)
                  (if-let [n (next-non-separator-row (count parsed) sep-local-idx cur-local)]
                    [n 0 false]
                    [(count parsed) 0 true])

                  :else
                  [cur-local pipes-before false]))

              new-table-lines (if append?
                                (conj base-table-lines
                                      (render-body-row (vec (repeat ncols ""))
                                                       widths aligns))
                                base-table-lines)
              before (subvec (vec lines) 0 start)
              after (subvec (vec lines) (inc end))
              new-lines (vec (concat before new-table-lines after))
              new-text (str/join "\n" new-lines)
              tgt-row-global (+ start tgt-row)
              preceding-len (reduce + 0 (map #(inc (count %)) (take tgt-row-global new-lines)))
              cell-col (cell-content-col widths tgt-cell)
              new-point (+ preceding-len cell-col)]
          {:new-text new-text
           :new-point new-point}))))))

(defn link-at-point
  "Parse the link at the given column in the line text.
   Returns {:type :bidir/:markdown/:none :target \"...\" :text \"...\"}."
  [line col]
  (if (nil? line)
    {:type :none}
    (let [len (count line)]
      ;; Check for [[bidir]] links
      (or
       (some (fn [i]
               (when (and (< (+ i 3) len)
                          (= (nth line i) \[)
                          (= (nth line (inc i)) \[))
                 (let [j (loop [j (+ i 2)]
                           (if (or (>= j (dec len))
                                   (and (= (nth line j) \])
                                        (= (nth line (inc j)) \])))
                             j
                             (recur (inc j))))]
                   (when (and (< j (dec len))
                              (= (nth line j) \])
                              (= (nth line (inc j)) \]))
                     (let [end (+ j 2)]
                       (when (and (>= col i) (< col end))
                         (let [target (subs line (+ i 2) j)]
                           {:type :bidir
                            :target target
                            :text target})))))))
             (range (max 0 (- len 3))))

       ;; Check for [text](url) links
       (some (fn [i]
               (when (and (= (nth line i) \[)
                          (or (zero? i) (not= (nth line (dec i)) \[)))
                 (let [j (loop [j (inc i)]
                           (if (or (>= j len) (= (nth line j) \]))
                             j
                             (recur (inc j))))]
                   (when (and (< j len)
                              (< (inc j) len)
                              (= (nth line (inc j)) \())
                     (let [k (loop [k (+ j 2)]
                               (if (or (>= k len) (= (nth line k) \)))
                                 k
                                 (recur (inc k))))]
                       (when (and (< k len) (>= col i) (< col (inc k)))
                         {:type :markdown
                          :text (subs line (inc i) j)
                          :target (subs line (+ j 2) k)}))))))
             (range len))

       {:type :none}))))
