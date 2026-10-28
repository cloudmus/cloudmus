# The CloudMus package: the Qt6 front, the three Python backends and the
# launcher that seeds their manifests. flake.nix at the repository root builds
# it from this checkout.
#
# The arguments below are overridable, e.g. for a build without usage
# statistics and crash reports (see the README):
#
#   pkgs.cloudmus.override { enableAnalytics = false; enableSentry = false; }
{
  pkgs,
  lib ? pkgs.lib,
  src,

  # Reported by the About dialog and compared with the release feed by the
  # update checker. cmake/Version.cmake takes it from `git describe`, and a
  # store copy of the sources has nothing to describe — the flake's source is
  # the git-tracked files without .git, so without this the app would call
  # itself 0.1.0, project()'s base version. CHANGELOG.md carries it instead:
  # the release commit renames its top heading to x.y.z and the tag is put on
  # that commit, so a build from the tag and a build from develop both report
  # the last released version, and a release needs nothing bumped here.
  # Override it for a checkout whose changelog is ahead of the last release.
  version ?
    let
      heading = lib.findFirst (line: lib.hasPrefix "# " line) ""
        (lib.splitString "\n" (builtins.readFile "${src}/CHANGELOG.md"));
      parsed = lib.head (lib.splitString " (" (lib.removePrefix "# " heading));
    in
    assert lib.assertMsg (builtins.match "[0-9]+\\.[0-9]+\\.[0-9]+" parsed != null)
      "CloudMus: the first heading of ${toString src}/CHANGELOG.md is not x.y.z (pass `version`)";
    parsed,

  # Added to the reported version only, not to the changelog's: a Nix build
  # never claims to be a release — it says which commit it names
  # ("0.6.4-gabc1234"), because a store copy of the sources cannot tell the
  # release commit from the ones after it. CrashReporter then files it as a
  # development build, and the About dialog pins the exact tree — as
  # `git describe` does for a checkout of the front. flake.nix passes it.
  versionSuffix ? "",

  # Usage statistics and crash reports, on by default as in the AppImage. The
  # values are the ones the released AppImage carries: a client-side DSN is
  # public by design — it can be read from any published build — and the
  # measurement ID is sent by every AppImage anyway. Replace them with your own
  # or switch the reports off; docs/analytics.md lists what is sent.
  enableSentry ? true,
  sentryDsn ? "https://027c004355643646c74c7d35c3d8c6e5@o4512222310825984.ingest.de.sentry.io/4512222349492304",
  enableAnalytics ? true,
  analyticsMeasurementId ? "G-CVW8W08KZ8",
}:

assert lib.assertMsg (enableSentry -> sentryDsn != "")
  "CloudMus: enableSentry needs a sentryDsn (or set enableSentry = false)";
assert lib.assertMsg (enableAnalytics -> analyticsMeasurementId != "")
  "CloudMus: enableAnalytics needs an analyticsMeasurementId (or set enableAnalytics = false)";

