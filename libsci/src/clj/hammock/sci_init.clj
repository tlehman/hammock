(ns hammock.sci-init
  "Initializes the SCI context for use by the C entry points.
   This namespace is loaded at build time to ensure SCI classes
   are included in the native image."
  (:require [sci.core :as sci]))

(defn create-context
  "Create a fresh SCI context with standard Clojure namespaces."
  []
  (sci/init {:namespaces {}}))

(defn eval-in-context
  "Evaluate a string in the given SCI context, return result."
  [ctx code-str]
  (sci/eval-string* ctx code-str))
