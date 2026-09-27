# cloudmus Source/Front RPC Protocol

**Version:** `1.1` (see §12 for versioning rules)

## 1. Overview

This protocol connects a **front** (the UI application — TUI, Qt, GTK, ...)
to one or more **sources** (per-backend adapters — Yandex Music, VK,
SoundCloud, internet radio, ...). A source is a standalone process, spawned
by the front, speaking JSON-RPC 2.0 over its own stdin/stdout. The front and
each source may be written in different languages; the only shared contract
is this document.

Design goals, in order:
1. Trivial to implement a new source in any language (no framing library, no
   codegen required).
2. A source declares what it can do (**capabilities**) instead of the front
   hardcoding per-backend behavior.
3. The front can play audio itself (reusing one playback engine across every
   source that supports it) or defer to a source that plays itself.

This document is the normative reference. If a source or front's behavior
disagrees with this document, the document wins.

---

## 2. Transport & framing

- Communication is over the source process's **stdin** (front → source) and
  **stdout** (source → front). A source's **stderr** is free-form
  human-readable logging and is never parsed by the front.
- Encoding: **UTF-8**, no byte-order mark.
- Framing: **newline-delimited JSON (NDJSON)**. Exactly one JSON *value* per
  line, terminated by `\n`. No `Content-Length` headers, no multi-line
  pretty-printing.
- Each line is exactly one JSON-RPC 2.0 **Request**, **Response**, or
  **Notification** object (see §3). **Batches (JSON arrays of requests) are
  not supported** — a line MUST be a single object, never an array.
- Writers MUST serialize compactly (e.g. Python: `json.dumps(msg,
  separators=(",", ":"), ensure_ascii=False)`, then append `\n` and flush).
  Compact serialization is what guarantees a message never contains a raw,
  unescaped newline — pretty-printing breaks framing and MUST NOT be used
  on the wire.
- Readers MUST tolerate and skip blank lines, but a conformant writer never
  emits one.
- **A source's stdout carries the RPC stream exclusively.** Nothing else —
  no debug prints, no third-party library output, no warnings — may ever
  reach stdout. Every source implementation MUST redirect stdout for
  everything except the RPC writer before doing anything else at startup:

  ```python
  import sys, logging
  _rpc_out = sys.stdout       # capture the real stream first, before anyone else touches it
  sys.stdout = sys.stderr     # any stray print() from here on lands on stderr instead
  logging.basicConfig(stream=sys.stderr, level=logging.INFO)
  # only the RPC writer object holds a reference to _rpc_out; nothing else uses it
  ```

  This must also cover third-party dependencies that may write to stdout by
  default (HTTP client libraries emitting warnings, etc.) — configure their
  loggers/warning filters to stderr as well.
- The front similarly never expects anything but valid protocol lines on a
  source's stdout; a line that fails to parse as JSON is treated as a
  protocol violation (see §9, source unavailable handling in the
  architecture plan).

---

## 3. Message envelope

Standard JSON-RPC 2.0 objects, restated for clarity:

**Request** (front → source, expects a Response):
```json
{"jsonrpc":"2.0","id":1,"method":"catalog.listPlaylists","params":{}}
```

**Response — success:**
```json
{"jsonrpc":"2.0","id":1,"result":{"playlists":[...]}}
```

**Response — error:**
```json
{"jsonrpc":"2.0","id":1,"error":{"code":1300,"message":"Playlist not found","data":{"retryable":false}}}
```

**Notification** (source → front, no response expected, no `id`):
```json
{"jsonrpc":"2.0","method":"state/changed","params":{"state":"playing","trackId":"123"}}
```

Rules:
- `id` is a JSON number, assigned by the front, unique among that source's
  currently in-flight requests. Sources echo it back verbatim in the
  Response.
- **Sources never initiate Requests to the front in protocol version 1.0**
  (no reverse RPC). A source only ever sends Responses and Notifications.
  This keeps a front's dispatcher a pure response/notification consumer.
- All `method` names are namespaced as `namespace.methodName` (requests) or
  `namespace/eventName` (notifications) — the `.` vs `/` separator is what
  visually distinguishes a callable method from a push notification at a
  glance in logs and examples.

---

## 4. Lifecycle

### 4.1 `initialize` (request, front → source)

Must be the first request sent. A source must not emit any notification
before responding to `initialize` (an `auth/prompt` may immediately follow
the `initialize` response if `auth.required` is true and no valid session
exists — see §10).

**Params:**
```json
{
  "protocolVersion": "1.1",
  "front": {"name": "cloudmus-tui", "version": "0.1.0"}
}
```

