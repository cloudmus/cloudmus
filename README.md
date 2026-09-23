<p align="center">
  <img src="art/small_logo.svg" alt="CloudMus logo" width="120">
</p>

<h1 align="center">CloudMus</h1>

<p align="center">
  A multi-source music player built on a front/backend split<br>
  over JSON-RPC 2.0
</p>

---

CloudMus is a music player where the UI and the music services it plays from
are separated by a clean protocol boundary. Each music service (Yandex
Music, YouTube Music, a local folder, and eventually Spotify, internet
radio, and others) runs as its own **backend** process, talking to a UI
**front** over JSON-RPC 2.0 via stdin/stdout. Adding a new service means
writing a small adapter — the UI code never has to change.

### What makes this different from a regular player

Most players hardcode support for specific services directly into the UI.
In CloudMus, the only thing that's stable is the **protocol** — JSON-RPC 2.0
over stdin/stdout, framed as NDJSON (one JSON object per line). Everything
else is an implementation detail of the process on either side:

- **The front and the backend don't have to share a language.** Today
  there's Python (the TUI, every backend) and C++/Qt (the desktop app);
  nothing stops a front written in Rust or a backend written in Go — see
  `protocol/README.md`.
- **Each backend decides how audio actually plays.** Either it hands the
  front a stream URL (`providesStream`), in which case one shared mpv
  instance on the front side does the playing — giving every source the
  same volume/seek UX — or it plays audio itself (`selfPlayback`), for
  services with no usable direct stream (DRM, Chromecast-like targets).
- **Capabilities are negotiated at startup.** A backend declares what it
  supports (playlists, likes, radio, downloads, the auth flow it needs),
  and the front adapts its UI to what's actually available instead of
  branching on `if backend == "yandex": ...` in interface code.
- **A network transport is possible in theory** (the same JSON-RPC over a
  socket instead of stdio), but it isn't implemented and isn't needed yet —
  stdio comfortably covers every current use case.

## Architecture

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

On startup, the front scans backend manifests, spawns each backend as a
child process, performs the handshake, and then talks to it over the
protocol: catalog browsing, playback, radio, likes, downloads.

## Backends

