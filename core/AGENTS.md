# Agent instructions — core library (`core/`)

On top of the root `AGENTS.md`. The `cloudmus-core` library is shared by the
desktop front (`fronts/desktop`) and the mobile one (`fronts/mobile`); neither owns
it, and neither's sources may be reached into from here.

## Layout

- `src/<Namespace>/` — a folder is named exactly after the namespace it
  implements (`namespace Rpc` → `src/Rpc/`). Backends and RPC (`Rpc`),
  playback (`Playback`), settings (`Config`), state (`App`), view models
  (`ViewModel`), and the rest of what sits below the view.
- `common/` — `cloudmus-common`: the little that needs Qt Core/Gui alone
  (`I18n`, `Covers/CoverPicture`, `Covers/MeshAura`, `About/People.h`, `Qr`),
  which the mobile front's UI process links without the rest of the core.
- `third_party/` — sources copied into the tree unchanged (`qrcodegen`); don't
  format or edit them.
- `resources/` — what both fronts embed: fonts, the icon symbols, the logo
  and cloud, the About data (`people.json`, agents, technologies), the sound
  logo. Each front lists what it needs in its own `.qrc` or CMake file.
- `tests/` — QtTest, as `cloudmus-core-tests` (built by the desktop front's
  CMake), and `cloudmus-core-test-support` (`FakeBackend`, `TestSupport.h`),
  which the fronts' tests link too. The test binary acts as its own fake
  backend — no Python needed.
- `generated/` — the C++ protocol stubs. Gitignored; CMake regenerates them
  from `protocol/` (`python protocol/codegen/generate.py --lang cpp`).

## Rules

- The core does **not** link Qt Widgets — no widget headers here. Widgets
  live in `fronts/desktop`'s `cloudmus-ui`.
- The mobile front builds the core on Qt 6.7: keep it to 6.7 APIs.
- What only one front needs goes into that front (the desktop's hotkeys,
  installing an update and crash reports: `cloudmus-desktop-core`), not here.
  Finding and downloading an update is shared: `Update::` and
  `ViewModel::UpdateScreen`; each platform installs it its own way.
- Platform differences stay in `#ifdef Q_OS_*` blocks, as in `Playback/AudioPlayer`.
- Coalescing or rate-limiting events: `Timing::Debounce` / `Timing::Throttle`
  (`src/Timing/`), not a hand-rolled single-shot `QTimer` with a "pending"
  flag. A plain `QTimer` stays for timeouts and delays.
- Format before committing (config: `core/_clang-format`):
  `clang-format -i core/src/**/*.{h,cpp}`
- New logic goes into the core (a service or view model) with a test in
  `core/tests/`, not into a front's widgets or QML.

Building and testing: through a front's CMake project (`fronts/desktop`,
`fronts/mobile`); the core is `add_subdirectory(../../core core)`. The core
expects a `cloudmus-mpv` target from it.
