# Agent instructions — Qt front (`fronts/qt/`)

On top of the root `AGENTS.md`.

## Build, run, test

```bash
cmake -S fronts/qt -B fronts/qt/build && cmake --build fronts/qt/build
CLOUDMUS_DEV_BACKENDS=1 ./fronts/qt/build/bin/cloudmus-qt
ctest --test-dir fronts/qt/build
```

- CMake regenerates the C++ stubs (`fronts/qt/generated/`) itself.
- Tests (QtTest): `cloudmus-qt-tests` (core) and `cloudmus-qt-ui-tests`
  (window, offscreen). The test binary acts as its own fake backend
  (`tests/FakeBackend.h`) — no Python needed.
- `CLOUDMUS_QT_DEBUG=1` enables the debug log; settings live in
  `~/.config/cloudmus/fronts/qt/config.ini`.

## Architecture: core library and MVVM

Everything below the view (backends, playback, settings, state, and the
view models in `src/ViewModel/`) builds into the `cloudmus-core` static
library, which does **not** link Qt Widgets — no widget headers there.
`App::Core` (`src/App/`) owns services and view models for the whole run;
widgets (`cloudmus-ui`) only show state and forward actions, and
`Ui::WindowHost` can replace the main window at any time. New logic goes
into core (a service or view model) with a test, not into `MainWindow`.

## Code style

- Classes, structs, namespaces: `PascalCase`; methods, fields: `camelCase`.
  Generated structs use wire names as-is; a C++ keyword gets a trailing
  underscore (`explicit_`, see `protocol/codegen/cpprender.py`).
- A folder under `src/` is named exactly after the namespace it implements
  (`namespace Rpc` → `src/Rpc/`). Small helper sub-namespaces (`Rpc::Detail`)
  need no folder; `generated/` is exempt.
- Format before committing (config: `fronts/qt/_clang-format`):
  `clang-format -i fronts/qt/src/**/*.{h,cpp}`