**Result:**
```json
{
  "protocolVersion": "1.2",
  "source": {
    "id": "yandex-music",
    "name": "Yandex Music",
    "version": "0.1.0",
    "description": "Yandex Music streaming service"
  },
  "capabilities": { /* §5 */ }
}
```

`source.description` is optional — a short human-readable description of
the service, for a front to display alongside the name (e.g. in a
per-source info panel). Absent when a source doesn't supply one; a front
should just check for its presence rather than assume it's always there.

If the front's `protocolVersion` major component doesn't match what the
source supports, the source returns an error (code `1000`-range is for auth;
use a dedicated value `1` in the standard JSON-RPC "Invalid params" style, or
more precisely: reply with a normal error object, code `-32602` "Invalid
params", `data: {"reason": "protocolVersion mismatch", "supported": "1.2"}`).
The front then refuses to use that source at all — **v1 does no subset
negotiation.**

### 4.2 `shutdown` (request, front → source)

No params, empty result `{}`. Sent when the front is closing that source
down. After responding, the source should stop emitting notifications and
may exit; the front closes the source's stdin immediately after receiving
the response, which the source's read loop observes as EOF. If the process
hasn't exited within a short grace period after that, the front sends
`SIGTERM`, then `SIGKILL` as a last resort. There is no separate "exit"
notification — EOF on stdin is the signal.

### 4.3 Proxies (process environment)

Some services are only reachable through a proxy, chosen per source by the
user. The front passes it in the environment it spawns the source with —
nothing on the wire changes:

| Variable (also lowercase) | Value |
|---|---|
| `HTTP_PROXY`, `HTTPS_PROXY`, `ALL_PROXY` | `http://[user:pass@]host:port` or `socks5h://[user:pass@]host:port` |
| `NO_PROXY` | `localhost,127.0.0.1,::1` |

- A source must send **all** of its own network traffic through them
  (Python's `requests` and `yt-dlp` do so by default; `requests` needs
  `PySocks` for `socks5h://`). With `socks5h`, host names are resolved by the
  proxy too.
- For a source set to connect directly, the front removes these variables
  (inherited from its own environment) instead; left alone, they are the
  system's.
- The front fetches what a source hands it — stream URLs, cover art —
  through the same proxy: a service may only honor a stream URL from the
  address that asked for it.
- A different proxy takes effect by restarting the source (§4.2, then a new
  process and `initialize`).

---

## 5. Capabilities

Returned inside the `initialize` result. All fields are required unless
marked optional; a source should be explicit rather than relying on
defaults, since the front does not assume any implicit capability.

```jsonc
{
  "playback": {
    "providesStream": true,
    "selfPlayback": false,
    "controls": { "pause": false, "seek": false, "volume": false }
  },
  "browse": {
    "playlists": true,
    "likedTracks": true,
    "radio": true,
    "search": false
  },
  "feedback": { "like": true, "dislike": true, "skip": true },
  "download": true,
  "auth": { "required": true, "flow": "deviceCode" }
}
```

| Field | Type | Meaning |
|---|---|---|
| `playback.providesStream` | bool | Source resolves a playable stream/URI for the front to play via its own playback engine (see §8's `track/streamReady`). |
| `playback.selfPlayback` | bool | Source plays audio itself; the front only mirrors state pushed via `state/changed` and forwards transport commands to the source. **Must not be `true` at the same time as `providesStream`** — if a source sets both, the front logs a warning and behaves as if only `providesStream` were true. |
| `playback.controls.pause` / `.seek` / `.volume` | bool | Only meaningful when `selfPlayback` is `true`. Declares which transport commands (§7.3) the front may send. Ignored entirely in `providesStream` mode, where the front's own playback engine always has full pause/seek/volume control locally. |
| `browse.playlists` | bool | `catalog.listPlaylists` / `catalog.listTracks` supported. |
| `browse.likedTracks` | bool | `catalog.listLiked` supported. |
| `browse.radio` | bool | `catalog.startRadio` and `radio/tracksAdded` supported. |
| `browse.editPlaylists` | bool | Optional (1.3+). `catalog.getTrackPlaylists` / `addToPlaylist` / `removeFromPlaylist` (§7.6) are supported on the source's `editable` playlists. |
| `browse.search` | bool | `catalog.search` supported (reserved for future use; no method defined yet in v1 — see §13). |
| `feedback.like` / `.dislike` / `.skip` | bool | Corresponding `feedback.*` methods (§7.4) are accepted. |
| `download` | bool | `catalog.downloadTrack` (§7.5) is supported. |
| `downloadControl` | bool | Optional (1.7+). Downloads report `download/progress` and can be stopped with `catalog.cancelDownload` (§7.5). |
| `settings` | bool | Optional (1.4+). The source has settings of its own: `settings.describe` / `settings.update` (§7.7) are supported. |
| `auth.required` | bool | Whether the source needs an authenticated session before any `catalog.*`/`playback.*` call will succeed. |
| `auth.flow` | string enum | One of `"none"`, `"deviceCode"`, `"usernamePassword"`, `"oauthRedirect"`. Only meaningful when `auth.required` is `true`. See §10. |

The front should treat an absent/omitted nested object as "capability not
declared, do not attempt" (defensive default), but conformant sources are
expected to always emit the full shape above.

---

## 6. Shared data types

### Track
```jsonc
{
  "id": "51672522",
  "title": "Victory",
  "artists": [{"id": "719143", "name": "Aurolab"}],
  "album": {"id": "7224367", "title": "Victory", "coverUrl": "https://..."},   // optional
  "durationMs": 391200,
  "coverUrl": "https://...",     // optional, falls back to album cover if absent
  "liked": false,                 // optional, omitted if the source doesn't track like-state
  "disliked": false,              // optional (1.3+), omitted if the source doesn't track dislike-state
  "explicit": false,               // optional
  "webUrl": "https://music.yandex.ru/album/7224367/track/51672522"  // optional, added in 1.1 (§12);
                                    // a browsable page for this track. Absent when the source has no
                                    // such page (e.g. local-folder tracks). No matching capability flag —
                                    // same precedent as coverUrl/liked/explicit: front just checks presence.
}
```

### Playlist
```jsonc
{
  "id": "3",
  "title": "My Playlist",
  "description": "...",           // optional
  "coverUrl": "https://...",      // optional
  "trackCount": 42,
  "kind": "playlist",              // one of "playlist" | "liked" | "radioStation"
  "editable": true,                // optional (1.3+): the user's own playlist — tracks can be added/removed (§7.6)
  "featured": true                 // optional (1.6+): worth showing at the source's top level by default
}
```
`kind: "radioStation"` playlists (e.g. My Wave) have no fixed track
list — `catalog.listTracks` is not valid for them, `trackCount` is `0`
("not applicable"), and a front only offers play/skip-forward/
skip-back/stop for them (via `catalog.startRadio`, passing the playlist's
`id` as `seed`), never a browsable list. `kind: "liked"` playlists are
fetched via `catalog.listLiked`, not `catalog.listTracks` — their `id` is
not a valid `playlistId`. Any `kind` may carry `description`/`coverUrl`;
a front is expected to show them uniformly (e.g. a cover/description
banner) regardless of kind.

`featured` (1.6+) is the source's suggestion for which entries a front
shows up front — e.g. My Wave, Liked Tracks, a daily personal playlist —
when it has more to offer than fits comfortably (a dozen personal mixes,
recommended stations). It is only a default: which entries a front puts at
the top level is the user's choice (favorites), kept by the front. A front
talking to a source that marks nothing `featured` (e.g. one older than 1.6)
treats every `liked` and `radioStation` entry as featured.

### PlaybackState
```jsonc
{
  "state": "playing",              // one of "idle" | "playing" | "paused" | "buffering" | "stopped"
  "trackId": "51672522",           // optional, absent when state is "idle"
  "positionMs": 12345,             // optional
  "durationMs": 391200,            // optional
  "liked": false                    // optional
}
```
Only emitted by `selfPlayback` sources via `state/changed` (§8) — a
`providesStream` source's playback state is owned and tracked entirely by
the front's own playback engine and never needs to be pushed back.

### StreamDescriptor
```jsonc
{
  "kind": "url",
  "url": "https://api.music.yandex.net/get-mp3/...",
  "mimeType": "audio/mpeg",
  "headers": {"User-Agent": "..."}   // optional, extra HTTP headers the front must send when fetching
}
```
`kind` is a discriminator left open for future stream types (e.g. a local
file path) without breaking existing consumers; `"url"` is the only defined
value in v1.

---

## 7. Methods (front → source requests)

### 7.1 Catalog / browse

| Method | Params | Result | Requires capability |
|---|---|---|---|
| `catalog.listPlaylists` | `{}` | `{"playlists": [Playlist, ...]}` | `browse.playlists \|\| browse.likedTracks \|\| browse.radio` |
| `catalog.listTracks` | `{"playlistId": string, "cursor"?: string}` | `{"tracks": [Track, ...], "nextCursor"?: string}` | `browse.playlists` |
| `catalog.listLiked` | `{"cursor"?: string}` | `{"tracks": [Track, ...], "nextCursor"?: string}` | `browse.likedTracks` |
| `catalog.startRadio` | `{"seed"?: string}` | `{"stationId": string, "initialTracks": [Track, ...]}` | `browse.radio` |

`catalog.listPlaylists` is the single source of truth for everything a
front shows as a "playlist" under a source, including the source's built-in
special playlists: a source advertising `browse.radio` includes one or
more `kind: "radioStation"` entries (e.g. Yandex Music's My Wave and its
recommended stations, YouTube Music's personal mixes), and a source
advertising `browse.likedTracks` includes one `kind: "liked"` entry, both
alongside any real `kind: "playlist"` entries. A front therefore calls
`catalog.listPlaylists` whenever any of the three `browse.*` capabilities
above is true, not only `browse.playlists`.

- Recommended client-side timeout: **20 seconds** (longer than the ~5s
  default for other browse/auth calls — some backends fan this out into
  several chained upstream requests, e.g. youtube-music's ytmusicapi
  wrapper does three sequential calls per `listPlaylists`, the last one a
  scan of the home feed for the account's personal mixes).

`cursor` is an **opaque string** — the front only ever passes back exactly
what it last received in `nextCursor`; it has no structure a front is
allowed to depend on. Absence of `nextCursor` in a result means "no more
pages."

`catalog.startRadio`'s `seed` is source-defined (e.g. a genre id, or absent
for a generic personal station like Yandex's "My Wave" / `user:onyourwave`).
After the initial call, the front calls `feedback.trackStarted` /
`feedback.trackFinished` / `feedback.skip` (§7.4) as the station plays, and
the source pushes more tracks proactively via `radio/tracksAdded` (§8) —
there is no `catalog.getMoreRadioTracks` pull method; a source decides when
to top up the queue.

By default the front appends a `radio/tracksAdded` batch to its queue. A
source whose upstream recomputes the upcoming sequence in reaction to
feedback may set `replaceUpcoming: true` when that's what the user asked
for — e.g. Yandex's My Wave does it on a skip ("not this"), but after a
track played to the end it leaves the queue alone and only appends once it
runs low. With the flag, the batch replaces every
queued track after the one currently playing (or starting), so the queue
stays "already played + the station's current recommendations" rather
than growing by a full batch per track. Tracks the user queued explicitly
(Play Next / Add to Queue) are kept, and incoming tracks already in the
played part of the queue are dropped by the front.

### 7.2 Playback — `providesStream` sources

| Method | Params | Result |
|---|---|---|
| `playback.play` | `{"trackId": string}` | `{"accepted": true}` (fast ack — see §11.1 for the async resolution flow) |
| `playback.cancel` | `{"requestId": number}` | *(no response required; fire-and-forget notification-style call is also acceptable, but implemented as a request for symmetry — front does not wait on its result)* |

The front never sends `playback.pause/resume/seek/setVolume/next/previous`
to a `providesStream` source — those are handled entirely locally by the
front's own playback engine once it has a `StreamDescriptor`.

### 7.3 Playback — `selfPlayback` sources

| Method | Params | Result |
|---|---|---|
| `playback.play` | `{"trackId": string}` | `{"accepted": true}` |
| `playback.pause` | `{}` | `{}` |
| `playback.resume` | `{}` | `{}` |
| `playback.next` | `{}` | `{}` |
| `playback.previous` | `{}` | `{}` |
| `playback.seek` | `{"positionMs": number}` | `{}` |
| `playback.setVolume` | `{"volume": number}` (0–100) | `{}` |

Only send the methods whose corresponding `capabilities.playback.controls.*`
flag is `true` (§5); e.g. don't send `playback.seek` to a source that
declared `controls.seek: false`. All state resulting from these calls is
reflected back via `state/changed` (§8), not via each method's result —
results here are just acks.

### 7.4 Feedback

| Method | Params | Requires capability |
|---|---|---|
| `feedback.like` | `{"trackId": string}` | `feedback.like` |
| `feedback.dislike` | `{"trackId": string}` | `feedback.dislike` |
| `feedback.unlike` | `{"trackId": string}` | `feedback.like` |
| `feedback.undislike` | `{"trackId": string}` | `feedback.dislike` |
| `feedback.trackStarted` | `{"trackId": string}` | `browse.radio` (radio attribution) |
| `feedback.trackFinished` | `{"trackId": string, "playedMs": number}` | `browse.radio` |
| `feedback.skip` | `{"trackId": string, "playedMs": number}` | `feedback.skip` or `browse.radio` |

All return `{}` on success. `trackStarted`/`trackFinished`/`skip` exist
specifically to let a radio/wave-style source adapt its recommendations —
send them whenever `browse.radio` is the active capability, independent of
whether `feedback.skip` itself is separately declared.

`unlike`/`undislike` reverse a previous `like`/`dislike` (reuse the same
capability flag — a source that can like can un-like). Liking/disliking a
track is exclusive: a successful `like` clears any prior dislike and vice
versa (both shipped sources enforce this server-side), so a front should
mirror that locally rather than showing both states lit up at once.

### 7.6 Editing playlists

| Method | Params | Result | Requires capability |
|---|---|---|---|
| `catalog.getTrackPlaylists` | `{"trackId": string}` | `{"playlistIds": [string, ...]}` | `browse.editPlaylists` |
| `catalog.addToPlaylist` | `{"playlistId": string, "trackId": string}` | `{"trackCount": number}` | `browse.editPlaylists` |
| `catalog.removeFromPlaylist` | `{"playlistId": string, "trackId": string}` | `{"trackCount": number}` | `browse.editPlaylists` |

- Only playlists the source marked `editable: true` (the user's own) are
  valid targets; `liked` and `radioStation` playlists never are (liking is
  §7.4's job).
- `getTrackPlaylists` answers "which of my editable playlists contain this
  track" in one call, so a front can show membership checkboxes without
  fetching every playlist itself; a source may cache playlist contents to
  answer it, keeping the cache in step with its own add/remove calls.
- `addToPlaylist` appends the track; `removeFromPlaylist` removes its first
  occurrence. Both return the playlist's new `trackCount`.

### 7.5 Download

| Method | Params | Result | Requires capability |
|---|---|---|---|
| `catalog.downloadTrack` | `{"trackId": string, "destDir": string, "downloadId"?: string}` | `{"path": string}` | `download` |
| `catalog.cancelDownload` | `{"downloadId": string}` | `{}` | `downloadControl` (1.7+) |

- `destDir` is an absolute path to an existing directory, supplied by the
  front (e.g. the user's current working directory, or a configured music
  folder).
- The source resolves the track's stream itself, downloads it, writes
  appropriate file tags, and returns the **absolute path of the saved
  file** in `path`.
- This is a plain, synchronous-style request/response — unlike
  `playback.play`, it is not subject to the `track/streamReady` /
  `requestId` correlation flow in §11.1, since it's a one-shot user action
  outside the playback queue.
- Recommended client-side timeout: **60 seconds** (longer than the ~5s
  default for browse/auth calls — this is a file transfer, not a metadata
  fetch).
- To download an entire playlist, the front calls `catalog.downloadTrack`
  once per track; there is no `catalog.downloadPlaylist` method.
- If a source's capability declares `"download": false`, the front must not
  call this method; a source receiving it anyway without the capability
  should reply with a capability error (code `1100`, see §9).

**Progress and cancelling (1.7+, capability `downloadControl`).** The front
may name a download with a `downloadId` of its choosing (unique among its
downloads in flight on that source). A source declaring `downloadControl`
then:

- sends `download/progress` notifications
  `{"downloadId": string, "receivedBytes": integer, "totalBytes"?: integer}`
  while the file transfers — `totalBytes` omitted while unknown — no more
  often than a few times a second;
- accepts `catalog.cancelDownload` `{"downloadId": string}`: it stops that
  download, removes what it had written of the file, replies `{}`, and
  the `catalog.downloadTrack` call fails with code `1410` ("download
  cancelled"). Cancelling a download that already finished, or an unknown
  `downloadId`, is not an error — it replies `{}` and does nothing.

Since progress shows the download is alive, a front may give a named
download a much longer timeout than the 60 seconds above. A source without
`downloadControl` ignores `downloadId`.

### 7.7 Settings

| Method | Params | Result | Requires capability |
|---|---|---|---|
| `settings.describe` | `{}` | `SettingsDescription` (below) | `settings` |
| `settings.update` | `{"values": {key: value, ...}}` | `{}` | `settings` |

A source's own options (stream quality, a library folder, API
credentials, ...) are described by the source itself, so a front can
build a settings form for any source without knowing it. The values live
with the source — typically next to its auth token — not with the front;
every front shares them.

```jsonc
{
  "groups": [
    {"id": "playback", "title": "Playback"}      // description optional
  ],
  "fields": [
    {
      "key": "streamQuality",
      "group": "playback",                        // optional
      "label": "Stream quality",
      "description": "Applies from the next track.",  // optional
      "type": "enum",
      "default": "best",
      "value": "high",
      "options": [
        {"value": "best", "label": "Best available"},
        {"value": "high", "label": "Up to 192 kbps"}
      ]
    }
  ]
}
```

- `groups` are listed in display order; `fields` in display order within
  their group. A field without `group` goes into an untitled group shown
  before the titled ones.
- `type` decides both the control and the JSON type of `default`/`value`:

  | `type` | Value | Extra fields |
  |---|---|---|
  | `boolean` | boolean | — |
  | `integer` | integer | `min`, `max` (optional) |
  | `string` | string | `placeholder` (optional) |
  | `secret` | string | `isSet`, `placeholder` (optional) |
  | `enum` | string, one of `options[].value` | `options` (required) |
  | `path` | string, an absolute path or `""` | `pathKind`: `"directory"` or `"file"`; `placeholder` (optional) |

- A `secret`'s `value` is always `""`: it never leaves the source.
  `isSet` says whether one is stored. A front sends a secret in
  `settings.update` only when the user typed a new one; `""` clears it.
- `restartRequired: true` marks a setting the source only picks up at its
  next start. After a successful `settings.update` touching one, the front
  restarts the source (`shutdown`, then spawn and `initialize` again).
  Everything else takes effect as soon as `settings.update` returns.
- `settings.update` carries only the keys being changed and is
  all-or-nothing: the source validates every value first, and on any bad
  one saves nothing and replies `-32602` (invalid params) with
  `data: {"key": <the offending key>, "message": <for the user>}`, which a
  front shows next to that field. An unknown key is an error too.
- Labels, descriptions and option labels are for display, in English, like
  `source.name`.

---

## 8. Notifications (source → front, unsolicited)

| Notification | Params | Emitted by |
|---|---|---|
| `state/changed` | `PlaybackState` (§6) | `selfPlayback` sources, whenever play/pause/seek/volume/track changes |
| `track/streamReady` | `{"requestId": number, "trackId": string, "stream": StreamDescriptor}` | `providesStream` sources, asynchronously after `playback.play` |
| `radio/tracksAdded` | `{"stationId": string, "tracks": [Track, ...], "replaceUpcoming"?: bool}` | Any source with `browse.radio`, proactively as it tops up the queue (§7.1 for `replaceUpcoming`) |
| `download/progress` | `{"downloadId": string, "receivedBytes": number, "totalBytes"?: number}` | Sources with `downloadControl` (1.7+), while a named download transfers (§7.5) |
| `auth/prompt` | flow-specific, see §10 | Sources with `auth.required: true`, mid-flow |
| `auth/statusChanged` | `{"status": "authenticated"} \| {"status": "error", "message": string}` | Any source, on auth state transitions |
| `error` | `{"code": number, "message": string, "data"?: object}` | Any source, for non-fatal issues worth surfacing in the UI (e.g. "track unavailable, skipping") |

### 8.1 `track/streamReady` and the `requestId` correlation

`playback.play`'s JSON-RPC `id` is echoed back inside this notification's
params as `requestId` (a notification has no `id` of its own per JSON-RPC
2.0, but nothing prevents its `params` from carrying an application-level
value equal to a prior request's `id` — this is exactly that). See §11.1 for
the full race-condition rationale and handling; in short, the front only
acts on a `track/streamReady` whose `requestId` matches the most recent
`playback.play` it issued and discards any other.

---

## 9. Error codes

Standard JSON-RPC 2.0 codes (`-32700` parse error, `-32600` invalid request,
`-32601` method not found, `-32602` invalid params, `-32603` internal error)
apply as usual and stay in the reserved `-32768..-32000` range.

Application-specific errors use a disjoint **positive** range:

| Range | Meaning |
|---|---|
| 1000–1099 | Auth errors (not authenticated, session expired, flow failed) |
| 1100–1199 | Capability errors (method called without the declaring capability) |
| 1200–1299 | Upstream/network errors (backend unreachable, rate-limited, timed out) |
| 1300–1399 | Resource errors (track/playlist/station not found) |
| 1400–1499 | State errors (e.g. `playback.seek` with nothing loaded); `1410`: a download was cancelled (§7.5, 1.7+) |

All application errors include:
```json
{"code": 1300, "message": "Track not found", "data": {"retryable": false, "detail": "id=51672522"}}
```
`data.retryable` tells the front whether it's reasonable to automatically
retry (e.g. a `1200` network blip) versus surface immediately and stop (e.g.
`1300` not found).

---

## 10. Auth flows

Driven entirely by `capabilities.auth.flow`. Methods:

- `auth.getStatus` (request) → `{"status": "unauthenticated"|"pending"|"authenticated"|"error", "detail"?: string}`
- `auth.start` (request, `{}`) → `{}` ack; source begins the flow asynchronously
- `auth.submit` (request, `{"fields": {"username": "...", "password": "..."}}`) → `{}` ack; only used by `usernamePassword` flows
- `auth.cancel` (request, `{}`) → `{}` ack
- `auth.logout` (request, `{}`) → `{}` ack; clears the source's stored session

The front calls `auth.getStatus` right after `initialize` when
`auth.required` is true. If not authenticated, it calls `auth.start` and
waits for `auth/prompt` / `auth/statusChanged` notifications.

Any `auth/prompt` may carry an optional `"message"` (1.5+): what the user
has to do, in the source's own words — a front can't know that a
`usernamePassword` field expects, say, headers copied from a browser. It
may use a minimal HTML subset: `<a href="https://...">`, `<b>`, `<i>`,
`<code>`, `<br>`. A front renders those (links open in the user's browser)
and shows anything else as plain text; one that ignores `message` falls back
to its own generic wording.

```json
{"flow": "usernamePassword",
 "message": "Open <a href=\"https://music.youtube.com\">music.youtube.com</a> signed in, then paste its <b>Request Headers</b> below.",
 "fields": [{"name": "headers", "secret": false, "multiline": true}]}
```

### 10.1 `deviceCode` flow (e.g. Yandex OAuth device flow)
```
--> auth.start {}
<-- {} (ack)
<-- auth/prompt {"flow":"deviceCode","url":"https://oauth.yandex.ru/...","code":"ABCD-1234","expiresInSec":600}
      (front displays url + code to the user; user completes this out-of-band, in any browser)
<-- auth/statusChanged {"status":"authenticated"}
```

### 10.2 `usernamePassword` flow
```
--> auth.start {}
<-- {} (ack)
<-- auth/prompt {"flow":"usernamePassword","fields":[
      {"name":"username","secret":false},{"name":"password","secret":true}]}
      (front renders a generic small form from this field list — no source-specific UI needed)
--> auth.submit {"fields":{"username":"...","password":"..."}}
<-- {} (ack)
<-- auth/statusChanged {"status":"authenticated"}         // or {"status":"error","message":"..."}
```

A field descriptor may also carry an optional `"multiline": true` (default
`false`) alongside `name`/`secret`, hinting that its value is expected to be
long pasted text (e.g. a browser's raw request headers) rather than a short
single-line value — a front should render such a field as a multi-line text
box instead of a single-line input. Same "generic form, no source-specific
UI" principle applies: a front that ignores this hint still works correctly
(a single-line input round-trips the value fine, just awkward to read/edit),
it's a rendering hint only. `cloudmus_backend_ytmusic`'s browser-header
paste (see that backend's `auth.py`) is the first real user of this.

### 10.3 `oauthRedirect` flow
```
--> auth.start {}
<-- {} (ack)
<-- auth/prompt {"flow":"oauthRedirect","url":"https://..."}
      (front just opens this URL in a browser; the SOURCE owns any local-loopback
       redirect listener needed to catch the callback — not the front)
<-- auth/statusChanged {"status":"authenticated"}
```
No concrete source uses this flow yet in v1; the shape above is reserved.

Credentials/tokens are stored inside each source's own private config
directory. The front never sees them and never needs source-specific
knowledge to drive any of the three flows above.

---

## 11. Playback flow examples

### 11.1 `providesStream` source — play, with the race condition explained

```
--> playback.play {"trackId":"51672522"}      id=42
<-- {"accepted":true}                          id=42
... (source resolves the stream asynchronously, e.g. an HTTP round-trip) ...
<-- track/streamReady {"requestId":42,"trackId":"51672522","stream":{"kind":"url","url":"https://...","mimeType":"audio/mpeg"}}
      (front's playback engine now hands this URL to its local mpv instance and starts playing)
```

**Why the ack is separated from stream resolution:** blocking the RPC
response on a network round-trip would stall the front's event loop (or at
minimum add needless latency to something that should feel instant, like
pressing "next"). Instead, `playback.play` acks immediately and the actual
stream shows up later as a notification.

**The race this creates, and how it's resolved:** if the user presses "next"
twice quickly, two `playback.play` calls go out (ids `42` then `43`) before
either resolves. Their `track/streamReady` notifications can arrive in
either order. The front's playback engine keeps a single
`latestRequestId` variable, overwritten by every `playback.play` call it
issues. When a `track/streamReady` arrives, the engine **only acts on it if
its `requestId` equals `latestRequestId`** — otherwise it's silently
discarded, regardless of arrival order. Whenever a `playback.play` is
superseded before resolving, the front also sends a fire-and-forget
`playback.cancel {"requestId": <superseded id>}` so the source can drop the
now-pointless in-flight work. A ~10 second client-side timeout applies to
any pending `playback.play`; on timeout the front discards the correlation
entry and surfaces an error to the user.

### 11.2 `selfPlayback` source — play and transport control

```
--> playback.play {"trackId":"radio-track-9"}   id=10
<-- {"accepted":true}                            id=10
<-- state/changed {"state":"playing","trackId":"radio-track-9","positionMs":0,"durationMs":180000}
... time passes, source pushes periodic updates or updates on every transition ...
<-- state/changed {"state":"playing","trackId":"radio-track-9","positionMs":15000,"durationMs":180000}
--> playback.setVolume {"volume":80}             id=11    (only if capabilities.playback.controls.volume)
<-- {}                                            id=11
<-- state/changed {"state":"playing","trackId":"radio-track-9","positionMs":15200,"durationMs":180000}
```

---

## 12. Versioning

`protocolVersion` is a `"MAJOR.MINOR"` string. A front refuses to use a
source whose major version doesn't match what it supports (see §4.1) — no
subset/partial negotiation in v1. Additive, backward-compatible changes
(new optional fields, new notification types, new optional methods) bump
MINOR; anything that changes the meaning of an existing field or removes
something bumps MAJOR. Changes are tracked in `docs/protocol-changelog.md`.

---

## 13. Known gaps / reserved-but-unspecified (not implemented in v1)

- `catalog.search` — capability flag exists (`browse.search`) but no method
  is defined yet; reserved for a future minor version.
- `catalog.resolveUrl` (resolving a pasted service URL/ID directly) is not
  part of the protocol; if a source wants this, it's exposed only through
  that source's own standalone debug CLI, outside RPC.
- `auth.flow: "oauthRedirect"` has a defined message shape (§10.3) but no
  source implements it yet — treat as provisional until a real
  implementation exercises it.

---

## Appendix: full example session (NDJSON)

```ndjson
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"1.1","front":{"name":"cloudmus-tui","version":"0.1.0"}}}
{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"1.1","source":{"id":"yandex-music","name":"Yandex Music","version":"0.1.0"},"capabilities":{"playback":{"providesStream":true,"selfPlayback":false,"controls":{"pause":false,"seek":false,"volume":false}},"browse":{"playlists":true,"likedTracks":true,"radio":true,"search":false},"feedback":{"like":true,"dislike":true,"skip":true},"download":true,"auth":{"required":true,"flow":"deviceCode"}}}}
{"jsonrpc":"2.0","id":2,"method":"auth.getStatus","params":{}}
{"jsonrpc":"2.0","id":2,"result":{"status":"unauthenticated"}}
{"jsonrpc":"2.0","id":3,"method":"auth.start","params":{}}
{"jsonrpc":"2.0","id":3,"result":{}}
{"jsonrpc":"2.0","method":"auth/prompt","params":{"flow":"deviceCode","url":"https://oauth.yandex.ru/device","code":"WXYZ-9876","expiresInSec":600}}
{"jsonrpc":"2.0","method":"auth/statusChanged","params":{"status":"authenticated"}}
{"jsonrpc":"2.0","id":4,"method":"catalog.listPlaylists","params":{}}
{"jsonrpc":"2.0","id":4,"result":{"playlists":[{"id":"3","title":"Favorites","trackCount":42,"kind":"playlist"}]}}
{"jsonrpc":"2.0","id":5,"method":"catalog.listTracks","params":{"playlistId":"3"}}
{"jsonrpc":"2.0","id":5,"result":{"tracks":[{"id":"51672522","title":"Victory","artists":[{"id":"719143","name":"Aurolab"}],"durationMs":391200}]}}
{"jsonrpc":"2.0","id":6,"method":"playback.play","params":{"trackId":"51672522"}}
{"jsonrpc":"2.0","id":6,"result":{"accepted":true}}
{"jsonrpc":"2.0","method":"track/streamReady","params":{"requestId":6,"trackId":"51672522","stream":{"kind":"url","url":"https://api.music.yandex.net/get-mp3/...","mimeType":"audio/mpeg"}}}
{"jsonrpc":"2.0","id":7,"method":"catalog.downloadTrack","params":{"trackId":"51672522","destDir":"/home/vlad/Music"}}
{"jsonrpc":"2.0","id":7,"result":{"path":"/home/vlad/Music/Aurolab - Victory.mp3"}}
{"jsonrpc":"2.0","id":8,"method":"shutdown","params":{}}
{"jsonrpc":"2.0","id":8,"result":{}}
```
