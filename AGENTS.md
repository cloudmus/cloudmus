# Agent instructions — cloudmus

Music player: **fronts** (UI) and **backends** (one process per music
service) talking JSON-RPC 2.0 over stdin/stdout, framed as NDJSON.
Details: `docs/development.md`. Before touching `fronts/qt/`, read
`fronts/qt/AGENTS.md`.

## Layout

| Path | What | Stack |
|---|---|---|
| `protocol/` | Schema-first protocol: `schema/*.yaml`, `methods.yaml`, `codegen/` | YAML, Python + Jinja2 |
| `docs/protocol.md` | Normative protocol spec (wins over `protocol/`) | |
| `backends/py-rpc-common/` | Shared NDJSON/JSON-RPC transport + conformance suite | Python ≥ 3.11 |
| `backends/<service>/` | One backend per service, each with `manifest.json` | Python ≥ 3.11 |
| `fronts/tui/` | Terminal front | Python, Textual, python-mpv |
| `fronts/qt/` | Desktop front | C++20, Qt 6, libmpv, CMake |
| `packaging/appimage/`, `build-appimage.sh` | AppImage build in Docker | |
| `packaging/appimage/apprun/` | The AppImage's `AppRun`: static launcher that reports failed starts to Sentry | Rust (built in `rust:alpine`) |

## Generated code

Protocol stubs are gitignored build artifacts — never edit or commit
`backends/py-rpc-common/rpc_common/generated/` or `fronts/qt/generated/`.
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
4. Update the users, run the tests, and build `fronts/qt`.

## Build and test

```bash
source .venv/bin/activate      # setup: docs/development.md
pytest backends/                                  # from the repo root
(cd fronts/tui && python -m pytest tests/ -q)
python -m rpc_common.testing.conformance python3 -m cloudmus_backend_local  # per backend
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
- New backend: `backends/<service>/` with `manifest.json`, built on
  `py-rpc-common`, passing the conformance suite.
- Git flow (`docs/development.md`, "Branches and git flow"): work goes to
  `develop` or a `feature/*`/`bugfix/*` branch off it, never straight to
  `master`; `master` only gets release and hotfix merges.
- Versions come from `x.y.z` git tags; pushing one publishes a release.
  Don't create or push tags unless asked.

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
