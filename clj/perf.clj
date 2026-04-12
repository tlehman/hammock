;; hammock.perf: comparison and reporting for perf/runs/*.edn files.
;;
;; Designed to run under SCI via `./hammock -e "(hammock.perf/...)"`.
;; Reads EDN produced by src/perf.c `perf_write_bench_edn`, which is a
;; map like:
;;
;;   {:version "0.2.1"
;;    :host "bender"
;;    :mode :bench
;;    :timestamp "2026-04-11T23:59:57Z"
;;    :samples [{:label "forward-char" :n 1000
;;               :min-ns 0 :p50-ns 0 :p90-ns 0 :p99-ns 1000
;;               :max-ns 5000 :mean-ns 91} ...]}
;;
;; Also understands the ambient log shape (one `{:label :ns}` map per
;; line) for `summarize`.
(ns hammock.perf
  (:require [clojure.string :as str]))

;; SCI under GraalVM native-image doesn't expose `slurp` or
;; `clojure.edn/read-string`. Shell out to `cat` via the
;; `hammock.shell/exec` built-in the same way clj/symbols.clj does,
;; and use `clojure.core/read-string` for parsing. The input files are
;; produced by `perf_write_bench_edn` in src/perf.c so they are trusted.
(defn- slurp-file [path]
  (:out (hammock.shell/exec ["cat" path])))

(defn load-run
  "Read a bench EDN file and return the parsed map."
  [path]
  (read-string (slurp-file path)))

(defn- samples->by-label [run]
  (into {} (map (fn [s] [(:label s) s])) (:samples run)))

(defn- fmt-ns
  "Pretty-print a nanosecond value as ns / µs / ms."
  [ns]
  (cond
    (nil? ns)        "     -"
    (< ns 1000)      (format "%5d ns" ns)
    (< ns 1000000)   (format "%5.1f µs" (/ ns 1000.0))
    :else            (format "%5.1f ms" (/ ns 1000000.0))))

(defn- pct-delta [baseline now]
  (when (and baseline now (pos? baseline))
    (* 100.0 (/ (double (- now baseline)) (double baseline)))))

(defn- fmt-delta [pct]
  (if (nil? pct)
    "      -"
    (let [s (format "%+6.1f%%" pct)]
      (cond
        (< pct -2.0) (str "\033[32m" s "\033[0m")   ; green: faster
        (> pct  5.0) (str "\033[31m" s "\033[0m")   ; red:   slower
        :else        (str "\033[33m" s "\033[0m")))));; yellow: within noise

(defn diff-runs
  "Compare two bench runs by :label. Returns a seq of maps
   {:label :baseline-p50 :new-p50 :delta-p50-pct :baseline-p99 :new-p99
    :delta-p99-pct}."
  [baseline-run new-run]
  (let [b (samples->by-label baseline-run)
        n (samples->by-label new-run)
        labels (sort (distinct (concat (keys b) (keys n))))]
    (for [label labels
          :let  [bs (get b label)
                 ns (get n label)]]
      {:label          label
       :baseline-p50   (:p50-ns bs)
       :new-p50        (:p50-ns ns)
       :delta-p50-pct  (pct-delta (:p50-ns bs) (:p50-ns ns))
       :baseline-p99   (:p99-ns bs)
       :new-p99        (:p99-ns ns)
       :delta-p99-pct  (pct-delta (:p99-ns bs) (:p99-ns ns))})))

(defn report
  "Print a color-coded diff table comparing two bench run EDN files."
  [baseline-path new-path]
  (let [baseline (load-run baseline-path)
        newrun   (load-run new-path)
        rows     (diff-runs baseline newrun)]
    (println (str "baseline: " baseline-path
                  " (" (:version baseline) " @ " (:timestamp baseline) ")"))
    (println (str "new:      " new-path
                  " (" (:version newrun)   " @ " (:timestamp newrun)   ")"))
    (println (str "host:     " (:host baseline) " -> " (:host newrun)))
    (when (not= (:host baseline) (:host newrun))
      (println "\033[33mWARNING: hosts differ; perf comparisons across machines are meaningless\033[0m"))
    (println)
    (println (format "%-32s %10s %10s %8s   %10s %10s %8s"
                     "case" "base p50" "new p50" "Δ p50" "base p99" "new p99" "Δ p99"))
    (println (apply str (repeat 95 \-)))
    (doseq [r rows]
      (println (format "%-32s %10s %10s %8s   %10s %10s %8s"
                       (:label r)
                       (fmt-ns (:baseline-p50 r))
                       (fmt-ns (:new-p50 r))
                       (fmt-delta (:delta-p50-pct r))
                       (fmt-ns (:baseline-p99 r))
                       (fmt-ns (:new-p99 r))
                       (fmt-delta (:delta-p99-pct r)))))
    (println)
    "done"))

(defn- parse-ambient-line [line]
  (when (and line (pos? (count line)) (not (clojure.string/starts-with? line ";")))
    (try (read-string line) (catch Exception _ nil))))

(defn summarize
  "Read an ambient-mode log (one EDN map per line) and print a summary table.
   Groups samples by :label and reports count, min, p50, p99, max, mean (ns)."
  [path]
  (let [lines      (clojure.string/split-lines (slurp-file path))
        parsed     (keep parse-ambient-line lines)
        by-label   (group-by :label parsed)]
    (println (format "%-28s %8s %10s %10s %10s %10s %10s"
                     "label" "count" "min" "p50" "p99" "max" "mean"))
    (println (apply str (repeat 90 \-)))
    (doseq [[label samples] (sort-by key by-label)]
      (let [ns-vals (sort (map :ns samples))
            n       (count ns-vals)]
        (when (pos? n)
          (let [mn   (first ns-vals)
                mx   (last ns-vals)
                p50  (nth ns-vals (int (* 0.50 (dec n))))
                p99  (nth ns-vals (int (* 0.99 (dec n))))
                sum  (reduce + 0 ns-vals)
                mean (long (/ sum n))]
            (println (format "%-28s %8d %10s %10s %10s %10s %10s"
                             label n
                             (fmt-ns mn)
                             (fmt-ns p50)
                             (fmt-ns p99)
                             (fmt-ns mx)
                             (fmt-ns mean)))))))
    "done"))
