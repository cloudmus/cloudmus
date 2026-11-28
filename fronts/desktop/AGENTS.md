# Agent instructions — desktop front (`fronts/desktop/`)

On top of the root `AGENTS.md`.

## Build, run, test

```bash
cmake -S fronts/desktop -B fronts/desktop/build && cmake --build fronts/desktop/build
CLOUDMUS_DEV_BACKENDS=1 ./fronts/desktop/build/bin/cloudmus-qt
ctest --test-dir fronts/desktop/build
```

On macOS the app is `build/bin/CloudMus.app` (run
`CloudMus.app/Contents/MacOS/CloudMus`).

- CMake regenerates the C++ stubs (`core/generated/`) itself.
- Tests (QtTest): `cloudmus-core-tests` (the core's own, in `core/tests/`),
  `cloudmus-qt-tests` (the desktop-only core) and `cloudmus-qt-ui-tests`
  (window, offscreen). The test binaries act as their own fake backend
  (`core/tests/FakeBackend.h`) — no Python needed.
- `CLOUDMUS_QT_DEBUG=1` enables the debug log; settings live in
  `~/.config/cloudmus/fronts/qt/config.ini`.
- Three platforms: Linux, Windows, macOS (setup: `docs/development.md`,
  "macOS"). "Not Windows" isn't "Linux": D-Bus, X11 and Wayland code goes
  under `Q_OS_LINUX`. macOS stubs and integrations live in `*Mac.cpp`.

## Architecture: core library and MVVM

Everything below the view (backends, playback, settings, state, and the
view models) builds into the `cloudmus-core` static library, in `core/`
outside any front (see `core/AGENTS.md`), which does **not** link Qt
Widgets — no widget headers there. `App::Core` (`core/src/App/`) owns services and view models
for the whole run; widgets (`cloudmus-ui`) only show state and forward
actions, and `Ui::WindowHost` can replace the main window at any time. New
logic goes into core (a service or view model) with a test, not into
`MainWindow`.

The mobile front (`fronts/mobile`) builds `cloudmus-core` too, on Qt 6.7: keep
it to 6.7 APIs. What only the desktop needs (hotkeys, updates, crash reports)
goes into `cloudmus-desktop-core`, and `App::DesktopCore` owns it.

## Timing

To coalesce or rate-limit events use `Timing::Debounce` (run once things
have gone quiet: typing, a drag, a resize) or `Timing::Throttle` (at most
once per interval, the last trigger never lost: progress updates), both in
`core/src/Timing/` — not a hand-rolled single-shot `QTimer` with a "pending"
flag. A plain `QTimer` stays for timeouts and delays.

## Code style

- Classes, structs, namespaces: `PascalCase`; methods, fields: `camelCase`.
  Generated structs use wire names as-is; a C++ keyword gets a trailing
  underscore (`explicit_`, see `protocol/codegen/cpprender.py`).
- A folder under `src/` (and `core/src/`) is named exactly after the namespace it implements
  (`namespace Rpc` → `core/src/Rpc/`). Small helper sub-namespaces (`Rpc::Detail`)
  need no folder; `generated/` is exempt.
- Format before committing (config: `fronts/desktop/_clang-format`):
  `clang-format -i fronts/desktop/src/**/*.{h,cpp}` (the core: `core/AGENTS.md`)
- Don't hardcode sizes (pixel widths/heights, opening sizes, minimums)
  without need — use Qt's own mechanisms: layouts, size policies, stretch
  factors, layout size constraints (`QLayout::setSizeConstraint`), and
  `sizeHint()`/`adjustSize()` for the opening size. To stop a window growing
  one way, give its widgets a `Maximum`/`Fixed` size policy in that
  direction and let the layout's constraint carry it to the window. Not
  `setFixedSize`/`setFixedHeight`/`setMinimumSize`, a `resize()` to a
  constant, or a hand-computed `sizeHint()` — only where truly unavoidable
  (a square icon button, a progress bar's thickness), with a comment saying
  why.
