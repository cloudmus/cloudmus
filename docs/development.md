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

Build the AppImage (needs only Docker; the Debian 11 build container in
`packaging/appimage/` sets the glibc 2.31 floor):

```bash
./build-appimage.sh   # -> dist/CloudMus-x86_64.AppImage
```

Build the Windows x64 NSIS installer from Linux with Docker:

```bash
./build-windows.sh    # -> dist/CloudMus-x86_64-Setup.exe
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

## Environment variables

| Variable | Effect |
|---|---|
| `CLOUDMUS_DEV_BACKENDS=1` | Discover backends from this checkout |
| `CLOUDMUS_QT_DEBUG=1` | Qt front debug log (`~/.config/cloudmus/fronts/qt/debug.log`), same as `--debug` |
| `CLOUDMUS_TUI_DEBUG=1` | TUI debug log, `./cloudmus-tui-debug.log` |
| `CLOUDMUS_LOCAL_FOLDER_MUSIC_DIR` | Overrides the local-folder backend's music folder |

## Crash reports

When the Qt front crashes, it writes `crash-<unix time>.txt` to
`~/.config/cloudmus/fronts/qt/crashes/`
(`%LOCALAPPDATA%\cloudmus\fronts\qt\crashes\` on Windows). The report holds
the reason, the stack and the last 40 log lines, whether or not debug
logging is on. On Windows a minidump (`.dmp`) is written next to it. The
next start logs the report's path and renames it to `*.reported.txt`, and
only the newest ten are kept.

Stack frames read `module+offset`. To resolve one of the app's own frames,
pass `addr2line` the offset plus the module's image base, from the build
that shipped (the Windows exe is not stripped):

```bash
base=$(x86_64-w64-mingw32-objdump -p build-windows/bin/cloudmus-qt.exe | awk '/^ImageBase/ {print $2}')
x86_64-w64-mingw32-addr2line -f -C -e build-windows/bin/cloudmus-qt.exe $(printf '0x%x' $((0x$base + 0x1a2b3c)))
```

## Repository layout

```
protocol/            Protocol schemas (schema/, methods.yaml) + codegen/
backends/            py-rpc-common/ and one folder per service
fronts/              tui/ (Python, Textual), qt/ (C++20, Qt 6)
packaging/appimage/  AppImage build (Dockerfile, AppRun, build script)
packaging/windows/   Windows NSIS build (Dockerfile, build script, installer)
.github/workflows/   Release build
docs/                Protocol spec, this guide, Windows, releasing, analytics
art/                 Logo and screenshots
```

Releases and the changelog: [`docs/releasing.md`](releasing.md).
