# Developing CloudMus

## Architecture

The UI (**front**) and each music service (**backend**) are separate
processes. A front spawns one child process per backend and talks to it
over JSON-RPC 2.0 via stdin/stdout, framed as NDJSON (one JSON object per
line). The protocol is the only stable contract; everything else is an
implementation detail of the process on either side.

```
      ┌─────────────────────┐      ┌─────────────────────┐
      │    TUI (Textual)    │      │ Qt6 Desktop (C++20) │
      └─────────────────────┘      └─────────────────────┘
                 │                            │
                 └─────────────┴──────────────┘
                               │   JSON-RPC 2.0 / NDJSON over stdin/stdout
                               │   one child process per backend
         ┌─────────────────────┴─────────────────────┐
         │                     │                     │
┌─────────────────┐   ┌─────────────────┐   ┌─────────────────┐
│  Yandex Music   │   │  Local Folder   │   │  YouTube Music  │
│     backend     │   │     backend     │   │     backend     │
└─────────────────┘   └─────────────────┘   └─────────────────┘
```

- **Front and backend don't have to share a language.** Today there's
  Python (the TUI, every backend) and C++/Qt (the desktop app); a front in
  Rust or a backend in Go would work just as well.
- **Each backend decides how audio plays.** Either it hands the front a
  stream URL (`providesStream`) and one shared mpv instance on the front
  side plays it — the same volume/seek UX for every source — or it plays
  audio itself (`selfPlayback`), for services with no usable direct stream.
- **Capabilities are negotiated at startup.** A backend declares what it
  supports (playlists, likes, radio, downloads, its auth flow) and the
  front adapts its UI to that instead of branching on the backend's name.
- A network transport (the same JSON-RPC over a socket) is possible but not
  implemented — stdio covers every current use case.

### Backend discovery and supervision

On startup a front reads backend manifests (`manifest.json`: `id`, `name`,
`argv`, `protocolVersion`, optional `icon`) from
`~/.config/cloudmus/backends.d/*.json` — or, with `CLOUDMUS_DEV_BACKENDS=1`,
from `backends/*/manifest.json` in this checkout (run with its `.venv`).
It spawns each backend, performs the `initialize` handshake, and restarts a
crashed backend after 2 s, 5 s, then 10 s; after three failed attempts the
backend is treated as gone.

## Components

| Backend | Service | Auth flow | Playlists / likes | Radio | Download |
|---|---|---|---|---|---|
| `backends/yandex-music` | Yandex Music | `deviceCode` (OAuth) | yes | My Wave, stations | yes |
| `backends/youtube-music` | YouTube Music | `usernamePassword` (browser headers, see its [README](../backends/youtube-music/README.md)) | yes | mixes, from a track | yes |
| `backends/local-folder` | A local music folder (subfolders → playlists) | none | playlists | — | — |

`backends/py-rpc-common` is the shared Python NDJSON/JSON-RPC transport and
the conformance suite.

| Front | Description | Stack |
|---|---|---|
| `fronts/qt` | Desktop app, shipped as the AppImage | C++20, Qt 6, libmpv, CMake — see [`fronts/qt/AGENTS.md`](../fronts/qt/AGENTS.md) |
| `fronts/tui` | Terminal player | Python 3.11+, Textual, python-mpv |

## Building from source

Requirements:

- Python ≥ 3.11
- `libmpv` (the Qt front also needs its development package and `pkg-config`)
- `jinja2`, `pyyaml` (protocol code generation)
- Qt front: Qt 6 (Core, Widgets, Network, DBus, Svg), CMake ≥ 3.21, a C++20
  compiler; `clang-format` to format generated code (optional)

```bash
python3 -m venv .venv && source .venv/bin/activate

# Typed protocol stubs (gitignored; regenerate after any change under protocol/)
pip install jinja2 pyyaml
python protocol/codegen/generate.py --lang python cpp

# Shared transport, all backends, and the TUI front
pip install -e backends/py-rpc-common -e backends/yandex-music \
    -e backends/local-folder -e backends/youtube-music -e fronts/tui
```

Run the TUI:

```bash
CLOUDMUS_DEV_BACKENDS=1 cloudmus      # or: python -m cloudmus_tui
```

Build and run the Qt front (CMake regenerates the C++ stubs itself):