let
  fullVersion = "${version}${versionSuffix}";

  # --- Generated protocol code --------------------------------------------
  # protocol/codegen/generate.py renders both language bindings from
  # protocol/schema/*.yaml + methods.yaml. CMake regenerates the C++ ones
  # itself into fronts/qt/generated/, but the Python ones land in
  # backends/py-rpc-common/rpc_common/generated/ and are imported by
  # rpc_common and by every backend, so they must exist before that package is
  # built. Both output directories are gitignored, so the checkout has no
  # pregenerated copy.
  codegen = pkgs.python3.withPackages (ps: [
    ps.jinja2
    ps.pyyaml
  ]);

  pythonStubs = pkgs.runCommand "cloudmus-python-protocol-stubs" { nativeBuildInputs = [ codegen ]; } ''
    cp -r ${src} repo
    # The store copy is read-only, codegen writes next to the sources.
    chmod -R u+w repo
    cd repo
    python protocol/codegen/generate.py --lang python
    mkdir -p $out
    cp -r backends/py-rpc-common/rpc_common/generated $out/
  '';

  # --- Python backends -----------------------------------------------------
  # Upstream pip-installs these four distributions into the bundled CPython of
  # the AppImage (packaging/appimage/build-in-docker.sh); here they are
  # ordinary packages of the flake's own interpreter. The front spawns them by
  # the absolute interpreter path from their manifest.
  python = pkgs.python3;

  rpcCommon = python.pkgs.buildPythonPackage {
    pname = "cloudmus-rpc-common";
    version = "0.1.0";
    pyproject = true;
    src = "${src}/backends/py-rpc-common";
    build-system = [ python.pkgs.setuptools ];
    dependencies = with python.pkgs; [
      jsonschema
      pyyaml
    ];
    postPatch = ''
      cp -r ${pythonStubs}/generated rpc_common/generated
    '';
    doCheck = false;
  };

  # The PyPI library the Yandex Music backend is built on: nixpkgs does not
  # package it, so it is built here. 3.2.2 is the version the AppImage shipped
  # when the packaging was written (usr/python-runtime/lib/python3.11/
  # site-packages/yandex_music-3.2.2.dist-info there); the backend asks for
  # >=3.0.0. This pin is bumped by hand: the library's dependencies and extras
  # change between releases, so a bump means re-reading its metadata.
  #
  # The dependency list is not guessed — it is what that release declares
  # (PEP 658 metadata of .../yandex_music-3.2.2-py3-none-any.whl):
  #   Requires-Python: >=3.8
  #   Requires-Dist: requests[socks]
  #   Requires-Dist: typing-extensions>=4.4.0
  # Its extras (async/crypto/orjson/ujson/pydantic-core/ynison) are not needed:
  # the backend imports only the sync Client, yandex_music.utils.request.Request
  # and yandex_music.exceptions, and nothing from aiohttp/aiofiles/
  # cryptography/ujson/betterproto/websockets.
  yandexMusic = python.pkgs.buildPythonPackage rec {
    pname = "yandex-music";
    version = "3.2.2";
    pyproject = true;
    # fetchPypi builds its URL from pname, and PyPI's source shorthand only
    # exists in the underscored form (packages/source/y/yandex_music/
    # yandex_music-3.2.2.tar.gz is the real file; the dashed form 404s).
    src = pkgs.fetchPypi {
      pname = "yandex_music";
      inherit version;
      hash = "sha256-6wtCQFIc4tBP/4dsna2n7OZUWTeZe6Eg/PqoE++CVrI=";
    };
    # pyproject.toml: requires = ["hatchling>=1.27"], backend hatchling.build
    build-system = [ python.pkgs.hatchling ];
    dependencies = with python.pkgs; [
      requests
      pysocks # the "socks" extra of requests
      typing-extensions
    ];
    # The sdist ships its own suite (255 test files); it runs offline and
    # passes in the sandbox: 2367 passed, 59 skipped. The skips are the suite's
    # own gates for the optional extras deliberately not installed
    # (orjson/ujson/pydantic_core/betterproto), not failures.
    nativeCheckInputs = [ python.pkgs.pytestCheckHook ];
  };

  # yt-dlp's nightlies are where YouTube's extraction fixes land, so the
  # backend's pyproject.toml pins one exactly ("bumped by hand when YouTube
  # breaks stream extraction"). That very version is built here rather than
  # taken from nixpkgs, which carries a release weeks older — the pin is the
  # whole point of it.
  ytDlp = python.pkgs.buildPythonPackage rec {
    pname = "yt-dlp";
    version = "2026.9.27.232945.dev0";
    pyproject = true;
    # fetchPypi builds its URL from pname, and PyPI's source shorthand exists
    # only in the underscored form (as for yandex_music above).
    src = pkgs.fetchPypi {
      pname = "yt_dlp";
      inherit version;
      hash = "sha256-8JFgvjfFuy5R8tF/aFflEI81Scqt8tl3AJoU3/W08eM=";
    };
    # pyproject.toml: requires = ["hatchling>=1.27.0"], backend hatchling.build
    build-system = [ python.pkgs.hatchling ];
    # Its core declares no runtime dependencies — brotli, certifi, curl_cffi
    # and the rest are extras, and playback.py imports only YoutubeDL, the
    # networking exceptions and DownloadError. The sdist ships no test suite.
    doCheck = false;
  };

  backendLocalFolder = python.pkgs.buildPythonPackage {
    pname = "cloudmus-backend-local-folder";
    version = "0.1.0";
    pyproject = true;
    src = "${src}/backends/local-folder";
    build-system = [ python.pkgs.setuptools ];
    dependencies = [
      rpcCommon
      python.pkgs.mutagen
    ];
    doCheck = false;
  };

  backendYoutubeMusic = python.pkgs.buildPythonPackage {
    pname = "cloudmus-backend-youtube-music";
    version = "0.1.0";
    pyproject = true;
    src = "${src}/backends/youtube-music";
    build-system = [ python.pkgs.setuptools ];
    dependencies = [
      rpcCommon
      python.pkgs.pysocks # SOCKS proxies (ALL_PROXY), see docs/protocol.md
      python.pkgs.ytmusicapi
      ytDlp
    ];
    doCheck = false;
  };

  backendYandexMusic = python.pkgs.buildPythonPackage {
    pname = "cloudmus-backend-yandex-music";
    version = "0.1.0";
    pyproject = true;
    src = "${src}/backends/yandex-music";
    build-system = [ python.pkgs.setuptools ];
    dependencies = [
      rpcCommon
      yandexMusic
      python.pkgs.pysocks # SOCKS proxies, as in the backend's own pyproject
    ];
    # Its 84 tests run offline and pass in the sandbox; pytest-asyncio for the
    # @pytest.mark.asyncio ones.
    nativeCheckInputs = [
      python.pkgs.pytestCheckHook
      python.pkgs.pytest-asyncio
    ];
  };

  # All three sources the AppImage ships.
  backendPython = python.withPackages (_: [
    rpcCommon
    backendLocalFolder
    backendYandexMusic
    backendYoutubeMusic
  ]);

  # Backend manifests. The front reads installed backends only from
  # $XDG_CONFIG_HOME/cloudmus/backends.d/*.json (src/Rpc/BackendManifest.cpp,
  # discoverManifests) — its other discovery paths are Windows-only or a
  # dev-mode repo checkout. Upstream's AppRun writes the AppImage's manifests
  # there on every launch for exactly this reason; this does the same, and like
  # AppRun it writes only its own three files, so a manifest a user put there
  # for their own backend stays untouched.
  #
  # Each manifest is the backend's own (backends/<id>/manifest.json) with the
  # two values this package owns: argv[0], an absolute path on purpose — the
  # front passes its own environment on to the backends, so a bare python3
  # would be the user's, which does not have these packages (see
  # BackendManifest.cpp) — and the icon. The rest, protocolVersion above all,
  # stays the backend's to declare, so a bump there needs nothing here.
  backendIds = [
    "local-folder"
    "yandex-music"
    "youtube-music"
  ];

  # Written into $out in postInstall and copied from there by seedBackends:
  # whole files, so no shell ever has to quote their JSON.
  seedManifest =
    id:
    let
      backend = lib.importJSON "${src}/backends/${id}/manifest.json";
    in
    builtins.toJSON (
      backend
      // {
        argv = [
          "${backendPython}/bin/python3"
          "-m"
          (builtins.elemAt backend.argv 2)
        ];
        icon = "${placeholder "out"}/share/cloudmus/backend-icons/${id}.svg";
      }
    );

  # $1 is the package's own $out (an AppImage has a fresh mount point every
  # launch, so AppRun cannot embed these paths; a store path is stable, but the
  # script is still run every launch — it is idempotent).
  seedBackends = pkgs.writeShellScript "cloudmus-seed-backends" ''
    out="$1"

    # A broken manifest directory must not stop the app from starting:
    # upstream's AppRun warns and goes on, this exits 0 and goes on.
    configHome="''${XDG_CONFIG_HOME:-''${HOME:-/nonexistent}/.config}"
    # XDG_CONFIG_HOME counts only when absolute, like QStandardPaths.
    case "$configHome" in
      /*) ;;
      *) configHome="''${HOME:-/nonexistent}/.config" ;;
    esac
    dir="$configHome/cloudmus/backends.d"
    mkdir -p "$dir" 2>/dev/null || exit 0

    # Whole files, so no JSON is ever quoted into a shell. install sets the mode
    # itself: the copies in the store are read-only, and a plain cp would hand
    # that mode to the config file, which the next launch — and the AppImage's
    # AppRun, writing the same file — then could not overwrite. The glob is
    # exactly the three manifests this package ships (postInstall), so a
    # manifest a user put there for their own backend stays untouched.
    install -m644 "$out"/share/cloudmus/backends.d/*.json "$dir"/ || exit 0
  '';
in
pkgs.stdenv.mkDerivation {
  pname = "cloudmus";
  inherit src;
  version = fullVersion;

  nativeBuildInputs = [
    pkgs.cmake # fronts/qt/CMakeLists.txt requires >= 3.21
    pkgs.ninja
    pkgs.pkg-config # find_package(PkgConfig) for libmpv
    pkgs.qt6.qttools # Qt6::LinguistTools (lupdate/lrelease), a build-time tool
    pkgs.qt6.wrapQtAppsHook
    pkgs.clang-tools # clang-format: protocol/codegen/generate.py formats the
    # generated C++ with it and warns when it is missing
    pkgs.wayland-scanner # the blur protocols, wayland-scanner client-header/private-code
    codegen # CMake's protocol codegen step
  ];

  buildInputs = [
    pkgs.qt6.qtbase # Core Gui Widgets Network DBus (find_package(Qt6 COMPONENTS ...))
    pkgs.qt6.qtsvg # Qt6::Svg
    # The Wayland *platform* plugin (platforms/libqwayland.so, next to
    # libqxcb.so) comes from qtbase in this nixpkgs release; qtwayland adds its
    # shell/decoration/graphics-integration plugins — the client-side
    # decoration plugin among them, which a compositor that does no server-side
    # decorations needs.
    pkgs.qt6.qtwayland
    pkgs.mpv-unwrapped # libmpv via pkg-config (pkg_check_modules(MPV REQUIRED ...))
    pkgs.taglib # find_package(TagLib 2.2 CONFIG) — 2.2.1 here, so no FetchContent
    pkgs.wayland # wayland-client for the blur protocols
    pkgs.libxcb # xcb: KWin's X11 blur property + X11 global hotkeys
  ]
  # sentry-native from nixpkgs instead of the FetchContent call in
  # fronts/qt/CMakeLists.txt: that one downloads the SDK during the build, and
  # a nix build runs without network.
  ++ lib.optional enableSentry pkgs.sentry-native;

  # Read by fronts/qt/CMakeLists.txt when it configures. Empty means "this
  # build reports nothing", which is what the disabled switches above are for.
  CLOUDMUS_SENTRY_DSN = lib.optionalString enableSentry sentryDsn;
  CLOUDMUS_GA4_MEASUREMENT_ID = lib.optionalString enableAnalytics analyticsMeasurementId;

  # The CMake project is fronts/qt, there is no CMakeLists at the repo root.
  # The cmake hook configures from a "build" subdirectory, so this is relative
  # to that.
  cmakeDir = "../fronts/qt";
  cmakeBuildType = "Release";
  cmakeFlags = [
    # Beats the hook's own PATH lookup: generate.py needs jinja2+pyyaml.
    "-DPYTHON3_EXECUTABLE=${codegen}/bin/python3"
    # See `version` above.
    "-DCLOUDMUS_VERSION_OVERRIDE=${fullVersion}"
    # Explicit: without it the option's default follows the DSN, and a build
    # that asked for no crash reports should not link the SDK at all.
    "-DCLOUDMUS_SENTRY=${if enableSentry then "ON" else "OFF"}"
    # -g1, as packaging/appimage/build-in-docker.sh builds the AppImage: enough
    # debug information for Sentry to name functions and lines in a crash. It
    # ends up in the `debug` output below rather than in the closure.
    "-DCMAKE_C_FLAGS=-g1"
    "-DCMAKE_CXX_FLAGS=-g1"
  ];

  # The project declares no install() rules at all, so (with CMake 4) there is
  # no "install" target and `ninja install` fails outright. Everything is
  # installed from postInstall below instead.
  dontUseNinjaInstall = true;

  # The debug information goes to a separate `debug` output instead of being
  # stripped away: .github/workflows/nix.yml uploads it to Sentry, so a crash
  # from a NixOS build arrives with symbols, like an AppImage's does. The
  # runtime closure does not grow — the symbols live in their own output.
  separateDebugInfo = true;

  # CLOUDMUS_TLS_CA_FILE is not set: it exists because the AppImage's bundled
  # libavformat trusts Debian's CA path, while the nixpkgs one uses the host's
  # /etc/ssl/certs/ca-certificates.crt.

  # Upstream wraps its own AppRun around the app for exactly one reason left
  # here (the other AppRun steps are AppImage-specific): the backend manifests,
  # see postInstall. wrapQtAppsHook does the Qt part on its own — the plugin
  # path, so the xcb/wayland platform plugins are found.

  postInstall = ''
    # fronts/qt/CMakeLists.txt declares no install() rules (upstream's AppImage
    # copies the binary straight out of the build tree), so the binary and the
    # desktop integration are installed here. The cwd of this phase is the
    # cmake build directory, where the CMakeLists' CMAKE_RUNTIME_OUTPUT_DIRECTORY
    # puts the executable.
    install -Dm755 bin/cloudmus-qt $out/bin/cloudmus-qt

    # Launcher: seeds the backend manifests, then starts the app. It is a
    # separate executable rather than makeWrapper's --run because this
    # nixpkgs' makeWrapper is the binary one, which has no --run; cloudmus-qt
    # is wrapped by wrapQtAppsHook as usual.
    # The delimiter is quoted so the build shell leaves "$@" alone (the
    # Nix-level paths are already substituted above).
    cat > $out/bin/cloudmus <<'EOF'
    #!/bin/sh
    # A failed step here must not keep the app from starting.
    ${seedBackends} ${placeholder "out"} || true

    # Where the "launch at login" entry points (Integration/Autostart.cpp). The
    # wrapped binary in the store is no substitute — it skips this script and the
    # environment wrapQtAppsHook set up for it — and neither is our own store
    # path: an installed copy sits behind a link an update moves
    # (/run/current-system/sw/bin/cloudmus, ~/.nix-profile/bin/cloudmus), while a
    # store path keeps starting the version an update replaced, and none at all
    # once a collection removed it. $0 is that link for a launched script; it is
    # not absolute only when something ran this by a bare name.
    launcher=$0
    case "$launcher" in
      /*) ;;
      *) launcher=${placeholder "out"}/bin/cloudmus ;;
    esac
    CLOUDMUS_LAUNCHER=$launcher
    export CLOUDMUS_LAUNCHER
    exec ${placeholder "out"}/bin/cloudmus-qt "$@"
    EOF
    chmod 755 $out/bin/cloudmus

    # Same desktop entry as packaging/appimage/build-in-docker.sh installs into
    # the AppDir, with its Exec= pointing at the launcher — exactly what
    # AppRun's own desktop_entry() does for the AppImage.
    mkdir -p $out/share/applications
    sed 's|^Exec=.*|Exec=cloudmus|' ${src}/packaging/appimage/cloudmus-qt.desktop \
      > $out/share/applications/cloudmus-qt.desktop

    # The window/tray icon, looked up by the app id "cloudmus-qt".
    install -Dm644 ${src}/art/logo.png \
      $out/share/icons/hicolor/512x512/apps/cloudmus-qt.png

    # The manifests the launcher copies (see seedBackends) and the sidebar icons
    # they name.
    mkdir -p $out/share/cloudmus/backends.d
    ${lib.concatStrings (
      map (id: ''
        printf '%s\n' ${lib.escapeShellArg (seedManifest id)} \
          > $out/share/cloudmus/backends.d/${id}.json
        install -Dm644 ${src}/backends/${id}/icon.svg $out/share/cloudmus/backend-icons/${id}.svg
      '') backendIds
    )}
  '';

  meta = {
    description = "Multi-source music player (Yandex Music, YouTube Music, local files)";
    homepage = "https://cloudmus.app";
    license = lib.licenses.mit;
    mainProgram = "cloudmus";
    # Linux on any architecture: nothing here is architecture-specific (the
    # backends are CPython, the front Qt).
    platforms = lib.platforms.linux;
  };
}
