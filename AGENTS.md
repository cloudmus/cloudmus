# Agent instructions — CloudMus

Music player: **fronts** (UI) and **backends** (one process per music
service) talking JSON-RPC 2.0 over stdin/stdout, framed as NDJSON.
Details: `docs/development.md`. Before touching `core/`, `fronts/desktop/` or
`fronts/mobile/`, read its `AGENTS.md`.

## Questions and changes

When the user asks a question, answer it directly. Inspect the code or run
read-only checks as needed. Wait for an explicit request before changing code,
rebuilding packages, or installing an update. A question about a bug or missing
feature is a request for an explanation by default.

## Layout

| Path | What | Stack |
|---|---|---|
| `protocol/` | Schema-first protocol: `schema/*.yaml`, `methods.yaml`, `codegen/` | YAML, Python + Jinja2 |
| `docs/protocol.md` | Normative protocol spec (wins over `protocol/`) | |
| `docs/plans/` | Plans and investigations with open work; the rest of `docs/` is how things are now. New plans go here | |
| `backends/py-rpc-common/` | Shared NDJSON/JSON-RPC transport + conformance suite | Python ≥ 3.11 |
| `core/` | `cloudmus-core`: the C++ library both Qt fronts build (RPC, playback, state, view models, tests, shared resources); `core/AGENTS.md` | C++20, Qt 6.7+, libmpv, CMake |
| `backends/<service>/` | One backend per service, each with `manifest.json` | Python ≥ 3.11, Rust for local-folder |
| `fronts/tui/` | Terminal front | Python, Textual, python-mpv |
| `fronts/desktop/` | Desktop front | C++20, Qt 6, libmpv, CMake |
| `fronts/mobile/` | Mobile front MVP (Android APK + Linux debug build); `docs/android.md` | C++20, Qt 6.7, QML, Media3 |
| `packaging/appimage/`, `build-appimage.sh` | AppImage build in Docker | |
| `packaging/appimage/apprun/` | The AppImage's `AppRun`: static launcher that reports failed starts to Sentry | Rust (built in `rust:alpine`) |

## Generated code

Protocol stubs are gitignored build artifacts — never edit or commit
`backends/py-rpc-common/rpc_common/generated/` or `core/generated/`.
Regenerate after a fresh checkout or any change under `protocol/`:
`python protocol/codegen/generate.py --lang python cpp`.

## Changing the protocol

Fronts never special-case a backend (`if backend == "yandex"`): new data
goes through the protocol, backends declare support via capabilities.

1. Edit `protocol/methods.yaml` / `protocol/schema/*.yaml` (constructs:
   `protocol/README.md`). Name things for the wire (`camelCase`).
2. Regenerate both languages.
3. Update `docs/protocol.md` and add a `docs/protocol-changelog.md` entry —
   MINOR for additive/optional, MAJOR for changed/removed meaning. Prefer
   additive, optional fields.
4. Update the users, run the tests, and build `fronts/desktop`.

## Build and test

```bash
source .venv/bin/activate      # setup: docs/development.md
pytest backends/                                  # from the repo root
(cd fronts/tui && python -m pytest tests/ -q)
cargo test --locked --manifest-path backends/local-folder/Cargo.toml
cargo build --locked --manifest-path backends/local-folder/Cargo.toml
python -m rpc_common.testing.conformance backends/local-folder/target/debug/cloudmus-backend-local-folder
```

`CLOUDMUS_DEV_BACKENDS=1` makes fronts use backends from this checkout.
`./build-appimage.sh` needs Docker and is slow — only for packaging changes.
`AppRun` tests: `cargo test` in `packaging/appimage/apprun/` (command for the
pinned `rust:alpine` image: `docs/development.md`).

Windows: before building or checking anything for Windows (any `Q_OS_WIN` /
`*Win.cpp` change), read `docs/windows.md`. It covers the quick compile check
in Docker, running under Wine, and what only a real Windows can show.

## Conventions

- English everywhere. Match surrounding code; comments explain *why*.
- The app is called **CloudMus**, spelled exactly so wherever it's written as
  a name: window and dialog titles, labels, notifications, docs and their
  headings, comments, package descriptions. Never `Cloudmus` or `cloudmus`.
  Other spellings stay only in existing technical identifiers, where renaming
  would break something or lose user data: paths, package and binary names,
  D-Bus names, env vars, QML module and log tag names, `QSettings` names.
- New backend: `backends/<service>/` with `manifest.json`, built on
  `py-rpc-common`, passing the conformance suite.
- Git flow (`docs/development.md`, "Branches and git flow"): work goes to
  `develop` or a `feature/*`/`bugfix/*` branch off it, never straight to
  `master`; `master` only gets release and hotfix merges.
- Versions come from `x.y.z` git tags; pushing one publishes a release.
  Don't create or push tags unless asked.

## Copying to the clipboard

A button that copies something is always the copy button the About page has —
the copy glyph beside what it copies, a check mark for a moment once pressed —
never a "Copy …" text button or a toast of its own: `Ui::CopyButton` on the
desktop, `CopyButton.qml` on mobile.

## Commits

- Subject: short, imperative, capitalized, no trailing period; optional
  `Area: ` prefix (`Protocol + backends: …`, `Tray: …`, `Yandex: …`).
- Body: a few wrapped lines on what changed for the user and why.
- One logical change per commit; never commit generated files, build
  directories, or `.venv/`.
- A commit that fixes (or works toward fixing) a crash from Sentry names
  it, before the trailers: `Refs CLOUDMUS-QT-X` and
  `Sentry: https://navrockydev.sentry.io/issues/CLOUDMUS-QT-X/`. Find the
  issue id from the event (Sentry MCP, or the event JSON's `event_id`).
  `Refs`, never `Fixes`/`Closes`: the issue is resolved by hand once the
  fix is confirmed.

## Changelog and releases

For "tidy up CHANGELOG", "make a release" or "make a hotfix", follow
`docs/releasing.md`.
