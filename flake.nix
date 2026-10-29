{
  description = "CloudMus — enjoy listening to your clouds";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      # Linux only: the front is Qt and the backends are CPython processes, so
      # nothing in the package is architecture-specific.
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forEachSystem = nixpkgs.lib.genAttrs systems;

      # A flake cannot see the tags of the source it was fetched from — self
      # carries rev and shortRev, not the ref — so the package cannot tell a
      # release commit from the ones after it. Every Nix build therefore names
      # its commit, and CrashReporter files it as a development build: no NixOS
      # crash ends up mixed into a released version's.
      versionSuffix =
        if self ? dirtyShortRev then
          "-g${self.dirtyShortRev}"
        else if self ? shortRev then
          "-g${self.shortRev}"
        else
          "";

      # The package definition lives in nix/package.nix; the sources are this
      # checkout, so a build needs no downloads of its own and follows the
      # commits. Sentry's DSN and the analytics measurement ID are arguments
      # there, not secrets of this workflow: a flake build has to be
      # reproducible, and a value taken from the environment would make the
      # CI build and a user's own build different store paths.
      cloudmusFor =
        pkgs:
        pkgs.callPackage ./nix/package.nix {
          inherit pkgs;
          src = self;
          inherit versionSuffix;
        };
    in
    {
      packages = forEachSystem (
        system:
        let
          cloudmus = cloudmusFor nixpkgs.legacyPackages.${system};
        in
        {
          default = cloudmus;
          cloudmus = cloudmus;
        }
      );

      apps = forEachSystem (system: {
        default = {
          type = "app";
          program = "${self.packages.${system}.cloudmus}/bin/cloudmus";
          meta.description = "CloudMus — enjoy listening to your clouds";
        };
      });

      # For a NixOS configuration: nixpkgs.overlays = [ inputs.cloudmus.overlays.default ];
      # then pkgs.cloudmus, or pkgs.cloudmus.override { enableSentry = false; }.
      overlays.default = final: _prev: {
        cloudmus = final.callPackage ./nix/package.nix {
          pkgs = final;
          src = self;
          inherit versionSuffix;
        };
      };

      devShells = forEachSystem (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.cloudmus ];
            packages = [
              (pkgs.python3.withPackages (ps: [
                ps.jinja2
                ps.pyyaml
              ]))
              # Not inputs of the package itself: docs/development.md recommends
              # both for a source build, and fronts/qt/CMakeLists.txt uses them
              # with no flags once it finds them.
              pkgs.ccache
              pkgs.mold
            ];
            shellHook = ''
              echo "CloudMus dev shell. To build the front outside Nix:"
              echo "  cmake -S fronts/qt -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPYTHON3_EXECUTABLE=$(command -v python3)"
              echo "  cmake --build build"
            '';
          };
        }
      );
    };
}
