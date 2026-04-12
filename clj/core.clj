(ns hammock.core
  (:require [hammock.state :as state]
            [hammock.effects :as fx]))

(defn hammock-version [] "0.2.2")

;; Backward compat: alias the old atom name
(defonce *editor-state* state/*editor*)

;; Utility functions
(defn say [msg] msg)

;; Startup message (returned as value, displayed by C via message())
(str "Hammock " (hammock-version) " - Clojure ready")
