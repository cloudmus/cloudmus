# cloudmus protocol: schema-first source of truth

This directory is the machine-readable source of truth for the cloudmus
front/backend JSON-RPC protocol. `docs/protocol.md` is still the normative
prose reference (if they disagree, `docs/protocol.md` wins — update this
directory to match, not the other way around), but the actual data shapes
and RPC surface live here as YAML, and per-language stubs are generated from
it rather than hand-copied.

```
protocol/
  schema/*.yaml     # data types (Track, Playlist, PlaybackState, StreamDescriptor, ...),
                    # JSON Schema draft-07 keywords, written in YAML
  methods.yaml      # the RPC surface itself: methods (request/response) and notifications
                    # (fire-and-forget push), each referencing schema/*.yaml for params/result
                    # shapes, plus the application error-code ranges
  codegen/          # the generator (Python + Jinja2) and its per-language templates
```

Why this exists, and why not an off-the-shelf tool (OpenRPC, quicktype):
neither has real codegen for the languages this protocol needs (Python, C++,
and eventually Go), and OpenRPC in particular has no way to model
backend-initiated push notifications, which is half of this protocol. See
`docs/protocol-changelog.md`'s `1.1` entry and the plan history for the full
reasoning; this is a small custom IDL deliberately scoped to exactly what
this protocol needs, not a general JSON-RPC framework.

## Regenerating

```
python protocol/codegen/generate.py --lang python   # -> backends/py-rpc-common/rpc_common/generated/
python protocol/codegen/generate.py --lang cpp       # -> fronts/qt/generated/
python protocol/codegen/generate.py --lang python cpp  # both
```

**Generated output is gitignored, not checked in** — both `generated/`
directories are build artifacts, always regenerated from `protocol/` rather
than committed, so there's nothing to go stale relative to `protocol/schema`
+ `protocol/methods.yaml` in the first place. Practically:

- Run `python protocol/codegen/generate.py --lang python` before running any
  Python backend or its tests (`rpc_common.generated.*` won't exist on a
  fresh checkout otherwise) — CI must run this before the Python test suites.
- Run `python protocol/codegen/generate.py --lang cpp` before building
  `fronts/qt` — CI/the build must run this before invoking CMake.

Requires (dev-time only, not a runtime dependency of any generated code):
`pip install jinja2 pyyaml`. The C++ target also formats its own output with
`clang-format` (via `fronts/qt/_clang-format`, see `fronts/qt/AGENTS.md`) as
the last step of generation; if `clang-format` isn't on `PATH`, generation
still succeeds but prints a warning and leaves the output unformatted.

## Adding a new method, notification, or field

1. Add/edit the relevant node in `methods.yaml` (or a data type in
   `schema/*.yaml`, referenced from `methods.yaml` via `$ref: "schema/x.yaml"`).
2. Anonymous nested objects (not defined in their own `schema/*.yaml` file)
   need an explicit `x-name: SomeName` key so the generator has a class name
   to give them — see `schema/track.yaml`'s `artists`/`album` for examples.
   A bare `{type: object, properties: {}}` means "no fields" (used for
   empty params/results like `auth.cancel`'s `{}`) and needs no name.
3. Regenerate both languages, run the Python test suites
   (`backends/py-rpc-common`, `backends/yandex-music`, `backends/local-folder`),
   and rebuild whatever's consuming `fronts/qt/generated/`.
4. Update `docs/protocol.md` prose and add a `docs/protocol-changelog.md`
   entry (MINOR for anything additive/optional, MAJOR for anything that
   changes or removes existing meaning — see `docs/protocol.md` §12).

## What the generator does and doesn't do

Supported schema constructs: `string`/`integer`/`number`/`boolean`, `array`,
`object` with fixed `properties` (a named struct/dataclass), `$ref` to
another schema file, string `enum`, and `additionalProperties`-based maps or
free-form ("any") objects. This is deliberately not a general JSON-Schema
engine — it only supports what this protocol actually uses. Both language
targets generate real validation code inline (required-field presence,
per-field type/enum checks) rather than deferring to a runtime schema
interpreter — see `schema_ir.py`/`pyrender.py`/`cpprender.py`.

Python output (`backends/py-rpc-common/rpc_common/generated/`):
`models.py` (dataclasses, `to_dict()`/`from_dict()`), `methods.py` (a
`BackendMethods` typed reference `Protocol` — not required to subclass — and
`emit_*` notification helpers, used by backends for the notifications whose
wire shape is fully captured by their schema; one exception exists today —
`auth/prompt`'s payload varies per `auth.flow` and isn't modeled as a
`oneOf`, so it's still sent by hand in `cloudmus_backend_yandex/auth.py`).

C++ output (`fronts/qt/generated/`): `Models.h` (structs with `fromJson`/
`toJson`) and `RpcMethods.h` (typed call-wrapper declarations per method,
forward-declaring the hand-written `Task<T>`/`RpcClient` runtime that
implements them — see `fronts/qt/src/Rpc/`, `namespace Rpc`, per
`fronts/qt/AGENTS.md`'s folder-matches-namespace rule). C++ field names that collide
with a C++20 keyword (only `explicit` today) get a trailing underscore on
the struct member; the JSON wire key is untouched.

Generated C++ bodies are one line per field (`out.id =
Rpc::requiredField<QString>(obj, "Track", "id");`), not inlined
presence/type-check blocks — the actual validation logic lives once, as a
small set of hand-written templates in
`fronts/qt/src/Rpc/JsonValidation.h` (`requiredField<T>`/`optionalField<T>`/
`requiredArray<T>`/`requiredMap<T>`/`optionalMap<T>`/`required|optionalEnumField`,
plus the `toJson*`/`insertOptional*` counterparts), keyed on a `JsonField<T>`
trait (specialized for `QString`/`int`/`double`/`bool`/`QJsonValue`; the
primary template covers any generated struct via its own `fromJson`/`toJson`).
Uses only Qt (`QJsonObject`/`QJsonArray`/`QJsonValue`, `QString`, `QMap`) plus
the C++ standard library — no third-party JSON library.

Go is not generated yet (no backend rewrite has started) — extending
`generate.py` with a `--lang go` and a `templates/go/` directory when that
work begins is the intended path; nothing about the schema or the generator
needs to change to support it.
