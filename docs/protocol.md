# cloudmus Source/Front RPC Protocol

**Version:** `1.0` (see §12 for versioning rules)

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
  "protocolVersion": "1.0",
  "front": {"name": "cloudmus-tui", "version": "0.1.0"}
}
```

**Result:**
```json
{
  "protocolVersion": "1.0",
  "source": {"id": "yandex-music", "name": "Yandex Music", "version": "0.1.0"},
  "capabilities": { /* §5 */ }
}
```

If the front's `protocolVersion` major component doesn't match what the
source supports, the source returns an error (code `1000`-range is for auth;
use a dedicated value `1` in the standard JSON-RPC "Invalid params" style, or
more precisely: reply with a normal error object, code `-32602` "Invalid
params", `data: {"reason": "protocolVersion mismatch", "supported": "1.0"}`).
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
| `browse.search` | bool | `catalog.search` supported (reserved for future use; no method defined yet in v1 — see §13). |
| `feedback.like` / `.dislike` / `.skip` | bool | Corresponding `feedback.*` methods (§7.4) are accepted. |
| `download` | bool | `catalog.downloadTrack` (§7.5) is supported. |
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
  "explicit": false                // optional
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
  "kind": "playlist"               // one of "playlist" | "liked" | "radioStation"
}
```

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
| `catalog.listPlaylists` | `{}` | `{"playlists": [Playlist, ...]}` | `browse.playlists` |
| `catalog.listTracks` | `{"playlistId": string, "cursor"?: string}` | `{"tracks": [Track, ...], "nextCursor"?: string}` | `browse.playlists` |
| `catalog.listLiked` | `{"cursor"?: string}` | `{"tracks": [Track, ...], "nextCursor"?: string}` | `browse.likedTracks` |
| `catalog.startRadio` | `{"seed"?: string}` | `{"stationId": string, "initialTracks": [Track, ...]}` | `browse.radio` |

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
| `feedback.trackStarted` | `{"trackId": string}` | `browse.radio` (radio attribution) |
| `feedback.trackFinished` | `{"trackId": string, "playedMs": number}` | `browse.radio` |
| `feedback.skip` | `{"trackId": string, "playedMs": number}` | `feedback.skip` or `browse.radio` |

All return `{}` on success. `trackStarted`/`trackFinished`/`skip` exist
specifically to let a radio/wave-style source adapt its recommendations —
send them whenever `browse.radio` is the active capability, independent of
whether `feedback.skip` itself is separately declared.

### 7.5 Download

| Method | Params | Result | Requires capability |
|---|---|---|---|
| `catalog.downloadTrack` | `{"trackId": string, "destDir": string}` | `{"path": string}` | `download` |

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

---

## 8. Notifications (source → front, unsolicited)

| Notification | Params | Emitted by |
|---|---|---|
| `state/changed` | `PlaybackState` (§6) | `selfPlayback` sources, whenever play/pause/seek/volume/track changes |
| `track/streamReady` | `{"requestId": number, "trackId": string, "stream": StreamDescriptor}` | `providesStream` sources, asynchronously after `playback.play` |
| `radio/tracksAdded` | `{"stationId": string, "tracks": [Track, ...]}` | Any source with `browse.radio`, proactively as it tops up the queue |
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
| 1400–1499 | State errors (e.g. `playback.seek` with nothing loaded) |

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
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"1.0","front":{"name":"cloudmus-tui","version":"0.1.0"}}}
{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"1.0","source":{"id":"yandex-music","name":"Yandex Music","version":"0.1.0"},"capabilities":{"playback":{"providesStream":true,"selfPlayback":false,"controls":{"pause":false,"seek":false,"volume":false}},"browse":{"playlists":true,"likedTracks":true,"radio":true,"search":false},"feedback":{"like":true,"dislike":true,"skip":true},"download":true,"auth":{"required":true,"flow":"deviceCode"}}}}
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
