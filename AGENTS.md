# Agent instructions — cloudmus

CloudMus is a music player split into **fronts** (UI) and **backends** (one
process per music service) talking JSON-RPC 2.0 over stdin/stdout, framed
as NDJSON. `README.md` has the overview; this file holds the rules for
the whole repo. A subdirectory's own `AGENTS.md` adds rules specific to
that part on top of these — currently `fronts/qt/AGENTS.md` for the C++ Qt
front; read it before touching `fronts/qt/`.

## Layout

| Path | What | Stack |
|---|---|---|
| `protocol/` | Schema-first protocol: `schema/*.yaml`, `methods.yaml`, `codegen/` | YAML, Python + Jinja2 |
| `docs/protocol.md` | Normative protocol prose (wins over `protocol/` if they disagree) | |
| `backends/py-rpc-common/` | Shared NDJSON/JSON-RPC transport + conformance suite | Python ≥ 3.11 |
| `backends/<service>/` | One backend per service, each with `manifest.json` | Python ≥ 3.11 |
| `fronts/tui/` | Terminal front | Python, Textual, python-mpv |
| `fronts/qt/` | Desktop front | C++20, Qt 6, libmpv, CMake |
| `packaging/appimage/`, `build-appimage.sh` | AppImage build in Docker (Debian 11 base) | |
| `.github/workflows/release.yml` | Builds the AppImage on an `x.y.z` tag and attaches it to the GitHub release | |

## Generated code

Protocol stubs are **gitignored build artifacts** — never edit or commit
them:

- `backends/py-rpc-common/rpc_common/generated/`
- `fronts/qt/generated/`

Regenerate after a fresh checkout or any change under `protocol/`:

```bash
python protocol/codegen/generate.py --lang python cpp
```

Wire-format names in `protocol/` are `camelCase`; the generators map them
to each language's own conventions, so name new fields/methods for the
wire, not for any one language.

## Changing the protocol

The front and the backends only share the protocol, so a feature that
needs new data goes through it — never by a front special-casing a
particular backend (`if backend == "yandex"`). Backends declare what they
support via capabilities; fronts adapt to them.

1. Edit `protocol/methods.yaml` and/or `protocol/schema/*.yaml`
   (see `protocol/README.md` for the supported constructs, e.g. `x-name`).
2. Regenerate both languages.
3. Update `docs/protocol.md` and add a `docs/protocol-changelog.md` entry —
   MINOR for additive/optional changes, MAJOR for changed or removed
   meaning (`docs/protocol.md` §12). Prefer additive, optional fields.
4. Update the backends/fronts that use it and run the tests below
   (including a build of `fronts/qt`, which consumes the C++ stubs).

## Build and test

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install jinja2 pyyaml
python protocol/codegen/generate.py --lang python cpp
pip install -e backends/py-rpc-common -e backends/yandex-music \
    -e backends/local-folder -e backends/youtube-music -e fronts/tui

pytest backends/                                  # all backend tests (run from repo root)
(cd fronts/tui && python -m pytest tests/ -q)      # TUI tests
python -m rpc_common.testing.conformance python3 -m cloudmus_backend_local  # per backend
```

- Run `pytest backends/` from the repo root: the root `pyproject.toml`
  only exists to set `--import-mode=importlib` so same-named test files in
  different backends don't collide.
- `CLOUDMUS_DEV_BACKENDS=1` makes the fronts discover backends from this
  checkout instead of `~/.config/cloudmus/backends.d/`.
- `CLOUDMUS_TUI_DEBUG=1` enables the TUI's debug log.
- Building and running the Qt front: see `fronts/qt/AGENTS.md`.
- The AppImage build (`./build-appimage.sh`) needs Docker and is slow —
  run it only when touching packaging.

## Conventions

- Code, comments, docs, and commit messages are in English.
- Match the surrounding code. Comments explain *why* (the constraint, the
  bug it avoids), not what the next line does — see existing code for the
  expected tone and density.
- Adding a backend: `backends/<service>/` with `manifest.json` (`id`,
  `name`, `argv`, `protocolVersion`, optional monochrome 24×24 `icon.svg`),
  built on `py-rpc-common`, passing the conformance suite.
- Versions come from git tags (`x.y.z`, via `git describe` in
  `fronts/qt/cmake/Version.cmake`); pushing such a tag publishes a release.
  Don't create or push tags unless asked.

## Commits

- Subject: short, imperative, capitalized, no trailing period; an
  `Area: ` prefix when it helps (`Protocol + backends: …`, `Tray: …`,
  `Yandex: …`).
- Body: a few wrapped lines on what changed from the user's point of view
  and why.
- One logical change per commit; don't commit generated files, build
  directories, or `.venv/`.

## Changelog

`CHANGELOG.md` is seeded from git by `./update-changelog-from-git.sh`: it
inserts a `# X.X.X (<date>)` heading on top with one `- ` bullet per commit
subject since the last version in the file. `X.X.X` is replaced with the
real version (the `x.y.z` release tag) when releasing.

