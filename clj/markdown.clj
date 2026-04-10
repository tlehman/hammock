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
