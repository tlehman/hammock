(ns hammock.commands
  (:require [hammock.state :as state]
            [hammock.effects :as fx]
            [hammock.core :as core]
            [hammock.git :as git]
            [hammock.markdown :as md]
            [hammock.symbols :as symbols]
            [clojure.string :as str]))

;; Register a command: associates a name string with a map of {:fn f :doc docstring}
(defn defcommand
  ([name f] (defcommand name "" f))
  ([name docstring f]
   (swap! state/*commands* assoc name {:fn f :doc docstring})))

;; Dispatch a command by name, return effect vector.
;; Any exception thrown by the command body is caught and converted to a
;; [:message ...] effect so the error surfaces in the echo area and the
;; *Messages* buffer instead of being lost.
(defn dispatch [name]
  (let [entry (get @state/*commands* name)
        f (if (map? entry) (:fn entry) entry)]
    (if f
      (try
        (f)
        (catch Exception e
          [[:message (str "ERROR in " name ": " e)]]))
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

(defcommand "yank-pop"
  "Replace the just-yanked text with the previous kill-ring entry."
  (fn [] [[:yank-pop]]))

(defcommand "backward-kill-word"
  "Kill the word before point; push it onto the kill ring."
  (fn [] [[:set-mark]
          [:point-backward-word]
          [:kill-region]]))

(defcommand "delete-paragraph"
  "Kill from point to the end of the current paragraph."
  (fn [] [[:set-mark]
          [:point-forward-paragraph]
          [:kill-region]]))

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

(defcommand "view-messages" "Switch to the *Messages* buffer."
  (fn [] [[:buffer-switch "*Messages*"]
          [:point-to-buffer-end]]))

(defcommand "clear-messages" "Erase the contents of the *Messages* buffer."
  (fn [] [[:buffer-switch "*Messages*"]
          [:buffer-set-read-only false]
          [:buffer-set-contents ""]
          [:buffer-set-modified false]]))

(defcommand "list-buffers" "Display a list of all buffers."
  (fn []
    (reset! buflist-marks #{})
    (let [state (fx/editor)
          content (format-buffer-list (:buffers state)
                                     (:current-buffer state)
                                     @buflist-marks)]
      [[:buffer-create "*Buffer List*"]
       [:display-buffer "*Buffer List*"]
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
  (let [name (last (clojure.string/split path #"/"))
        existing (some #(= (:name %) name) (fx/buffers))]
    (if existing
      [[:buffer-switch name]]
      [[:buffer-create name]
       [:buffer-switch name]
       [:buffer-load-file path]])))

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
        (let [name (last (str/split path #"/"))
              existing (some #(= (:name %) name) (fx/buffers))]
          (if existing
            [[:buffer-switch name]]
            [[:buffer-create name]
             [:buffer-switch name]
             [:buffer-load-file path]]))
        [[:message "No file at point"]]))))

(defcommand "git-quit" "Quit git mode: close git buffers and return to previous buffer."
  (fn []
    (let [bufs (fx/buffers)
          git-names #{"*git-status*" "*git-diff*" "*git-log*" "*git-show*"}
          target (or (first (keep #(when-not (git-names (:name %)) (:name %)) bufs))
                     "*scratch*")]
      [[:buffer-switch target]
       [:window-delete-others]
       [:buffer-destroy "*git-status*"]
       [:buffer-destroy "*git-diff*"]
       [:buffer-destroy "*git-log*"]
       [:buffer-destroy "*git-show*"]])))

(defn- git-log-effects
  "Return effects to populate the *git-log* buffer with recent commits."
  []
  (let [content (git/git-log 50)]
    [[:buffer-set-read-only false]
     [:buffer-set-contents content]
     [:point-to-buffer-start]
     [:buffer-set-modified false]
     [:buffer-set-read-only true]
     [:buffer-set-mode "Git-Log"]]))

(defcommand "git-log" "Show recent git log in a dedicated buffer."
  (fn []
    (let [existing (some #(= (:name %) "*git-log*") (fx/buffers))]
      (if existing
        (into [[:buffer-switch "*git-log*"]] (git-log-effects))
        (into [[:buffer-create "*git-log*"]
               [:buffer-switch "*git-log*"]]
              (git-log-effects))))))

(defcommand "git-log-quit" "Close the git log buffer."
  (fn []
    (let [bufs (fx/buffers)
          target (or (some #(when (= (:name %) "*git-status*") (:name %)) bufs)
                     (first (keep #(when-not (= (:name %) "*git-log*") (:name %)) bufs))
                     "*scratch*")]
      [[:buffer-switch target]
       [:buffer-destroy "*git-log*"]])))

(defcommand "git-log-show"
  "Show the commit at point (git show <sha>) in a split-below pop-up."
  (fn []
    (let [line (fx/current-line)
          sha  (when line (first (str/split (str/trim line) #"\s+")))]
      (if (seq sha)
        (let [content (or (not-empty (git/git-show sha))
                          (str "No output for " sha))]
          [[:buffer-create "*git-show*"]
           [:window-split-below]
           [:window-other]
           [:buffer-switch "*git-show*"]
           [:buffer-set-read-only false]
           [:buffer-set-contents content]
           [:point-to-buffer-start]
           [:buffer-set-modified false]
           [:buffer-set-read-only true]
           [:buffer-set-mode "Diff"]])
        [[:message "No commit at point"]]))))

(defcommand "git-fetch" "Fetch from remote and refresh status."
  (fn []
    (let [result (git/git-fetch)
          msg (if (str/blank? result) "Fetch complete" result)]
      (into [[:message msg]] (git-status-effects)))))

(defcommand "git-pull" "Pull from remote and refresh status."
  (fn []
    (let [result (git/git-pull)
          msg (if (str/blank? result) "Pull complete" result)]
      (into [[:message msg]] (git-status-effects)))))

(defcommand "git-push" "Push to remote and refresh status."
  (fn []
    (let [result (git/git-push)
          msg (if (str/blank? result) "Push complete" result)]
      (into [[:message msg]] (git-status-effects)))))

(defcommand "git-commit" "Commit staged changes."
  (fn []
    [[:prompt "Commit message: " "hammock.commands/git-commit-cb" :none]]))

(defn git-commit-cb [msg]
  (let [result (git/git-commit-with-msg msg)]
    (into [[:message result]] (git-status-effects))))

(defcommand "diff-quit" "Close the diff window and return to git status or git log."
  (fn []
    (let [bufs (fx/buffers)
          target (or (some #(when (= (:name %) "*git-log*") (:name %)) bufs)
                     "*git-status*")]
      [[:window-delete]
       [:buffer-switch target]])))

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
        (let [filename (str (:target link) ".md")
              existing (some #(= (:name %) filename) (:buffers state))]
          (swap! link-history conj {:buffer (:current-buffer state)
                                    :point (:point state)})
          (if existing
            [[:buffer-switch filename]]
            [[:buffer-create filename]
             [:buffer-switch filename]
             [:buffer-load-file filename]
             [:buffer-set-mode "Markdown"]]))

        :markdown
        (let [target (:target link)]
          (swap! link-history conj {:buffer (:current-buffer state)
                                    :point (:point state)})
          (cond
            (str/starts-with? target "hammock://")
            (let [rest (subs target (count "hammock://"))
                  slash (str/index-of rest "/")
                  kind (if slash (subs rest 0 slash) rest)
                  arg (if slash (subs rest (inc slash)) "")]
              (case kind
                "buffer"  [[:buffer-switch arg]]
                "command" [[:run-command arg]]
                "file"    (let [name (last (str/split arg #"/"))
                                existing (some #(= (:name %) name) (:buffers state))]
                            (if existing
                              [[:buffer-switch name]]
                              [[:buffer-create name]
                               [:buffer-switch name]
                               [:buffer-load-file arg]]))
                [[:message (str "Unknown hammock:// link: " target)]]))

            (or (str/starts-with? target "http://")
                (str/starts-with? target "https://"))
            [[:shell-command (str "open '" target "'")]
             [:message (str "Opened " target " in browser")]]

            (str/starts-with? target "#")
            [[:search-forward (str "\n" target)]
             [:point-forward 1]]

            :else
            (let [name (last (str/split target #"/"))
                  existing (some #(= (:name %) name) (:buffers state))]
              (if existing
                [[:buffer-switch name]]
                [[:buffer-create name]
                 [:buffer-switch name]
                 [:buffer-load-file target]]))))

        ;; :none - not on a link, insert newline
        [[:insert "\n"] [:reset-target-col]]))))

(defcommand "markdown-next-link" "Jump to the next link in the buffer."
  (fn []
    [[:search-forward "["]]))

(defcommand "markdown-prev-link" "Jump to the previous link in the buffer."
  (fn []
    [[:search-backward "["]]))

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

;; ---- Symbol explorer ----

(defn- first-namespace-for-pane []
  (let [idx (symbols/ensure!)]
    (or (first (keys (:namespaces idx)))
        (first (keys (:modules idx)))
        "(all)")))

(defn- symbol-browser-layout-effects []
  (let [ns-text (symbols/format-namespace-pane)
        first-ns (first-namespace-for-pane)
        sym-text (symbols/format-symbol-pane first-ns)]
    [[:window-delete-others]
     [:buffer-create "*Symbols*"]
     [:buffer-switch "*Symbols*"]
     [:buffer-set-read-only false]
     [:buffer-set-contents ns-text]
     [:point-to-buffer-start]
     [:buffer-set-modified false]
     [:buffer-set-read-only true]
     [:buffer-set-mode "Symbol-Browser"]
     [:window-split-right]
     [:window-other]
     [:buffer-create "*Symbol-Detail*"]
     [:buffer-switch "*Symbol-Detail*"]
     [:buffer-set-read-only false]
     [:buffer-set-contents sym-text]
     [:point-to-buffer-start]
     [:buffer-set-modified false]
     [:buffer-set-read-only true]
     [:buffer-set-mode "Symbol-Detail"]
     [:window-other]]))

(defcommand "browse-symbols"
  "Open the namespace/symbol explorer."
  (fn []
    (symbols/ensure!)
    (symbol-browser-layout-effects)))

(defcommand "symbrowse-select"
  "Populate the right pane with symbols of the namespace at point."
  (fn []
    (let [line (fx/current-line)
          selector (symbols/namespace-at-line line)]
      (if selector
        (let [text (symbols/format-symbol-pane selector)]
          ;; window-other moves into whatever the other pane is holding —
          ;; possibly a source file if the user just jumped to a symbol —
          ;; so explicitly switch to *Symbol-Detail* before overwriting.
          [[:window-other]
           [:buffer-create "*Symbol-Detail*"]
           [:buffer-switch "*Symbol-Detail*"]
           [:buffer-set-read-only false]
           [:buffer-set-contents text]
           [:point-to-buffer-start]
           [:buffer-set-modified false]
           [:buffer-set-read-only true]
           [:buffer-set-mode "Symbol-Detail"]])
        [[:message "Not on a namespace line"]]))))

(defn- visit-symbol-effects [sym]
  (let [file (:file sym)
        line (:line sym)
        base (when file (last (str/split file #"/")))]
    (cond
      (or (str/blank? file) (zero? (or line 0)))
      [[:message (str "No source location for " (:name sym))]]

      :else
      ;; Keep the explorer buffers alive and the other pane visible so the
      ;; user can C-o back to *Symbols* and pick another namespace/symbol.
      [[:buffer-create base]
       [:buffer-switch base]
       [:buffer-load-file file]
       [:point-to-line line]])))

(defcommand "symbrowse-visit"
  "Jump to the definition of the symbol at point in the explorer."
  (fn []
    (let [line (fx/current-line)
          nm (symbols/name-at-line line)]
      (if (str/blank? nm)
        [[:message "No symbol at point"]]
        (let [idx (symbols/ensure!)
              hit (or (symbols/find-command nm)
                      (first (filter #(= nm (:name %))
                                     (mapcat val (:namespaces idx))))
                      (first (filter #(= nm (:name %))
                                     (mapcat val (:modules idx)))))]
          (if hit
            (visit-symbol-effects hit)
            [[:message (str "Symbol not in index: " nm)]]))))))

(defcommand "symbrowse-refresh"
  "Rebuild the symbol index and re-render the explorer."
  (fn []
    (symbols/rebuild!)
    (symbol-browser-layout-effects)))

(defcommand "symbrowse-quit"
  "Close the symbol explorer."
  (fn []
    [[:buffer-destroy "*Symbol-Detail*"]
     [:buffer-destroy "*Symbols*"]
     [:window-delete-others]
     [:buffer-switch "*scratch*"]]))

;; ---- Apropos ----

(defn- apropos-layout-effects [pattern]
  (let [text (symbols/format-apropos pattern)]
    [[:buffer-create "*Apropos*"]
     [:window-split-below]
     [:window-other]
     [:buffer-switch "*Apropos*"]
     [:buffer-set-read-only false]
     [:buffer-set-contents text]
     [:point-to-buffer-start]
     [:buffer-set-modified false]
     [:buffer-set-read-only true]
     [:buffer-set-mode "Apropos"]]))

(defcommand "apropos"
  "Prompt for a pattern and list matching symbols."
  (fn []
    [[:prompt "Apropos: " "hammock.commands/apropos-cb" :none]]))

(defn apropos-cb [pattern]
  (symbols/ensure!)
  (apropos-layout-effects pattern))

(defcommand "apropos-visit"
  "Jump to the definition of the symbol at point in the apropos buffer."
  (fn []
    (let [line (fx/current-line)
          nm (symbols/apropos-name-at-line line)]
      (if (str/blank? nm)
        [[:message "No symbol at point"]]
        (let [idx (symbols/ensure!)
              hit (or (symbols/find-command nm)
                      (first (filter #(= nm (:name %))
                                     (mapcat val (:namespaces idx))))
                      (first (filter #(= nm (:name %))
                                     (mapcat val (:modules idx)))))]
          (if hit
            (visit-symbol-effects hit)
            [[:message (str "Symbol not in index: " nm)]]))))))

(defcommand "apropos-quit"
  "Close the apropos window."
  (fn [] [[:buffer-destroy "*Apropos*"] [:window-delete]]))

;; ---- Version ----

(defcommand "version" "Show the Hammock version in the minibuffer."
  (fn [] [[:message (str "Hammock " (core/hammock-version))]]))