### "Tidy up CHANGELOG"

When asked to tidy up `CHANGELOG.md`, edit only the section for commits
since the last tagged release (`git tag -l`) — typically the topmost,
still-unreleased `# X.X.X` heading. For each entry in that section:

- Drop entries of no interest to CloudMus users (people using the player)
  or backend/front authors (people relying on the protocol): routine
  `README`/`AGENTS.md` touch-ups, internal test/CI/build-tooling churn,
  typo fixes, reverted or rolled-back intermediate steps, and other commits
  that don't change user-visible behavior, a supported service's features,
  or the protocol.
- Refactors and source-tree reorganizations (moving files, renaming
  classes, replacing QSS with a custom `QStyle`, splitting modules) are not
  interesting on their own — drop them, unless they visibly change the
  app's look or behavior; then describe that change instead.
- Rephrase entries that describe an implementation step (a class name like
  `ThemedSlider`/`HeroPanel`, a file path, a `cloudmus-qt:` prefix) into a
  plain description of the resulting user-visible capability.
- Consolidate a run of incremental commits that build up one feature (e.g.
  several commits landing Yandex "My Wave") into a single bullet describing
  the feature's end state, rather than listing every step.
- Multiple commits touching the same component (the tray, notifications,
  menus, a specific backend such as Yandex Music) collapse into one bullet
  naming that component once, not one bullet per commit.
- Protocol changes stay, briefly, with the protocol version they bring
  (e.g. "Protocol 1.3: playlist editing, `feedback.unlike`/`undislike`") —
  details belong in `docs/protocol-changelog.md`, not here.
- Keep bug fixes, new features, new backends/services, new
  packaging/install/release options (e.g. the AppImage) — these are what
  users read the changelog for. But still aim to leave only what matters
  most: fold minor/low-signal entries (small visual polish) into a closely
  related bullet instead of listing them on their own.

Preserve the existing formatting: one version heading per release
(`# <x.y.z> (<date>)`, `# X.X.X (<date>)` while unreleased), one `- ` bullet
per entry, in English. Do not touch already-released version sections.

Order the entries in three groups, in this order: new features first, then
changes to existing behavior (including docs/process changes), then bug
fixes last. Within each group, keep entries in newest-first order (their
original relative order).

## Releasing

A release is an annotated `x.y.z` tag; pushing it makes
`.github/workflows/release.yml` build the AppImage and publish the GitHub
release, with the tag's message as its release notes. When asked to make a
release:

1. Pull fresh tags from GitHub: `git fetch --tags --force origin` (a tag
   may have been moved or created there).
2. Run `./update-changelog-from-git.sh` to add the commits since the last
   release to `CHANGELOG.md` under a new `# X.X.X (<date>)` heading.
3. Tidy up that section (see "Tidy up CHANGELOG" above).
4. Pick the version from what the section contains: only bug fixes → a
   patch release (`0.1.0` → `0.1.1`); anything new → a minor release
   (`0.1.0` → `0.2.0`). Replace `X.X.X` in the heading with it.
5. Commit the changelog with the version as the subject:
   `Release <x.y.z>`.
6. Create an annotated tag on that commit whose message is the version
   followed by that release's changelog bullets, and push the commit and
   the tag:

   ```bash
   git tag -a <x.y.z> -F <message-file>
   git push origin HEAD <x.y.z>
   ```
