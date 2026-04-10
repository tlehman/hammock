(ns build
  (:require [clojure.tools.build.api :as b]))

(def class-dir "target/classes")
(def uber-file "target/libsci.jar")

;; Basis with GraalVM SDK for javac compilation
(def compile-basis (b/create-basis {:project "deps.edn"
                                    :aliases [:build]}))

;; Basis without GraalVM SDK for uber JAR
(def uber-basis (b/create-basis {:project "deps.edn"}))

(defn clean [_]
  (b/delete {:path "target"}))

(defn uber [_]
  (clean nil)
  (b/copy-dir {:src-dirs ["src/clj"]
               :target-dir class-dir})
  ;; Compile Java with GraalVM SDK on classpath (compile-time only)
  (b/javac {:src-dirs ["src/java"]
            :class-dir class-dir
            :basis compile-basis
            :javac-opts ["--release" "21"]})
  ;; Build uber JAR WITHOUT GraalVM SDK (native-image provides it)
  (b/uber {:class-dir class-dir
           :uber-file uber-file
           :basis uber-basis}))
