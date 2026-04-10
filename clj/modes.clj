(ns hammock.modes
  (:require [hammock.state :as state]
            [clojure.string :as str]))

;; Mode definitions. Each mode has:
;; :id         - integer index matching C MajorModeID enum
;; :name       - display name
;; :syntax     - syntax language name for C (maps to SyntaxLang enum)
;; :extensions - file extensions that trigger this mode
;; :keymap     - name of mode-specific keymap (or nil for global only)

(def mode-definitions
  [{:id 0 :name "Fundamental" :syntax "none"     :extensions [] :keymap nil}
   {:id 1 :name "C"           :syntax "c"        :extensions [".c" ".h" ".cc" ".cpp" ".hpp"] :keymap nil}
   {:id 2 :name "Clojure"     :syntax "clojure"  :extensions [".clj" ".cljs" ".cljc" ".edn" ".bb"] :keymap "clojure"}
   {:id 3 :name "Bash"        :syntax "bash"     :extensions [".sh" ".bash" ".zsh"] :keymap nil}
   {:id 4 :name "Markdown"    :syntax "markdown" :extensions [".md" ".markdown"] :keymap "markdown"}
   {:id 5 :name "Git-Status"  :syntax "none"     :extensions [] :keymap "git-status"}
   {:id 6 :name "Shell"       :syntax "none"     :extensions [] :keymap nil}
   {:id 7 :name "Buffer-List" :syntax "none"     :extensions [] :keymap nil}
   {:id 8 :name "Diff"        :syntax "diff"     :extensions [] :keymap "diff"}
   {:id 9 :name "Grep"        :syntax "none"     :extensions [] :keymap "grep"}
   {:id 10 :name "Help"       :syntax "help"     :extensions [] :keymap nil}])

;; Detect mode from filename
(defn detect-mode [filename]
  (if-not filename
    "Fundamental"
    (let [matching (first (filter (fn [m]
                                    (some #(str/ends-with? filename %) (:extensions m)))
                                  mode-definitions))]
      (if matching
        (:name matching)
        "Fundamental"))))

;; Export modes as EDN vector for C to parse.
;; Format: [[id "name" "syntax" ["ext1" "ext2"] "keymap-or-nil"] ...]
(defn export []
  (mapv (fn [{:keys [id name syntax extensions keymap]}]
          [id name syntax (vec extensions) (or keymap "nil")])
        mode-definitions))

;; Store in atom
(reset! state/*modes* (into {} (map (fn [m] [(:name m) m]) mode-definitions)))
