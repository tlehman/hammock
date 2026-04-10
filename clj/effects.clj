(ns hammock.effects
  (:require [hammock.state :as state]))

;; Effect constructor helpers.
;; Commands return vectors of effects. Each effect is a vector
;; whose first element is a keyword naming the primitive operation
;; and remaining elements are arguments.
;;
;; Example: [[:point-forward 1] [:reset-target-col]]

;; Point/cursor
(defn point-set [pos]              [:point-set pos])
(defn point-forward [n]            [:point-forward n])
(defn point-backward [n]           [:point-backward n])
(defn point-to-line-start []       [:point-to-line-start])
(defn point-to-line-end []         [:point-to-line-end])
(defn point-to-buffer-start []     [:point-to-buffer-start])
(defn point-to-buffer-end []       [:point-to-buffer-end])
(defn point-forward-word []        [:point-forward-word])
(defn point-backward-word []       [:point-backward-word])
(defn point-forward-paragraph []   [:point-forward-paragraph])
(defn point-backward-paragraph []  [:point-backward-paragraph])
(defn point-next-line []           [:point-next-line])
(defn point-prev-line []           [:point-prev-line])

;; Text mutation
(defn insert [text]                [:insert text])
(defn delete-forward [n]           [:delete-forward n])
(defn delete-backward [n]          [:delete-backward n])

;; Mark / region / kill
(defn set-mark []                  [:set-mark])
(defn deactivate-mark []           [:deactivate-mark])
(defn kill-region []               [:kill-region])
(defn copy-region []               [:copy-region])
(defn yank []                      [:yank])
(defn kill-line []                 [:kill-line])

;; Undo
(defn undo []                      [:undo])

;; Buffer management
(defn buffer-create [name]         [:buffer-create name])
(defn buffer-switch [name]         [:buffer-switch name])
(defn buffer-load-file [path]      [:buffer-load-file path])
(defn buffer-save []               [:buffer-save])
(defn buffer-destroy [name]        [:buffer-destroy name])
(defn buffer-set-contents [text]   [:buffer-set-contents text])
(defn buffer-set-read-only [b]     [:buffer-set-read-only b])
(defn buffer-set-modified [b]      [:buffer-set-modified b])
(defn buffer-set-mode [mode]       [:buffer-set-mode mode])
(defn buffer-set-filename [path]   [:buffer-set-filename path])

;; Window management
(defn window-split-below []        [:window-split-below])
(defn window-split-right []        [:window-split-right])
(defn window-delete []             [:window-delete])
(defn window-delete-others []      [:window-delete-others])
(defn window-other []              [:window-other])

;; Display
(defn msg [text]                   [:message text])
(defn redisplay []                 [:redisplay])

;; Interactive input
(defn prompt [prompt-text callback-fn completion-type]
  [:prompt prompt-text callback-fn completion-type])

;; Search
(defn search-forward [pattern]     [:search-forward pattern])
(defn search-backward [pattern]    [:search-backward pattern])

;; Scroll
(defn scroll-down [n]              [:scroll-down n])
(defn scroll-up [n]                [:scroll-up n])

;; Target column
(defn reset-target-col []          [:reset-target-col])
(defn preserve-target-col []       [:preserve-target-col])

;; Lifecycle
(defn quit []                      [:quit])

;; Shell
(defn shell-start []               [:shell-start])
(defn shell-command [cmd]          [:shell-command cmd])

;; Clipboard
(defn clipboard-copy [text]        [:clipboard-copy text])

;; Convenience: get current editor state
(defn editor [] @state/*editor*)
(defn current-buffer [] (:current-buffer (editor)))
(defn point [] (:point (editor)))
(defn mark [] (:mark (editor)))
(defn mark-active? [] (:mark-active (editor)))
(defn buffer-length [] (:length (editor)))
(defn modified? [] (:modified (editor)))
(defn read-only? [] (:read-only (editor)))
(defn current-line [] (:current-line (editor)))
(defn line-number [] (:line-number (editor)))
(defn col [] (:col (editor)))
(defn buffers [] (:buffers (editor)))
