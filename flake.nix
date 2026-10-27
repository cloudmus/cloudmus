{
  description = "CloudMus — enjoy listening to your clouds";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};

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
      cloudmus = pkgs.callPackage ./nix/package.nix {
        inherit pkgs;
        inherit versionSuffix;
        src = self;
      };
    in
    {
      packages.${system} = {
        default = cloudmus;
        cloudmus = cloudmus;
      };

      apps.${system}.default = {
        type = "app";
        program = "${cloudmus}/bin/cloudmus";
        meta.description = "CloudMus — enjoy listening to your clouds";
      };

      # For a NixOS configuration: nixpkgs.overlays = [ inputs.cloudmus.overlays.default ];
      # then pkgs.cloudmus, or pkgs.cloudmus.override { enableSentry = false; }.
      overlays.default = final: _prev: {
        cloudmus = final.callPackage ./nix/package.nix {
          pkgs = final;
          inherit versionSuffix;
          src = self;
        };
      };

      devShells.${system}.default = pkgs.mkShell {
        inputsFrom = [ cloudmus ];
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
    };
}
