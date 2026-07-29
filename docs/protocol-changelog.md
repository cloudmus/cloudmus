# Protocol changelog

Tracks changes to the cloudmus front/backend JSON-RPC protocol
(`docs/protocol.md`). See that document §12 for versioning rules: MINOR bumps
are additive/backward-compatible, MAJOR bumps change or remove existing
meaning.

## 1.0 — initial version

Initial protocol design: NDJSON/JSON-RPC 2.0 transport, `initialize`
handshake with capability negotiation, catalog/playback/feedback/auth
namespaces, download capability, `track/streamReady` request/notification
correlation. See `docs/protocol.md` for the full spec.
