# Protocol changelog

Tracks changes to the cloudmus front/backend JSON-RPC protocol
(`docs/protocol.md`). See that document §12 for versioning rules: MINOR bumps
are additive/backward-compatible, MAJOR bumps change or remove existing
meaning.

## 1.3 — `feedback.unlike` / `feedback.undislike`

- **Additive, backward-compatible**: two new methods, `feedback.unlike` and
  `feedback.undislike` (§7.4), reverse a previous `like`/`dislike` — same
  params shape as `feedback.like`/`feedback.dislike`, same required
  capability flags. A source that never declared `feedback.like`/`.dislike`
  is simply never sent these either.
- **Additive, backward-compatible**: `Track` gains an optional `disliked`
  field, the counterpart of `liked` — omitted when the source doesn't know
  (or doesn't track) dislike state. A source that does know should set
  `liked`/`disliked` on every track it returns, not just in `listLiked`.
- **Additive, backward-compatible**: playlist editing (§7.6) —
  `Playlist.editable`, capability `browse.editPlaylists`, and the methods
  `catalog.getTrackPlaylists`, `catalog.addToPlaylist`,
  `catalog.removeFromPlaylist`. Sources/fronts without them are unaffected.
- **Additive, backward-compatible**: `radio/tracksAdded` gains an optional
  `replaceUpcoming` flag (§7.1): the batch replaces the queue's unplayed
  tail instead of being appended. Absent means append, as before.

## 1.2 — `source.description`

- **Additive, backward-compatible**: `initialize`'s result gains an optional
  `source.description` field — a short human-readable description of the
  service/source (e.g. "Yandex Music streaming service"), for a front to
  display alongside the source's name. Absent when a source doesn't supply
  one. Same precedent as `Track.webUrl` below: a front just checks presence.

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
- **Clarification, no version bump**: `Playlist.kind`'s `liked` and
  `radioStation` values (and the optional `description`/`coverUrl` fields)
  were part of the schema from the start but never actually produced or
  consumed — a front had to synthesize "My Wave"/"Liked Tracks" sidebar
  entries itself from capability flags instead of reading them from
  `catalog.listPlaylists`. §7.1 now documents that a source **must**
  include a `kind: radioStation`/`liked` entry in `catalog.listPlaylists`
  when it advertises the corresponding `browse.*` capability, and that a
  front should call `catalog.listPlaylists` based on any of
  `browse.playlists`/`browse.likedTracks`/`browse.radio`, not
  `browse.playlists` alone. No wire-format change — every field involved
  was already valid per the 1.0 schema, so this doesn't bump the version.

## 1.0 — initial version

Initial protocol design: NDJSON/JSON-RPC 2.0 transport, `initialize`
handshake with capability negotiation, catalog/playback/feedback/auth
namespaces, download capability, `track/streamReady` request/notification
correlation. See `docs/protocol.md` for the full spec.