| Backend | Description | Auth | Playlists / likes | Radio | Download |
|---|---|---|---|---|---|
| `backends/yandex-music` | [Yandex Music](https://music.yandex.ru) | deviceCode (OAuth) | yes | yes (My Wave) | yes |
| `backends/youtube-music` | [YouTube Music](https://music.youtube.com) | browser headers* | yes | no | no |
| `backends/local-folder` | A local music folder (subfolders → playlists) | none | yes | — | — |

\* YouTube Music: device-code OAuth is currently broken on Google's side
([sigma67/ytmusicapi#676](https://github.com/sigma67/ytmusicapi/issues/676)),
so this backend uses a workaround based on pasting browser request headers.
See `backends/youtube-music/README.md`.

The list of sources isn't fixed — a new backend (Spotify, internet radio,
SoundCloud, VK, etc.) is added as a standalone adapter process without
touching any front, see [Adding a new backend](#adding-a-new-backend).

## Fronts

| Front | Description | Stack |
|---|---|---|
| `fronts/tui` | Terminal player | Python 3.11+, Textual, python-mpv |
| `fronts/qt` | Desktop application | C++20, Qt 6, libmpv, CMake |

The Qt front additionally provides: a system tray icon, MPRIS (D-Bus)
integration, global hotkeys, desktop notifications, playback history, and
cover art caching.

## Quick start

### Requirements

- Python ≥ 3.11
- `libmpv` (install via your OS package manager)
- `jinja2`, `pyyaml` (for protocol code generation)
- For the Qt front: Qt 6, CMake ≥ 3.21, clang-format

### Install

```bash
# Virtual environment
python3 -m venv .venv && source .venv/bin/activate

# Generate typed protocol stubs (Python + C++)
pip install jinja2 pyyaml
python protocol/codegen/generate.py --lang python cpp

# Install the shared transport, all backends, and the TUI front
pip install -e backends/py-rpc-common
pip install -e backends/yandex-music
pip install -e backends/local-folder
pip install -e backends/youtube-music
pip install -e fronts/tui
```

### Run the TUI

```bash
cloudmus
# or:
python -m cloudmus_tui
```

### Run the Qt front

```bash
cmake -S fronts/qt -B fronts/qt/build
cmake --build fronts/qt/build
./fronts/qt/build/bin/cloudmus-qt
```

CMake automatically regenerates the C++ protocol stubs at configure time.

### Development mode

```bash
export CLOUDMUS_DEV_BACKENDS=1  # front discovers backends from the repo checkout
```

## Tests

```bash
# All backend tests, from the repo root
pytest backends/

# TUI tests
cd fronts/tui && python -m pytest tests/ -q

# Conformance suite (validates against the JSON schemas)
python -m rpc_common.testing.conformance python3 -m cloudmus_backend_yandex
python -m rpc_common.testing.conformance python3 -m cloudmus_backend_local
python -m rpc_common.testing.conformance python3 -m cloudmus_backend_ytmusic
```

The root `pyproject.toml` isn't a package — it only holds a pytest
`addopts` setting (`--import-mode=importlib`) so `pytest backends/` doesn't
collide when two backends' `tests/` directories both have a file with the
same name (e.g. two `test_catalog.py`, neither package-marked with
`__init__.py`). It's required for that command to work from the repo root.

## Protocol

The protocol (v1.2) is described in `docs/protocol.md`. Data schemas and the
RPC surface are defined as YAML under `protocol/`:

- `protocol/schema/*.yaml` — types (Track, Playlist, PlaybackState, ...)
- `protocol/methods.yaml` — methods, notifications, error codes
- `protocol/codegen/` — the Python/C++ stub generator (Jinja2)

Generated code is a build artifact (gitignored) and is regenerated before
building/testing. See `protocol/README.md` for details.

### Key protocol features

- **NDJSON framing** — one JSON value per line, UTF-8
- **Capability negotiation** — a backend declares its capabilities at handshake
- **providesStream** — a backend hands back a stream URL; the front plays it via mpv
- **Radio / My Wave** — a backend pushes new tracks; the front reports listening feedback
- **Download** — a backend saves an mp3 into a folder the front specifies

## Configuration

| Path | Description |
|---|---|
| `~/.config/cloudmus/backends.d/*.json` | Backend manifests (production) |
| `~/.config/cloudmus/backends/<id>/` | Per-backend data (tokens, settings) |
| `~/.config/cloudmus/fronts/qt/config.ini` | Qt front settings |

### Environment variables

| Variable | Description |
|---|---|
| `CLOUDMUS_DEV_BACKENDS=1` | Dev mode: discover backends from the repo checkout |
| `CLOUDMUS_TUI_DEBUG=1` | Debug log for the TUI, written to `./cloudmus-tui-debug.log` |
| `CLOUDMUS_QT_DEBUG=1` | Debug log for the Qt front |

## Adding a new backend

1. Create a folder under `backends/<service-name>/`
2. Write a `manifest.json` (id, name, argv, protocolVersion, and optionally
   `icon`: a monochrome 24×24 SVG, path relative to the manifest — the Qt
   front tints it to match its theme)
3. Implement the protocol methods: `initialize`, `catalog.*`, `playback.*`, ...
4. Use `backends/py-rpc-common` as the shared transport
5. Validate it against the conformance suite

## Repository layout

```
cloudmus/
├── protocol/          # Protocol schemas + codegen (Python, C++)
│   ├── schema/        # JSON Schema draft-07 types
│   ├── methods.yaml   # RPC surface
│   └── codegen/       # Stub generator
├── backends/
│   ├── py-rpc-common/ # Shared NDJSON/JSON-RPC transport (Python)
│   ├── yandex-music/  # Yandex Music backend
│   ├── local-folder/  # Local folder backend
│   └── youtube-music/ # YouTube Music backend
├── fronts/
│   ├── tui/           # TUI front (Python, Textual)
│   └── qt/            # Desktop front (C++20, Qt 6)
├── docs/               # Architecture and protocol prose
└── art/                # Logo
```

## License

MIT — see [LICENSE](LICENSE).
