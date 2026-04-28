(ns latex2unicode
  "Convert LaTeX commands to Unicode, e.g. \\mathcal{H} -> ℋ.

  Port of svenkreiss/unicodeit. Zero dependencies. Works under babashka
  and libsci. Public API is the single `latex2unicode` function."
  (:require [clojure.string :as str]
            [latex2unicode.data :as data]))

(defn- handle-not
  "Convert \\not\\foo -> \\slash{\\foo} so combining-slash can finish the job."
  [s]
  (str/replace s #"\\not(\\[A-Za-z]+)" "\\\\slash{$1}"))

(defn- escape-combining
  "For each combining mark \\hat, rewrite `\\hat{` as `\\ hat{` so the main
  REPLACEMENTS sweep doesn't eat it. The trailing combining-marks pass
  undoes this."
  [s]
  (reduce (fn [acc [cmd _]]
            (str/replace acc (str cmd "{") (str "\\ " (subs cmd 1) "{")))
          s data/combining-marks))

(defn- apply-replacements
  "Linear sweep of the ~4200 REPLACEMENTS, longest-first."
  [s]
  (reduce (fn [acc [pat rep]]
            (let [acc' (str/replace acc pat rep)]
              (if (str/ends-with? pat "{}")
                (str/replace acc' (str "\\ " (subs pat 1)) rep)
                acc')))
          s data/replacements))

(def ^:private sub-group-re
  #"_\{[0-9+\-=()<>aeoxjhklmnpstiruv\u03B2\u03B3\u03C1\u03C6\u03C7\u2212]+\}")

(def ^:private sup-group-re
  #"\^\{[0-9+\-=()<>ABDEGHIJKLMNOPRTUWabcdefghijklmnoprstuvwxyz\u03B2\u03B3\u03B4\u03C6\u03C7\u222B\u2212]+\}")

(defn- expand-sub-group
  "Rewrite `_{012}` as `_0_1_2` so the single-char subscript table can handle it."
  [s]
  (str/replace s sub-group-re
               (fn [m]
                 (let [inner (subs m 2 (dec (count m)))]
                   (apply str (map #(str "_" %) inner))))))

(defn- expand-sup-group
  [s]
  (str/replace s sup-group-re
               (fn [m]
                 (let [inner (subs m 2 (dec (count m)))]
                   (apply str (map #(str "^" %) inner))))))

(defn- apply-subsuperscripts
  [s]
  (reduce (fn [acc [pat rep]] (str/replace acc pat rep))
          s data/subsuperscripts))

(defn- apply-combining
  "Walk each `\\ hat{X}` escape, emit X followed by the combining mark."
  [s]
  (reduce
    (fn [acc [cmd mark]]
      (let [esc (str "\\ " (subs cmd 1) "{")
            el  (count esc)]
        (loop [f acc]
          (let [i (str/index-of f esc)]
            (cond
              (nil? i) f
              (<= (count f) (+ i el))
              (recur (str (subs f 0 i) cmd "{"))
              :else
              (let [cc  (subs f (+ i el) (+ i el 1))
                    rem (if (>= (count f) (+ i el 2))
                          (subs f (+ i el 2))
                          "")]
                (recur (str (subs f 0 i) cc mark rem))))))))
    s data/combining-marks))

(defn latex2unicode
  "Convert a LaTeX string to its Unicode rendering.

  (latex2unicode \"\\\\mathcal{H}\") ;=> \"ℋ\""
  [s]
  (-> s
      handle-not
      escape-combining
      apply-replacements
      expand-sub-group
      expand-sup-group
      apply-subsuperscripts
      apply-combining))
