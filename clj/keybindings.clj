(ns hammock.keybindings
  (:require [hammock.state :as state]
            [clojure.string :as str]))

;; Key code constants matching src/keymap.h
(def HK_BACKSPACE 0x1001)
(def HK_DELETE    0x1002)
(def HK_UP        0x1003)
(def HK_DOWN      0x1004)
(def HK_LEFT      0x1005)
(def HK_RIGHT     0x1006)
(def HK_HOME      0x1007)
(def HK_END       0x1008)
(def HK_PGUP      0x1009)
(def HK_PGDN      0x100A)
(def HK_TAB       0x100B)
(def HK_ENTER     0x100C)
(def HK_SHIFT_TAB 0x100D)

(def MOD_CTRL 1)
(def MOD_META 2)

;; Parse a key spec like "C-f", "M-x", "Enter", "s" into [key modifiers]
(defn parse-key-spec [spec]
  (let [parts (str/split spec #"-" 2)]
    (cond
      ;; C-M- or M-C- (both modifiers)
      (and (>= (count parts) 2)
           (#{"C" "M"} (first parts)))
      (let [[mod rest] parts
            mod-val (if (= mod "C") MOD_CTRL MOD_META)]
        (if (and (>= (count rest) 2) (= (subs rest 0 2) (str (if (= mod "C") "M" "C") "-")))
          ;; Double modifier like C-M-x
          (let [key-char (subs rest 2)]
            [(int (first key-char)) (bit-or MOD_CTRL MOD_META)])
          ;; Single modifier
          (let [key-name rest]
            (cond
              (= (count key-name) 1) [(int (first key-name)) mod-val]
              (= key-name "<") [(int \<) mod-val]
              (= key-name ">") [(int \>) mod-val]
              (= key-name "/") [(int \/) mod-val]
              (= key-name "_") [(int \_) mod-val]
              (= key-name " ") [(int \space) mod-val]
              ;; Special keys with a modifier (e.g. "M-Backspace", "C-Up")
              (= key-name "Backspace") [HK_BACKSPACE mod-val]
              (= key-name "Delete")    [HK_DELETE mod-val]
              (= key-name "Up")        [HK_UP mod-val]
              (= key-name "Down")      [HK_DOWN mod-val]
              (= key-name "Left")      [HK_LEFT mod-val]
              (= key-name "Right")     [HK_RIGHT mod-val]
              (= key-name "Home")      [HK_HOME mod-val]
              (= key-name "End")       [HK_END mod-val]
              (= key-name "PgUp")      [HK_PGUP mod-val]
              (= key-name "PgDn")      [HK_PGDN mod-val]
              (= key-name "Tab")       [HK_TAB mod-val]
              (= key-name "Enter")     [HK_ENTER mod-val]
              :else [(int (first key-name)) mod-val]))))

      ;; Special key names
      (= spec "Backspace") [HK_BACKSPACE 0]
      (= spec "Delete")    [HK_DELETE 0]
      (= spec "Up")        [HK_UP 0]
      (= spec "Down")      [HK_DOWN 0]
      (= spec "Left")      [HK_LEFT 0]
      (= spec "Right")     [HK_RIGHT 0]
      (= spec "Home")      [HK_HOME 0]
      (= spec "End")       [HK_END 0]
      (= spec "PgUp")      [HK_PGUP 0]
      (= spec "PgDn")      [HK_PGDN 0]
      (= spec "Tab")       [HK_TAB 0]
      (= spec "Shift-Tab") [HK_SHIFT_TAB 0]
      (= spec "Enter")     [HK_ENTER 0]

      ;; Single character
      (= (count spec) 1) [(int (first spec)) 0]

      :else (throw (ex-info (str "Unknown key spec: " spec) {:spec spec})))))

;; Global keybindings
(def global-bindings
  [;; Movement
   ["C-f" "forward-char"]
   ["C-b" "backward-char"]
   ["C-n" "next-line"]
   ["C-p" "previous-line"]
   ["C-a" "beginning-of-line"]
   ["C-e" "end-of-line"]
   ["C-v" "scroll-down"]
   ["M-v" "scroll-up"]

   ["Right" "forward-char"]
   ["Left" "backward-char"]
   ["Down" "next-line"]
   ["Up" "previous-line"]
   ["Home" "beginning-of-line"]
   ["End" "end-of-line"]
   ["PgUp" "scroll-up"]
   ["PgDn" "scroll-down"]

   ;; Word movement
   ["M-f" "forward-word"]
   ["M-b" "backward-word"]
   ["M-}" "forward-paragraph"]
   ["M-e" "forward-paragraph"]
   ["M-{" "backward-paragraph"]
   ["M-a" "backward-paragraph"]

   ;; Buffer bounds
   ["M-<" "beginning-of-buffer"]
   ["M->" "end-of-buffer"]

   ;; Editing
   ["C-d" "delete-char"]
   ["Delete" "delete-char"]
   ["Backspace" "delete-backward-char"]
   ["Enter" "newline"]
   ["Tab" "self-insert-tab"]

   ;; Kill/yank
   ["C-k" "kill-line"]
   ["C-w" "kill-region"]
   ["M-w" "kill-ring-save"]
   ["C-y" "yank"]
   ["M-y" "yank-pop"]
   ["M-Backspace" "backward-kill-word"]

   ;; Mark
   ["C- " "set-mark"]

   ;; Undo
   ["C-/" "undo"]
   ["C-_" "undo"]
   ["C-u" "undo"]

   ;; Search
   ["C-s" "isearch-forward"]
   ["C-r" "isearch-backward"]

   ;; Clojure eval
   ["C-j" "eval-last-sexp"]

   ;; M-x
   ["M-x" "execute-extended-command"]

   ;; Shell
   ["M-!" "shell-command"]

   ;; Find definition
   ["M-." "find-definition"]
   ["M-," "pop-mark"]

   ;; Grep
   ["M-g" "rgrep"]

   ;; Quit
   ["C-g" "keyboard-quit"]])

;; C-x prefix bindings
(def cx-bindings
  [["C-s" "save-buffer"]
   ["C-f" "find-file"]
   ["C-c" "save-buffers-kill-emacs"]
   ["b"   "switch-to-buffer"]
   ["C-b" "list-buffers"]
   ["k"   "kill-buffer"]
   ["o"   "other-window"]
   ["2"   "split-window-below"]
   ["3"   "split-window-right"]
   ["0"   "delete-window"]
   ["1"   "delete-other-windows"]
   ["g"   "magit-status"]])

;; F1 help prefix bindings
(def f1-bindings
  [["k" "describe-key"]
   ["f" "describe-function"]
   ["n" "view-news"]
   ["s" "browse-symbols"]])

;; C-h help prefix bindings (Emacs-style, mirrors F1)
(def ch-bindings
  [["k" "describe-key"]
   ["f" "describe-function"]
   ["n" "view-news"]
   ["s" "browse-symbols"]
   ["a" "apropos"]
   ["e" "view-messages"]])

;; Mode-specific keybindings
(def mode-bindings
  {"markdown"   [["Enter"     "markdown-follow-link"]
                 ["Tab"       "markdown-next-link"]
                 ["Shift-Tab" "markdown-prev-link"]
                 ["l"         "markdown-go-back"]
                 ["n"         "markdown-next-heading"]
                 ["p"         "markdown-prev-heading"]]
   "git-status" [["s"     "git-stage"]
                 ["u"     "git-unstage"]
                 ["c"     "git-commit"]
                 ["q"     "git-quit"]
                 ["g"     "git-refresh"]
                 ["d"     "git-diff"]
                 ["l"     "git-log"]
                 ["f"     "git-fetch"]
                 ["F"     "git-pull"]
                 ["P"     "git-push"]
                 ["Tab"   "git-toggle-section"]
                 ["Enter" "git-visit-file"]]
   "git-log"    [["g"     "git-log"]
                 ["q"     "git-log-quit"]]
   "clojure"      []
   "diff"         [["q"     "diff-quit"]]
   "grep"         [["Enter" "grep-visit"]
                   ["q"     "grep-quit"]
                   ["g"     "grep-refresh"]]
   "buffer-list"  [["Enter" "buflist-visit"]
                   ["D"     "buflist-mark-delete"]
                   ["x"     "buflist-execute"]
                   ["q"     "buflist-quit"]]
   "symbol-browser" [["n"     "next-line"]
                     ["p"     "previous-line"]
                     ["Enter" "symbrowse-select"]
                     ["g"     "symbrowse-refresh"]
                     ["q"     "symbrowse-quit"]]
   "symbol-detail"  [["n"     "next-line"]
                     ["p"     "previous-line"]
                     ["Enter" "symbrowse-visit"]
                     ["Tab"   "other-window"]
                     ["g"     "symbrowse-refresh"]
                     ["q"     "symbrowse-quit"]]
   "apropos"        [["Enter" "apropos-visit"]
                     ["g"     "apropos"]
                     ["q"     "apropos-quit"]]})

;; Export keybindings as EDN vector-of-vectors for C to parse.
;; Format: [["global" key-code modifiers "command"] ...]
;; Special: ["prefix" key-code modifiers "submap-name"]
;; Mode keymaps: ["mode:modename" key-code modifiers "command"]
(defn export []
  (let [encode (fn [keymap-name bindings]
                 (mapv (fn [[spec cmd]]
                         (let [[key mods] (parse-key-spec spec)]
                           [keymap-name key mods cmd]))
                       bindings))
        global (encode "global" global-bindings)
        cx (encode "cx" cx-bindings)
        f1 (encode "f1" f1-bindings)
        ch (encode "ch" ch-bindings)
        prefix-cx (let [[key mods] (parse-key-spec "C-x")]
                    [["prefix" key mods "cx"]])
        prefix-f1 [["prefix" 4112 0 "f1"]]  ;; HK_F1 = 0x1010 = 4112
        prefix-ch (let [[key mods] (parse-key-spec "C-h")]
                    [["prefix" key mods "ch"]])
        modes (mapcat (fn [[mode-name bindings]]
                        (encode (str "mode:" mode-name) bindings))
                      mode-bindings)]
    (vec (concat global cx f1 ch prefix-cx prefix-f1 prefix-ch modes))))

;; Store in atom for live modification
(reset! state/*keybindings*
        {:global global-bindings
         :cx cx-bindings
         :f1 f1-bindings
         :ch ch-bindings
         :modes mode-bindings})
