(ns hammock.git
  (:require [clojure.string :as str]
            [hammock.shell :as shell]))

(defn git-status-porcelain []
  (:out (shell/exec ["git" "status" "--porcelain=v1"])))

(defn parse-status-line [line]
  (when (>= (count line) 4)
    {:index  (nth line 0)
     :work   (nth line 1)
     :path   (subs line 3)}))

(defn parse-status [output]
  (->> (str/split-lines output)
       (filter #(>= (count %) 4))
       (map parse-status-line)))

(defn staged-files [entries]
  (filter #(and (not= (:index %) \space)
                (not= (:index %) \?))
          entries))

(defn unstaged-files [entries]
  (filter #(and (not= (:work %) \space)
                (not= (:work %) \?))
          entries))

(defn untracked-files [entries]
  (filter #(and (= (:index %) \?)
                (= (:work %) \?))
          entries))

(defn format-status []
  (let [output (git-status-porcelain)
        entries (parse-status output)
        staged (staged-files entries)
        unstaged (unstaged-files entries)
        untracked (untracked-files entries)]
    (str
     (when (seq staged)
       (str "Staged changes:\n"
            (str/join "\n" (map #(str "  " (:path %)) staged))
            "\n\n"))
     (when (seq unstaged)
       (str "Unstaged changes:\n"
            (str/join "\n" (map #(str "  " (:path %)) unstaged))
            "\n\n"))
     (when (seq untracked)
       (str "Untracked files:\n"
            (str/join "\n" (map #(str "  " (:path %)) untracked))
            "\n"))
     (when (and (empty? staged) (empty? unstaged) (empty? untracked))
       "Nothing to commit, working tree clean\n"))))

(defn current-branch []
  (str/trim
    (:out (shell/exec ["git" "branch" "--show-current"]))))

(defn stage-file [path]
  (shell/exec ["git" "add" path]))

(defn unstage-file [path]
  (shell/exec ["git" "restore" "--staged" path]))

(defn diff-file [path]
  (:out (shell/exec ["git" "diff" path])))

(defn diff-file-cached [path]
  (:out (shell/exec ["git" "diff" "--cached" path])))

(defn git-log [n]
  (:out (shell/exec ["git" "log" "--oneline" (str "-" n)])))

(defn- exec-out-err [args]
  (let [r (shell/exec args)]
    (str/trim (str (:out r) (:err r)))))

(defn git-fetch [] (exec-out-err ["git" "fetch"]))
(defn git-pull  [] (exec-out-err ["git" "pull"]))
(defn git-push  [] (exec-out-err ["git" "push"]))

(defn git-commit-with-msg [msg]
  (let [result (shell/exec ["git" "commit" "-m" msg])]
    (str/trim (:out result))))

(defn extract-file-from-status-line [line]
  (when (and line (>= (count line) 3))
    (str/trim line)))
