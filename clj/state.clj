(ns hammock.state)

;; Primary editor state atom. C pushes a snapshot here before each
;; Clojure command dispatch via (reset! *editor* {...}).
;;
;; Shape:
;; {:current-buffer "*scratch*"
;;  :point 42
;;  :mark 0
;;  :mark-active false
;;  :length 200
;;  :modified false
;;  :read-only false
;;  :mode "Clojure"
;;  :filename nil
;;  :window-count 1
;;  :top-line 0
;;  :visible-rows 24
;;  :current-line "text of line at point"00
;;  :line-number 1       ;; 1-indexed
;;  :col 0
;;  :buffers [{:name "*scratch*" :size 200 :modified false
;;             :read-only false :mode "Fundamental" :filename nil} ...]}
(defonce *editor* (atom {}))

;; Command registry: maps command name string to a Clojure fn
;; that returns an effect vector.
;; {"forward-char" #'hammock.commands/forward-char ...}
(defonce *commands* (atom {}))

;; Keybinding table: nested map exported as EDN for C keymap.
;; Populated by hammock.keybindings at startup.
(defonce *keybindings* (atom {}))

;; Mode definitions: map of mode keyword to config map.
;; Populated by hammock.modes at startup.
(defonce *modes* (atom {}))

;; Version counter: incremented by watches when any config atom changes.
;; C polls this via sci_get_state_version() to detect when re-export is needed.
(defonce *config-version* (atom 0))

(defn install-watches!
  "Set up watches on config atoms. Called by C after all namespaces are loaded."
  []
  (add-watch *keybindings* :version-bump
    (fn [_ _ _ _] (swap! *config-version* inc)))
  (add-watch *commands* :version-bump
    (fn [_ _ _ _] (swap! *config-version* inc)))
  (add-watch *modes* :version-bump
    (fn [_ _ _ _] (swap! *config-version* inc))))
