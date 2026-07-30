# Agent instructions — cloudmus Qt frontend (C++)

Scope: `fronts/qt/` only (the C++20 Qt desktop frontend). The rest of the
repo is Python and follows its own conventions.

## Naming

- **Classes, structs, and namespaces: `PascalCase`** — `Track`, `RpcClient`,
  `namespace Rpc`, `namespace Playback`.
- **Methods and fields: `camelCase`** — `fromJson()`, `requestId`,
  `durationMs`.
- Wire-format field names (from `protocol/schema/*.yaml` /
  `protocol/methods.yaml`) are already `camelCase` and are used as-is for
  struct members in generated code; a name that collides with a C++ keyword
  (only `explicit` today) gets a trailing underscore on the member only —
  see `fronts/qt/src/Rpc/JsonValidation.h` / `protocol/codegen/cpprender.py`.

## Formatting

Format with `clang-format` using `fronts/qt/_clang-format` (note: named
`_clang-format`, not `.clang-format` — this is clang-format's own supported
alternate name, recognized identically; either name works, this repo uses
the underscore form). Run before committing:

```
clang-format -i fronts/qt/src/**/*.{h,cpp} fronts/qt/generated/*.h
```

`clang-format` only handles formatting (braces, indentation, line breaks,
include ordering) — it does not enforce the naming rules above; those are a
convention to follow by hand (or via `clang-tidy`'s
`readability-identifier-naming` later, if that gets added).

## Folder layout = namespace layout

A folder under `fronts/qt/src/` holding code that belongs to a given
namespace must be named after that namespace, exactly (case-sensitive):
code in `namespace Rpc` lives in `fronts/qt/src/Rpc/`, code in a future
`namespace Playback` would live in `fronts/qt/src/Playback/`, and so on.

This applies to a folder's "real" organizational namespace — the module it
implements. It does not mean every last free function needs its own nested
namespace: a small internal-only helper namespace confined to one header
(e.g. an implementation-detail sub-namespace) is still fine nested *inside*
the module's namespace (e.g. `Rpc::Detail`), without needing a folder of its
own.

`fronts/qt/generated/` is the one exception: it's `protocol/codegen`'s
output, not hand-organized module code, so it isn't required to mirror a
namespace 1:1 (its structs are currently global — see `protocol/README.md`
if that changes).
