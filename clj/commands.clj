(ns hammock.commands
  (:require [hammock.state :as state]
            [hammock.effects :as fx]
            [hammock.git :as git]
            [hammock.markdown :as md]
            [clojure.string :as str]))

;; Register a command: associates a name string with a map of {:fn f :doc docstring}
(defn defcommand
  ([name f] (defcommand name "" f))
  ([name docstring f]
   (swap! state/*commands* assoc name {:fn f :doc docstring})))

;; Dispatch a command by name, return effect vector.
;; nREPL's value serialization produces the EDN string for C.
(defn dispatch [name]
  (let [entry (get @state/*commands* name)
        f (if (map? entry) (:fn entry) entry)]
    (if f
      (f)
      [[:message (str "Unknown Clojure command: " name)]])))

;; Export command metadata as [[name docstring] ...] vector for C registration
(defn export-command-metadata []
  (vec
    (map (fn [[name entry]]
           [name (if (map? entry) (:doc entry) "")])
         @state/*commands*)))

;; ---- Simple movement commands ----

(defcommand "forward-char" "Move point forward one character."
  (fn [] [[:point-forward 1] [:reset-target-col]]))

(defcommand "backward-char" "Move point backward one character."
  (fn [] [[:point-backward 1] [:reset-target-col]]))

(defcommand "next-line" "Move point to the next line."
  (fn [] [[:point-next-line]]))

(defcommand "previous-line" "Move point to the previous line."
  (fn [] [[:point-prev-line]]))

(defcommand "beginning-of-line" "Move point to beginning of current line."
  (fn [] [[:point-to-line-start] [:reset-target-col]]))

(defcommand "end-of-line" "Move point to end of current line."
  (fn [] [[:point-to-line-end] [:reset-target-col]]))

(defcommand "beginning-of-buffer" "Move point to the beginning of the buffer."
  (fn [] [[:point-to-buffer-start] [:reset-target-col]]))

(defcommand "end-of-buffer" "Move point to the end of the buffer."
  (fn [] [[:point-to-buffer-end] [:reset-target-col]]))

(defcommand "forward-word" "Move point forward one word."
  (fn [] [[:point-forward-word] [:reset-target-col]]))

(defcommand "backward-word" "Move point backward one word."
  (fn [] [[:point-backward-word] [:reset-target-col]]))

(defcommand "forward-paragraph" "Move point forward to end of paragraph."
  (fn [] [[:point-forward-paragraph] [:reset-target-col]]))

(defcommand "backward-paragraph" "Move point backward to start of paragraph."
  (fn [] [[:point-backward-paragraph] [:reset-target-col]]))

;; ---- Scroll ----

(defcommand "scroll-down" "Scroll the display down by one screenful."
  (fn []
    (let [visible (max 1 (- (:visible-rows (fx/editor)) 2))]
      [[:scroll-down visible]])))

(defcommand "scroll-up" "Scroll the display up by one screenful."
  (fn []
    (let [visible (max 1 (- (:visible-rows (fx/editor)) 2))]
      [[:scroll-up visible]])))

;; ---- Editing ----

(defcommand "delete-char" "Delete the character at point."
  (fn []
    (if (fx/mark-active?)
      [[:kill-region] [:deactivate-mark]]
      [[:delete-forward 1]])))

(defcommand "delete-backward-char" "Delete the character before point."
  (fn []
    (if (fx/mark-active?)
      [[:kill-region] [:deactivate-mark]]
      [[:delete-backward 1]])))

(defcommand "newline" "Insert a newline at point."
  (fn [] [[:insert "\n"] [:reset-target-col]]))

(defcommand "self-insert-tab" "Insert spaces to the next tab stop."
  (fn []
    ;; Insert spaces to next 4-column tab stop
    ;; Note: we don't have exact col info from state, so insert 4 spaces
    ;; The C implementation handles this more precisely
    [[:insert "    "]]))

;; ---- Mark / region / kill ----

(defcommand "set-mark" "Set the mark at point."
  (fn [] [[:set-mark] [:message "Mark set"]]))

(defcommand "keyboard-quit" "Cancel the current operation."
  (fn [] [[:deactivate-mark] [:message "Quit"]]))

(defcommand "kill-line" "Kill from point to end of line."
  (fn [] [[:kill-line]]))

(defcommand "kill-region" "Kill the text between point and mark."
  (fn [] [[:kill-region]]))

(defcommand "kill-ring-save" "Save the region to the kill ring without killing."
  (fn [] [[:copy-region] [:message "Region saved"]]))

(defcommand "yank" "Yank the last kill into the buffer at point."
  (fn [] [[:yank]]))

;; ---- Undo ----

(defcommand "undo" "Undo the last change."
  (fn [] [[:undo]]))

;; ---- Buffer management ----

(defcommand "save-buffer" "Save the current buffer to its file."
  (fn [] [[:buffer-save]]))

;; ---- Buffer list state ----
(def ^:private buflist-marks (atom #{}))

(defn- format-buffer-list
  "Format buffer list table from snapshot :buffers data."
  [buffers current-name marks]
  (let [header "  CRM  Buffer                Size  Mode          File\n"
        sep    "  ---  ------                ----  ----          ----\n"
        lines (for [{:keys [name size modified read-only mode filename]}
                    (remove #(= (:name %) "*Buffer List*") buffers)]
                (format " %c%c%c  %-20s  %5d  %-12s  %s\n"
                        (if (= name current-name) \. \space)
                        (if read-only \% \space)
                        (cond
                          (contains? marks name) \D
                          modified \*
                          :else \space)
                        name size (or mode "") (or filename "")))]
    (str header sep (apply str lines))))

(defcommand "list-buffers" "Display a list of all buffers."
  (fn []
    (reset! buflist-marks #{})
    (let [state (fx/editor)
          content (format-buffer-list (:buffers state)
                                     (:current-buffer state)
                                     @buflist-marks)]
      [[:buffer-create "*Buffer List*"]
       [:window-split-below]
       [:window-other]
       [:buffer-switch "*Buffer List*"]
       [:buffer-set-read-only false]
       [:buffer-set-contents content]
       [:point-to-buffer-start]
       [:buffer-set-modified false]
       [:buffer-set-read-only true]
       [:buffer-set-mode "Buffer-List"]])))

(defn- buflist-name-at-point
  "Extract buffer name from the current line of a buffer list display."
  []
  (let [line (fx/current-line)
        ln (fx/line-number)]
    (when (and line (>= ln 3) (>= (count line) 25))
      (str/trim (subs line 5 25)))))

(defcommand "buflist-visit" "Open the buffer at point in the buffer list."
  (fn []
    (let [name (buflist-name-at-point)]
      (if name
        [[:window-delete] [:buffer-switch name]]
        [[:message "No buffer at point"]]))))

(defcommand "buflist-mark-delete" "Mark the buffer at point for deletion."
  (fn []
    (let [name (buflist-name-at-point)]
      (if name
        (do (swap! buflist-marks conj name)
            (let [state (fx/editor)
                  content (format-buffer-list (:buffers state)
                                             (:current-buffer state)
                                             @buflist-marks)]
              [[:buffer-set-read-only false]
               [:buffer-set-contents content]
               [:buffer-set-modified false]
               [:buffer-set-read-only true]
               [:point-next-line]]))
        [[:message "No buffer at point"]]))))

(defcommand "buflist-execute" "Execute pending buffer list operations."
  (fn []
    (let [marks @buflist-marks
          effects (vec (mapcat (fn [name] [[:buffer-destroy name]]) marks))]
      (reset! buflist-marks #{})
      (let [state (fx/editor)
            content (format-buffer-list (:buffers state)
                                       (:current-buffer state)
                                       @buflist-marks)]
        (into effects
              [[:buffer-set-read-only false]
               [:buffer-set-contents content]
               [:point-to-buffer-start]
               [:buffer-set-modified false]
               [:buffer-set-read-only true]])))))

(defcommand "buflist-quit" "Close the buffer list window."
  (fn []
    [[:window-delete]]))

(defcommand "save-buffers-kill-emacs" "Save all buffers and exit the editor."
  (fn [] [[:quit]]))

;; ---- Window management ----

(defcommand "other-window" "Switch to the other window."
  (fn [] [[:window-other]]))

(defcommand "split-window-below" "Split the current window horizontally."
  (fn [] [[:window-split-below]]))

(defcommand "split-window-right" "Split the current window vertically."
  (fn [] [[:window-split-right]]))

(defcommand "delete-window" "Delete the current window."
  (fn [] [[:window-delete]]))

(defcommand "delete-other-windows" "Delete all windows except the current one."
  (fn [] [[:window-delete-others]]))

;; ---- Interactive commands (Phase 5 will use :prompt) ----

(defcommand "find-file" "Open a file, creating a new buffer."
  (fn [] [[:prompt "Find file: " "hammock.commands/find-file-cb" :file]]))

(defn find-file-cb [path]
  (let [name (last (clojure.string/split path #"/"))]
    [[:buffer-create name]
     [:buffer-load-file path]]))

(defcommand "switch-to-buffer" "Switch to a different buffer by name."
  (fn [] [[:prompt "Switch to buffer: " "hammock.commands/switch-to-buffer-cb" :buffer]]))

(defn switch-to-buffer-cb [name]
  [[:buffer-switch name]])

(defcommand "kill-buffer" "Kill a buffer by name."
  (fn [] [[:prompt "Kill buffer: " "hammock.commands/kill-buffer-cb" :buffer]]))

(defn kill-buffer-cb [name]
  [[:buffer-destroy name]])

(defcommand "execute-extended-command" "Run a command by name (M-x)."
  (fn [] [[:prompt "M-x " "hammock.commands/execute-extended-command-cb" :command]]))

(defn execute-extended-command-cb [name]
  ;; Re-dispatch to the named command
  (let [f (get @state/*commands* name)]
    (if f
      (f)
      [[:message (str "Unknown command: " name)]])))

;; ---- Shell ----

(defcommand "shell" "Start an interactive shell in a buffer."
  (fn [] [[:shell-start]]))

(defcommand "shell-command" "Run a shell command and display the output."
  (fn [] [[:prompt "Shell command: " "hammock.commands/shell-command-cb" :none]]))

(defn shell-command-cb [cmd]
  [[:shell-command cmd]])

;; ---- Git (magit-style) ----

(defn- git-status-effects
  "Return effects to populate the *git-status* buffer with current status."
  []
  (let [content (git/format-status)]
    [[:buffer-set-read-only false]
     [:buffer-set-contents content]
     [:point-to-buffer-start]
     [:buffer-set-modified false]
     [:buffer-set-read-only true]
     [:buffer-set-mode "Git-Status"]]))

(defcommand "magit-status" "Show git status in a dedicated buffer."
  (fn []
    (let [existing (some #(= (:name %) "*git-status*") (fx/buffers))]
      (if existing
        (into [[:buffer-switch "*git-status*"]] (git-status-effects))
        (into [[:buffer-create "*git-status*"]
               [:buffer-switch "*git-status*"]]
              (git-status-effects))))))

(defcommand "git-stage" "Stage the file at point."
  (fn []
    (let [path (git/extract-file-from-status-line (fx/current-line))
          pt (fx/point)]
      (if path
        (do (git/stage-file path)
            (-> (git-status-effects)
                (conj [:point-set pt])))
        [[:message "No file at point"]]))))

(defcommand "git-unstage" "Unstage the file at point."
  (fn []
    (let [path (git/extract-file-from-status-line (fx/current-line))
          pt (fx/point)]
      (if path
        (do (git/unstage-file path)
            (-> (git-status-effects)
                (conj [:point-set pt])))
        [[:message "No file at point"]]))))

(defcommand "git-refresh" "Refresh the git status buffer."
  (fn [] (git-status-effects)))

(defcommand "git-diff" "Show diff for the file at point."
  (fn []
    (let [path (git/extract-file-from-status-line (fx/current-line))]
      (if path
        (let [diff-text (or (not-empty (git/diff-file-cached path))
                            (git/diff-file path)
                            "No changes")]
          [[:buffer-create "*git-diff*"]
           [:window-split-below]
           [:window-other]
           [:buffer-switch "*git-diff*"]
           [:buffer-set-read-only false]
           [:buffer-set-contents diff-text]
           [:point-to-buffer-start]
           [:buffer-set-modified false]
           [:buffer-set-read-only true]
           [:buffer-set-mode "Diff"]])
        [[:message "No file at point"]]))))

(defcommand "git-toggle-section" "Move to the next section header."
  (fn []
    [[:search-forward "\n\n"]
     [:point-forward 1]]))

(defcommand "git-visit-file" "Open the file at point for editing."
  (fn []
    (let [path (git/extract-file-from-status-line (fx/current-line))]
      (if path
        (let [name (last (str/split path #"/"))]
          [[:buffer-create name]
           [:buffer-load-file path]
           [:buffer-switch name]])
        [[:message "No file at point"]]))))

(defcommand "git-quit" "Leave the git status buffer."
  (fn []
    (let [bufs (fx/buffers)
          target (or (first (keep #(when (and (not= (:name %) "*git-status*")
                                              (not= (:name %) "*git-diff*"))
                                     (:name %))
                                  bufs))
                     "*scratch*")]
      [[:buffer-switch target]])))

(defcommand "git-commit" "Commit staged changes."
  (fn []
    [[:prompt "Commit message: " "hammock.commands/git-commit-cb" :none]]))

(defn git-commit-cb [msg]
  (let [result (git/git-commit-with-msg msg)]
    (into [[:message result]] (git-status-effects))))

(defcommand "diff-quit" "Close the diff window and return to git status."
  (fn []
    [[:window-delete]
     [:buffer-switch "*git-status*"]]))

;; ---- Markdown mode ----

(def ^:private link-history (atom []))

(defcommand "markdown-follow-link" "Follow the link at point."
  (fn []
    (let [state (fx/editor)
          line (:current-line state)
          col (:col state)
          link (md/link-at-point line col)]
      (case (:type link)
        :bidir
        (let [filename (str (:target link) ".md")]
          (swap! link-history conj {:buffer (:current-buffer state)
                                    :point (:point state)})
          [[:buffer-create filename]
           [:buffer-load-file filename]
           [:buffer-switch filename]
           [:buffer-set-mode "Markdown"]])

        :markdown
        (let [target (:target link)]
          (swap! link-history conj {:buffer (:current-buffer state)
                                    :point (:point state)})
          (cond
            (or (str/starts-with? target "http://")
                (str/starts-with? target "https://"))
            [[:shell-command (str "open '" target "'")]
             [:message (str "Opened " target " in browser")]]

            (str/starts-with? target "#")
            [[:search-forward (str "\n" target)]
             [:point-forward 1]]

            :else
            (let [name (last (str/split target #"/"))]
              [[:buffer-create name]
               [:buffer-load-file target]
               [:buffer-switch name]])))

        ;; :none - not on a link, insert newline
        [[:insert "\n"] [:reset-target-col]]))))

(defcommand "markdown-next-link" "Jump to the next link in the buffer."
  (fn []
    [[:search-forward "["]]))

(defcommand "markdown-next-heading" "Jump to the next heading."
  (fn []
    [[:search-forward "\n#"]
     [:point-forward 1]]))

(defcommand "markdown-prev-heading" "Jump to the previous heading."
  (fn []
    [[:search-backward "\n#"]
     [:point-forward 1]]))

(defcommand "markdown-go-back" "Go back in link navigation history."
  (fn []
    (if (empty? @link-history)
      [[:message "No more history"]]
      (let [{:keys [buffer point]} (peek @link-history)]
        (swap! link-history pop)
        [[:buffer-switch buffer]
         [:point-set point]]))))
