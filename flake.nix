{
  description = "Hammock editor dev environment with GraalVM for libsci";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        graalvm = pkgs.graalvmPackages.graalvm-ce;
      in {
        devShells.default = pkgs.mkShell {
          buildInputs = [
            graalvm
            pkgs.ncurses
            pkgs.clojure
            pkgs.maven
            pkgs.pkg-config
          ];

          shellHook = ''
            export GRAALVM_HOME="${graalvm}"
            export JAVA_HOME="${graalvm}"
            echo "GraalVM $(native-image --version 2>&1) ready"
          '';
        };
      });
}