```bash
cmake -S fronts/qt -B fronts/qt/build
cmake --build fronts/qt/build
CLOUDMUS_DEV_BACKENDS=1 ./fronts/qt/build/bin/cloudmus-qt
```

The build is faster with `ccache` and `mold` (or `lld`) installed: CMake uses
them when it finds them, with no flags. The Qt and standard headers are
precompiled either way. Each can be switched off with
`-DCLOUDMUS_CCACHE=OFF`, `-DCLOUDMUS_FAST_LINKER=OFF` or `-DCLOUDMUS_PCH=OFF`.
ccache keys on the build directory's path, so a clean rebuild in the same
directory is almost free, while a new directory starts cold (set
`CCACHE_BASEDIR` to the checkout's parent to share between directories).
`./build-appimage.sh` keeps its cache in `.ccache/` (gitignored) and CI
restores it between runs; the Windows build runs under Wine, where ccache
can't be used, and gets only the precompiled headers.

To try the updater, run with `--update-from=0.0.1`: it then checks (at startup
too) as if that version were running, and offers the latest GitHub release.
Installing works only from an AppImage (Linux) or an installed copy
(Windows); from a build directory the dialog offers the release page instead.
To test the Linux install itself, copy an old AppImage somewhere writable and
run it with that flag: `CloudMus-0.2.0-x86_64.AppImage --update-from=0.0.1`.

The GitHub star prompt normally shows once, on the third day the player is
used. To try it, run with `--star-prompt`: it opens a couple of seconds after
the window, and nothing is saved, so it comes back on every such run.

Build the AppImage (needs only Docker; the Debian 11 build container in
`packaging/appimage/` sets the glibc 2.31 floor):

```bash
./build-appimage.sh   # -> dist/CloudMus-<version>-x86_64.AppImage
```

The AppImage's entry point (`AppRun`) is a static Rust program,
`packaging/appimage/apprun/`, built by that script in an
`rust:alpine` container (musl, so nothing it needs can be missing on the
host). Its tests, which run it against a fake AppDir and a local stand-in for
Sentry:

```bash
docker run --rm --user "$(id -u):$(id -g)" -e CARGO_HOME=/workspace/build-apprun/cargo-home \
    -e CARGO_TARGET_DIR=/workspace/build-apprun/target -v "$PWD":/workspace:z \
    -w /workspace/packaging/appimage/apprun rust:1.98.0-alpine cargo test --locked
```
(or plain `cargo test` in that directory with a local Rust).

Build the Windows x64 NSIS installer from Linux with Docker:

```bash
./build-windows.sh    # -> dist/CloudMus-<version>-x86_64-Setup.exe
```

The Windows builder in `packaging/windows/` installs the official Qt MinGW
toolchain and runs Windows CMake under Wine. Its first run downloads several
large archives; subsequent runs reuse Docker layers. It also bundles a
Windows Python runtime and the three backends. `build-windows/` and
`dist/windows-stage/` are generated and can be deleted. A release tag supplies
the installer and About-dialog version. `CLOUDMUS_GA4_MEASUREMENT_ID` is
forwarded to either Docker build when set.

Quick Windows compile checks, running the build under Wine and what that
can't tell: [`docs/windows.md`](windows.md).

### macOS

No Homebrew or MacPorts: whatever goes into the app bundle has to target an
older macOS than the build machine's, so everything comes from pinned
sources, the same here and in CI. Needs Xcode's Command Line Tools and the
repo's `.venv` (above):

```bash
pip install cmake ninja meson pkgconf aqtinstall
aqt install-qt mac desktop 6.9.3 clang_64 -O ~/Qt     # the Qt the other builds use
packaging/macos/build-deps.sh                         # libmpv, once (~20 min)
PKG_CONFIG_PATH=$PWD/packaging/macos/deps/$(uname -m)/lib/pkgconfig \
    cmake -S fronts/qt -B fronts/qt/build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/Qt/6.9.3/macos
cmake --build fronts/qt/build
CLOUDMUS_DEV_BACKENDS=1 ./fronts/qt/build/bin/CloudMus.app/Contents/MacOS/CloudMus
```

The app is built as `CloudMus.app` (signed ad hoc): macOS takes the name
the Dock and the menu bar show, and Now Playing and notifications the
app's identity, from the bundle.

`build-deps.sh` builds libmpv 0.41 (LGPL, Core Audio only) and its
dependencies from tarballs checked against their sha256, for
`MACOSX_DEPLOYMENT_TARGET` 12.0, into `packaging/macos/deps/<arch>/`. Only
`libmpv.2.dylib` is shared; the rest is linked into it. A rerun skips what's
built. To change a version, edit the script and delete that directory.
The TUI's python-mpv finds that libmpv with
`DYLD_FALLBACK_LIBRARY_PATH=$PWD/packaging/macos/deps/$(uname -m)/lib`.

Not on macOS yet (the stubs are `*Mac.cpp`): Now Playing and media keys,
notifications, launch at login, global shortcuts, self-update, and Sentry.
The Linux D-Bus integrations (MPRIS, the portal) aren't built there.
While the window is in the tray (the menu bar), the app leaves the Dock
and Cmd+Tab (`DockIconMac.mm`). A click on the menu bar icon opens its
menu, as on macOS; Settings → General can make it show the window instead.

## Tests

```bash
pytest backends/                               # all backend tests, from the repo root
(cd fronts/tui && python -m pytest tests/ -q)   # TUI
ctest --test-dir fronts/qt/build                # Qt front

# Conformance suite: drives a real backend over the wire, validates against the schemas
python -m rpc_common.testing.conformance python3 -m cloudmus_backend_yandex
python -m rpc_common.testing.conformance python3 -m cloudmus_backend_local
python -m rpc_common.testing.conformance python3 -m cloudmus_backend_ytmusic
```

The root `pyproject.toml` isn't a package: it only sets pytest's
`--import-mode=importlib`, so same-named test files in different backends
(e.g. two `test_catalog.py`) don't collide under `pytest backends/`.

### Download speed

The Qt front downloads tracks and updates in ranges over several
connections at once (`Net::ParallelDownload`): updates over 5
(`CLOUDMUS_UPDATE_CONNECTIONS` to try another number), a source's
tracks over as many as its `StreamDescriptor.maxConnections` says, unless
the user set a number on the source's settings page. To see what more
connections are worth on a line, time a real file:

```bash
cmake --build fronts/qt/build --target cloudmus-download-bench
U=https://github.com/cloudmus/cloudmus/releases/download/0.6.3/CloudMus-0.6.3-x86_64.AppImage
fronts/qt/build/bin/cloudmus-download-bench $U 1 --direct --seconds 30
fronts/qt/build/bin/cloudmus-download-bench $U 5 --direct --seconds 30
```

Without `--direct` it goes through the proxy the app would use
(`HTTPS_PROXY` & co.); `--verbose` prints each range asked for and done.
Speeds swing a lot from one run to the next; take a
few, or whole files. With `CLOUDMUS_QT_DEBUG=1` the app logs each finished
download's speed under `cloudmus.net.download`.

## Protocol

- [`docs/protocol.md`](protocol.md) — the normative spec (wins over
  `protocol/` if they disagree); [`docs/protocol-changelog.md`](protocol-changelog.md)
  — version history.
- `protocol/schema/*.yaml` — types (Track, Playlist, PlaybackState, …);
  `protocol/methods.yaml` — methods, notifications, error codes;
  `protocol/codegen/` — the Python/C++ stub generator. See
  [`protocol/README.md`](../protocol/README.md).

## Adding a new backend

1. Create `backends/<service-name>/`.
2. Write a `manifest.json` (`id`, `name`, `argv`, `protocolVersion`, and
   optionally `icon`: a monochrome 24×24 SVG, path relative to the manifest
   — the Qt front tints it to its theme). Installed fronts, the AppImage
   included, pick up manifests from `~/.config/cloudmus/backends.d/`, so a
   backend of your own works with a release build too.
3. Implement the protocol methods (`initialize`, `catalog.*`, `playback.*`,
   …) on top of `backends/py-rpc-common`.
4. Make it pass the conformance suite.
5. Translate what it sends to the front (docs/protocol.md §7.8). Each
   backend has its own dictionaries, `<package>/locales/<lang>.py`
   (`STRINGS = {english text: translation}`, for ru, fr, es, de, it, be),
   collected into a `Translator` (`<package>/i18n.py`) that is passed to
   `BackendServer`. Wrap user-visible text in `tr("English text")`; settings
   labels, groups and option labels are looked up in the dictionary by the
   settings store. The dictionary must also hold the strings `rpc_common`
   produces itself: the settings validation messages (`expected a whole
   number`, `must be at most {n}`, …), `unknown setting`, and `Download
   cancelled` — copy them from an existing backend. A missing entry shows
   English.

## Translations

The languages are ru, fr, es, de, it and be, besides English (the source
language). The language follows the system or the Language setting, and
reaches the backends too (docs/protocol.md §7.8).

- **Qt front**: texts are `tr("…")` in the code; the catalogs are
  `fronts/qt/translations/cloudmus_<lang>.ts`, compiled into the app by the
  build. After adding or changing a text, run
  `cmake --build fronts/qt/build --target update_translations` and translate
  the new entries (`linguist`, or edit the `.ts`). Text built once in a
  constructor is fine: a language change replaces the main window; but never
  cache a translated string in a `static`.
- **Backends**: each has its own dictionaries, see *Adding a new backend*.

## Environment variables

| Variable | Effect |
|---|---|
| `CLOUDMUS_DEV_BACKENDS=1` | Discover backends from this checkout |
| `CLOUDMUS_QT_DEBUG=1` | Qt front debug log (`~/.local/state/cloudmus/fronts/qt/debug.log`), same as `--debug` |
| `CLOUDMUS_UPDATE_CONNECTIONS=N` | Qt front: download an update over N connections at once (1–6) instead of 5 |
| `CLOUDMUS_HOTKEYS_BACKEND=kglobalaccel\|portal\|x11\|none` | Qt front: use one global-hotkeys mechanism whatever the desktop (to try the portal on KDE, say) |
| `CLOUDMUS_TUI_DEBUG=1` | TUI debug log, `./cloudmus-tui-debug.log` |
| `CLOUDMUS_LOCAL_FOLDER_MUSIC_DIR` | Overrides the local-folder backend's music folder |

## Crash reports

When the Qt front crashes, it writes `crash-<unix time>.txt` to
`$XDG_STATE_HOME/cloudmus/fronts/qt/crashes/` (`~/.local/state/…` by default;
`%LOCALAPPDATA%\cloudmus\fronts\qt\crashes\` on Windows). Logs and crash
reports are state, not configuration, so they are not under `~/.config`;
earlier versions kept them there, and the first start moves them
(`Config::migrateLegacyState`). The report holds
the reason, the stack and the last 40 log lines, whether or not debug
logging is on. On Windows a minidump (`.dmp`) is written next to it. The
next start logs the report's path and renames it to `*.reported.txt`, and
only the newest ten are kept.

This local report is written whether or not Sentry is on. A build made with
`CLOUDMUS_SENTRY_DSN` also sends the crash to Sentry (sentry-native, inproc
backend: the event is stored in `crashes/sentry/` and sent at the next
start); the setting and what is sent are in `docs/analytics.md`. Set the
variable when configuring CMake to try it; the tests that need it
(`aCrashWithSentryOnStillLeavesTheLocalReport`, ...) skip in a build without.

In the AppImage, `AppRun` also covers what the app can't: it runs
`cloudmus-qt` as a child and, when that dies before it has installed its crash
reporter (the dynamic linker missing a library, Qt's platform plugin, a
library's constructor), sends one event right away: exit code or signal,
the host's OS, glibc and session, the libraries the linker can't find, and the
start of stdout and stderr as attachments. The app tells it how far it got by
removing the file named in `CLOUDMUS_STARTUP_MARKER`. After an ordinary crash
`AppRun` uploads the event sentry-native stored, so it doesn't wait for the
next start. Both respect the same setting and need a build with
`CLOUDMUS_SENTRY_DSN` (`AppDir/usr/share/cloudmus/sentry-dsn`). To try the
first: `QT_QPA_PLATFORM=nonexistent ./CloudMus-….AppImage`.

To try crash reporting on a release build (an AppImage, an installed
Windows build), press **Ctrl+Shift+Alt+F12 five times** within three
seconds in any window of the app: it crashes on purpose with a real
segmentation fault. Start the app again and the event goes to Sentry; the
local report is in `crashes/`. A desktop that takes the combination for
itself never delivers it to the app.

Stack frames read `module+offset`. To resolve one of the app's own frames,
pass `addr2line` the offset plus the module's image base, from the build
that shipped (the Windows exe is not stripped):

```bash
base=$(x86_64-w64-mingw32-objdump -p build-windows/bin/cloudmus-qt.exe | awk '/^ImageBase/ {print $2}')
x86_64-w64-mingw32-addr2line -f -C -e build-windows/bin/cloudmus-qt.exe $(printf '0x%x' $((0x$base + 0x1a2b3c)))
```

## Branches and git flow

The repository follows [git flow](https://nvie.com/posts/a-successful-git-branching-model/):

| Branch | What |
|---|---|
| `master` | Released versions only: each of its commits is a release, tagged `x.y.z`. Never commit to it directly |
| `develop` | Where work comes together; the default branch on GitHub. Every push to it gets test builds (`.github/workflows/develop.yml`) |
| `feature/<name>` | A new feature, from `develop` |
| `bugfix/<name>` | A fix for something not released yet, from `develop` |
| `release/<x.y.z>` | Preparing a release (changelog), from `develop` |
| `hotfix/<x.y.z>` | An urgent fix to the released version, from `master` |

Tags have no prefix (`0.7.0`, not `v0.7.0`): pushing one publishes a release
(`docs/releasing.md`), and the app's version comes from `git describe`.

The [`git flow`](https://github.com/nvie/gitflow) tool is optional — each
step below is a couple of plain git commands. To set it up after cloning
(the defaults are what this repository uses):

```bash
git flow init -d
```

### A feature

```bash
git flow feature start my-feature      # = git checkout -b feature/my-feature develop
# ... commits ...
```

Then either open a pull request into `develop` (it gets test builds of its
own, see `docs/releasing.md`):

```bash
git flow feature publish my-feature    # = git push -u origin feature/my-feature
```

or merge it yourself:

```bash
git rebase develop                     # catch up first, keeps history readable
git flow feature finish my-feature     # = git checkout develop && git merge --no-ff feature/my-feature
git push origin develop
```

A small, self-contained change can go straight to `develop` as a commit.

### A bug fix for the next release

The same as a feature, on a `bugfix/<name>` branch. `git flow bugfix` exists
only in the git-flow-avh fork; with the original tool use plain git
(`git checkout -b bugfix/<name> develop`, then a pull request or
`git merge --no-ff` into `develop`). A bug that's already in a released
version and can't wait for the next release is a hotfix instead.

### A release

On an up-to-date `develop`: update and tidy up the changelog, move it to a
`release/<x.y.z>` branch as the `Release <x.y.z>` commit, tag that commit,
`git flow release finish -n <x.y.z>` (merges into `master` and `develop`),
and push `master`, `develop` and the tag. The steps and the changelog rules:
`docs/releasing.md`, "Making a release".

### A hotfix

An urgent fix to the released version, made on top of `master` without
whatever is on `develop`. Its version is the next patch: `0.6.3` → `0.6.4`.

```bash
git fetch --tags --force origin
git checkout master && git pull
git flow hotfix start 0.6.4            # = git checkout -b hotfix/0.6.4 master
# ... fix commits (a Sentry crash fix names it: AGENTS.md, "Commits") ...
```

Then release it from the hotfix branch: changelog, `Release 0.6.4` commit,
annotated tag on it, and:

```bash
git flow hotfix finish -n 0.6.4        # merges into master and develop
git push origin master develop 0.6.4
```

The steps: `docs/releasing.md`, "Making a hotfix". If `develop` has moved
on, the merge into it may conflict — resolve it there; the fix must not get
lost from the next release.

`finish` runs with `-n` (no tag) and we tag ourselves, for two reasons: the
tag message (the release notes) comes from a file, which `git flow` can't
take, and a tag on the branch's own `Release` commit is reachable from both
`master` and `develop`, so `git describe` on `develop` counts from the new
version.

## Repository layout

```
protocol/            Protocol schemas (schema/, methods.yaml) + codegen/
backends/            py-rpc-common/ and one folder per service
fronts/              tui/ (Python, Textual), qt/ (C++20, Qt 6)
packaging/appimage/  AppImage build (Dockerfile, build script, apprun/: the Rust AppRun)
packaging/windows/   Windows NSIS build (Dockerfile, build script, installer)
.github/workflows/   Release, develop and pull request builds (build.yml is shared)
docs/                Protocol spec, this guide, Windows, releasing, analytics
art/                 Logo and screenshots
```

Releases and the changelog: [`docs/releasing.md`](releasing.md).
