# Protocol changelog

Tracks changes to the cloudmus front/backend JSON-RPC protocol
(`docs/protocol.md`). See that document §12 for versioning rules: MINOR bumps
are additive/backward-compatible, MAJOR bumps change or remove existing
meaning.

## 1.11 — localization

- **Additive, backward-compatible**: optional `initialize` param `locale`,
  optional capability `localization` (`{locales: [...]}`) and method
  `localization.setLanguage` (§7.8). Sources answer user-facing strings
  (settings labels, error messages, auth prompts, self-made titles) in the
  selected language, English when unsupported.

## 1.10 — sources without a network

- **Additive, backward-compatible**: optional capability `network` (§5),
  `true` when absent. A source declaring `false` (local-folder) has no
  Connection setting in the front, and its streams and covers are never
  routed through a proxy.

## 1.9 — recommended client-side timeouts (no wire change)

- **No wire change** (`protocolVersion` stays 1.8): bumped the *recommended
  client-side timeouts* for `catalog.listTracks`/`catalog.listLiked` to 30s
  and `settings.describe` to 20s (mirroring the 1.4-era `listPlaylists`
  bump). Rationale: the first `listTracks` against a large local-folder
  playlist reads music-file tags for every entry, and on a cold,
  AV-indexed Windows install — including the current Wine test rig — that
  can cross a 5s budget and surface as a spurious "request timed out" even
  though the backend answers fine once warm. Fronts MAY keep any shorter
  timeout of their own; the bump just raises the default guidance.

## 1.8 — resolving future streams

- **Additive, backward-compatible**: optional `playback.resolveStream`
  capability and method let a front obtain a `StreamDescriptor` for a future
  track without starting it, so it can preload audio while the current track
  continues. Sources without the capability keep the `playback.play` flow.

## 1.7 — download progress and cancelling

- **Additive, backward-compatible**: capability `downloadControl` (§5).
  `catalog.downloadTrack` takes an optional `downloadId`; a source with the
  capability reports `download/progress` (`receivedBytes`, `totalBytes`)
  for it and stops it on `catalog.cancelDownload`, failing the download
  with the new code `1410` (§7.5, §9). Sources without it ignore
  `downloadId`; fronts show those downloads without progress.

## 1.6 — several stations per source, `Playlist.featured`

- **Clarified**: `catalog.listPlaylists` may return more than one
  `kind: "radioStation"` entry (§7.1) — e.g. YouTube Music's personal mixes
  or Yandex Music's recommended stations next to My Wave. Each one starts
  with `catalog.startRadio` using its `id` as `seed`, as before.
- **Additive, backward-compatible**: `Playlist` gains an optional `featured`
  (§6) — the source's suggestion for what a front shows at the source's top
  level until the user chooses their own favorites. Without it a front
  features every `liked`/`radioStation` entry, as it effectively did before.

## 1.5 — `auth/prompt.message`

- **Additive, backward-compatible**: `auth/prompt` gains an optional
  `message` (§10) — the source's own instructions for the user, in a minimal
  HTML subset (`<a href>`, `<b>`, `<i>`, `<code>`, `<br>`). Fronts render it
  above the prompt; without it they keep their generic wording.

## Proxies through the environment (no version change)

- Documented (§4.3): the front may spawn a source with `HTTP_PROXY` /
  `HTTPS_PROXY` / `ALL_PROXY` (`http://` or `socks5h://`) and `NO_PROXY` set
  for a user-chosen per-source proxy, and the source must route all its
  traffic through them. No message changes; sources built on `requests` /
  `yt-dlp` already comply once they can speak SOCKS (`PySocks`).

## 1.4 — source settings

- **Additive, backward-compatible**: capability `settings` and the methods
  `settings.describe` / `settings.update` (§7.7). A source describes its own
  options — key, type (`boolean`/`integer`/`string`/`secret`/`enum`/`path`),
  label, description, default, current value, group — and a front builds
  the form from that alone. Values stay stored by the source. Sources and
  fronts without the capability are unaffected.

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
