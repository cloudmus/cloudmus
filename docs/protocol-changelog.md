# Protocol changelog

Tracks changes to the cloudmus front/backend JSON-RPC protocol
(`docs/protocol.md`). See that document §12 for versioning rules: MINOR bumps
are additive/backward-compatible, MAJOR bumps change or remove existing
meaning.

## 1.1 — schema-first protocol + `Track.webUrl`

- The protocol's data shapes and RPC surface are now schema-first: hand
  authored in `protocol/schema/*.yaml` (data types, JSON Schema draft-07,
  moved from `backends/py-rpc-common/schema/*.json`) and `protocol/methods.yaml`
  (methods/notifications/error ranges — the RPC surface itself, previously
  only prose). Python and C++ stubs are generated from these via
  `protocol/codegen/generate.py`; see `protocol/README.md`. No wire-format
  change from this move by itself.
- **Additive, backward-compatible**: `Track` gains an optional `webUrl`
  field — a browsable page for the track (e.g. a `music.yandex.ru` track
  page), when the source has one. Absent when a source has no such page
  (e.g. `local-folder`). No new capability flag, same precedent as
  `coverUrl`/`liked`/`explicit`: a front just checks presence.

## 1.0 — initial version

Initial protocol design: NDJSON/JSON-RPC 2.0 transport, `initialize`
handshake with capability negotiation, catalog/playback/feedback/auth
namespaces, download capability, `track/streamReady` request/notification
correlation. See `docs/protocol.md` for the full spec.
