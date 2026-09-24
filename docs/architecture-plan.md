# Architecture: Source/Front split for cloudmus (JSON-RPC protocol)

## Context

The project is named **cloudmus**. Today it's still one monolithic Python
app (the `ym_player` package, from before the rename) — a single process
imports the
`yandex_music` library directly, wraps it with an mpv-based player, and renders
a Textual TUI on top. That's fine for "a Yandex Music player" but doesn't scale
to the user's actual goal: **one UI (TUI today, maybe Qt/GTK later) that can
integrate many unrelated music services** (Yandex Music, VK, SoundCloud,
internet radio, ...) by writing a small adapter per service, without ever
touching the UI code again, and without the UI and the adapter needing to
share a programming language.

The fix is a clean protocol boundary: each backend becomes a **source** —
a standalone child process that speaks JSON-RPC 2.0 over its own stdin/stdout.
The UI becomes a **front** that spawns sources, negotiates what each one can
do (declared **capabilities**), and either lets the source play audio itself
or plays a stream the source hands back, using one shared mpv-based engine.
This plan designs that protocol in detail and lays out a concrete migration
of the existing `ym_player/*` code into the new split, plus a second,
deliberately different, source to prove the abstraction actually holds
across more than one backend.

Decisions already made with the user (do not re-litigate):
- **Monorepo**: one git repo, subdirectories per component, versioned together.
- **Two top-level component folders only**: `fronts/` (UI players — TUI today,
  Qt/GTK later) and `backends/` (per-service adapters). No separate top-level
  `protocol/` or `libs/` folder — protocol prose lives in `docs/protocol.md`;
  the shared JSON-RPC/NDJSON transport library (`py-rpc-common`) nests inside
  `backends/`, since backends are what actually run a server loop with it.
  **Superseded (protocol 1.1):** a top-level `protocol/` directory now exists
  after all — the protocol became schema-first (`protocol/schema/*.yaml` +
  `protocol/methods.yaml`, with per-language stubs generated via
  `protocol/codegen/`), consumed by Python backends, the C++ Qt front, and
  (later) a Go backend rewrite. That's genuinely language-agnostic content
  with no natural home under `backends/` anymore, unlike `py-rpc-common`
  (still backend-Python-specific, still nests inside `backends/` as before).
  See `protocol/README.md` and `docs/protocol-changelog.md`'s `1.1` entry.
- **Two backends for v1**: `backends/yandex-music` (real, migrated from
  today's code) and `backends/local-folder` (new: plays a local music
  directory tree — each subfolder becomes a playlist, or the whole root is one
  playlist if it only contains files directly). Both are `providesStream`
  backends; the `selfPlayback` branch is instead covered by a synthetic test
  fixture (see `docs/protocol.md` / testing strategy), not a shipped backend.
- **Discovery via manifest files**: each backend drops a small JSON manifest
  into a well-known directory; the front scans that directory at startup
  rather than hardcoding a backend list in its own config.
- **Download is a protocol capability**, not a backend-specific side channel:
  on a front-issued command, the backend downloads the track to a
  front-specified destination folder and returns the full saved path.

---

## 1. Transport & framing

**Newline-delimited JSON-RPC 2.0 (NDJSON) over stdio.** Rejected LSP-style
`Content-Length` framing: every message here is small metadata (never binary,
never a raw embedded newline once compactly serialized), so a length header
buys nothing and costs every future source-author a header parser. NDJSON is
`readline()` + `json.loads()` in literally any language.

Hard rules (put verbatim in `docs/protocol.md`):
- UTF-8, no BOM, exactly one JSON *value* per line.
- **No batch arrays.** Every line is exactly one Request, Response, or
  Notification object — keeps parsing trivial and unambiguous.
- Serialize compactly (`separators=(",",":")`, no `indent=`). This is what
  actually makes "no raw embedded newline" true — a pretty-printed dump
  would break framing. This is a MUST, not a style preference.
- **A source's stdout is exclusively the RPC channel.** All logging goes to
  stderr. Enforce this at process boot, not by code review — a stray
  `print()` anywhere in the dependency tree silently corrupts the stream.
  Every source's entrypoint does this first, before importing anything else
  that might print:
  ```python
  import sys, logging
  _rpc_out = sys.stdout        # grab the real stream first
  sys.stdout = sys.stderr      # any stray print() now lands on stderr
  logging.basicConfig(stream=sys.stderr, level=logging.INFO)
  # the RPC writer is the only thing that ever touches _rpc_out
  ```
  Known offenders to fix while migrating (see §7): `downloader.py`'s
  `print()` calls, any `click.echo` reused inside a source's server code
  path, and third-party libs (`yandex_music`, `requests`/`urllib3`) that may
  warn to stdout by default.

**Errors**: standard `{"code", "message", "data"}`. JSON-RPC's own reserved
range (`-32768..-32000`) stays reserved for transport/protocol errors.
Source-specific errors use a positive range instead:
| Range | Meaning |
|---|---|
| 1000–1099 | auth errors (not authenticated / expired / flow failed) |
| 1100–1199 | capability errors (method not supported by this source) |
| 1200–1299 | upstream/network errors (backend unreachable, rate-limited) |
| 1300–1399 | resource errors (track/playlist not found) |
| 1400–1499 | state errors (e.g. seek with nothing playing) |

All app errors carry `data: {"retryable": bool, "detail"?: string}`.

---

## 2. Handshake & capability negotiation

Modeled on LSP's `initialize`:

```jsonc
--> {"jsonrpc":"2.0","id":1,"method":"initialize","params":{
      "protocolVersion":"1.0",
      "front":{"name":"cloudmus-tui","version":"0.1.0"}
    }}
<-- {"jsonrpc":"2.0","id":1,"result":{
      "protocolVersion":"1.0",
      "source":{"id":"yandex-music","name":"Yandex Music","version":"0.1.0"},
      "capabilities": { /* see below */ }
    }}
```

On `protocolVersion` mismatch: hard-fail for v1 (front refuses the source
and surfaces an error) — no subset negotiation. Simple, revisit later if it
ever actually matters.

**Capabilities schema:**

```jsonc
{
  "playback": {
    "providesStream": true,   // source resolves a playable URI; front plays it via its own mpv
    "selfPlayback": false,    // source plays audio itself; front just mirrors pushed state
    "controls": { "pause": false, "seek": false, "volume": false }
    // controls.* only meaningful when selfPlayback=true — front forwards these as real
    // commands in that case; ignored entirely when providesStream is the active mode.
  },
  "browse": { "playlists": true, "likedTracks": true, "radio": true, "search": false },
  "feedback": { "like": true, "dislike": true, "skip": true },
  "download": true,           // NEW: see §4b — source can save a track to a given folder
  "auth": { "required": true, "flow": "deviceCode" }  // "none"|"deviceCode"|"usernamePassword"|"oauthRedirect"
}
```

A source must not set both `providesStream` and `selfPlayback` true; if it
does, the front logs a warning and prefers `providesStream`.

**Why the front prefers `providesStream` whenever offered:** one shared mpv
instance gives one continuous volume/seek UX across every source instead of
jumping between per-source playback semantics; avoids N processes fighting
over the audio device; reuses the already-working mpv wrapper instead of
every source reimplementing a player; and makes writing a new source cheap —
an author just returns a URL, not a bundled playback stack. `selfPlayback`
is the escape hatch for backends where no usable direct stream exists (DRM'd
SDKs, Chromecast/DLNA-style targets) — those sources own their whole
playback stack and the front becomes a thin remote control.

---

## 3. Auth flow abstraction

Generic enough for device-code (Yandex today), username/password, or
OAuth-redirect, all riding the same shapes:

- `auth.getStatus` → `{"status":"unauthenticated"|"pending"|"authenticated"|"error","detail"?}`
- `auth.start` → kicks off the flow (source starts polling/waiting internally)
- `auth.submit {"fields":{...}}` → only used by `usernamePassword`-style flows
- `auth.cancel`, `auth.logout`

Notifications, shaped by `flow`:
- `auth/prompt`:
  - deviceCode: `{"flow":"deviceCode","url":"https://...","code":"ABCD-1234","expiresInSec":600}`
  - usernamePassword: `{"flow":"usernamePassword","fields":[{"name":"username","secret":false},{"name":"password","secret":true}]}`
    — front renders a generic small form, no Yandex-specific knowledge needed.
  - oauthRedirect: `{"flow":"oauthRedirect","url":"https://..."}` — front just
    opens a browser; the source owns any local-loopback redirect listener
    (no concrete source needs this yet — noted for later, not built now).
- `auth/statusChanged` → `{"status":"authenticated"}` or `{"status":"error","message":...}`

Token/credential storage stays inside each source's own config directory —
the front never sees credentials, keeping it fully source-agnostic.

---

## 4. Core RPC catalog

Namespaces: `catalog.*`, `playback.*`, `feedback.*`, `auth.*`, lifecycle
`initialize` / `shutdown`.

**Shared shapes** (defined once — now in `protocol/schema/*.yaml`, see the
protocol 1.1 note above; this section otherwise still describes the original
per-field design accurately):
```
Track:  {id, title, artists:[{id,name}], album:{id,title,coverUrl?}, durationMs,
         coverUrl?, liked?, explicit?}
Playlist: {id, title, description?, coverUrl?, trackCount, kind:"playlist"|"liked"|"radioStation"}
PlaybackState: {state:"idle"|"playing"|"paused"|"buffering"|"stopped",
                trackId?, positionMs?, durationMs?, liked?}
StreamDescriptor: {kind:"url", url, mimeType, headers?}
```

**Browse:** `catalog.listPlaylists`, `catalog.listTracks {playlistId, cursor?} → {tracks, nextCursor?}`,
`catalog.listLiked`, `catalog.startRadio {seed?} → {stationId, initialTracks}`.
Cursor is an opaque string the front just echoes back; no structure implied.

**Playback — meaning depends on the active capability:**
- `providesStream` sources: front owns the whole queue; it never sends
  next/previous/seek/setVolume to the source. It calls `playback.play
  {trackId}`, gets a fast ack, then a `track/streamReady` notification (see
  race-handling below) and plays locally. It still reports
  `feedback.trackStarted/Finished/Skipped` for radio attribution (mirrors
  today's `rotor_station_feedback_track_*` calls).
- `selfPlayback` sources: front forwards `playback.pause/resume/seek
  {positionMs}/setVolume {volume}/next/previous` as real commands and
  mirrors state purely from `state/changed` notifications.

**Play → stream resolution, and the race it creates:** `playback.play`
returns a fast synchronous `{"accepted":true}` rather than blocking on the
network round-trip. The actual stream arrives later via a `track/streamReady`
notification whose params carry back the originating request's `id` as a
plain `requestId` field. The front's playback engine keeps one
`latestRequestId`; every `playback.play` overwrites it; a `track/streamReady`
is only acted on if `requestId === latestRequestId` — this self-corrects even
if resolutions complete out of order (user hits "next" twice fast). A
`playback.cancel {requestId}` (fire-and-forget) is sent whenever a request
gets superseded, so the source can drop in-flight work. Apply a ~10s
per-play timeout on the front side; on timeout, discard the pending
correlation and show an error toast (reusing the existing toast pattern
from `tui.py`).

**Feedback:** `feedback.like {trackId}`, `feedback.dislike {trackId}`,
`feedback.trackStarted/trackFinished {trackId, playedMs}`,
`feedback.skip {trackId, playedMs}` — this is exactly today's rotor
feedback calls, generalized.

**Notifications:** `state/changed`, `radio/tracksAdded {stationId, tracks}`,
`auth/prompt`, `auth/statusChanged`, `error {code,message,data}`.

**Reverse RPC:** none in v1 — sources never send requests to the front, only
responses/notifications. Revisit only if a concrete need appears (e.g. a
captcha prompt requiring the front to collect input mid-flow beyond what
`auth.submit` already covers).

### 4b. Download capability (per this session's decision)

```jsonc
"download": true   // in capabilities
```

```jsonc
--> {"jsonrpc":"2.0","id":7,"method":"catalog.downloadTrack",
     "params":{"trackId":"51672522","destDir":"/home/vlad/Music"}}
<-- {"jsonrpc":"2.0","id":7,"result":{"path":"/home/vlad/Music/Aurolab - Victory.mp3"}}
```

- Front supplies the destination folder; the source resolves the stream
  itself, downloads, tags the file (reusing today's `downloader.py` /
  mutagen logic), and returns the **absolute saved path**.
- This is a normal request/response, not routed through the
  `track/streamReady` machinery — it's an explicit one-shot user action
  (the `s` key in the TUI, or a future CLI), not part of the playback
  queue, so no correlation/race handling is needed for it.
- Needs a longer per-method timeout than browse calls (file download, not
  metadata) — front's RPC client should support per-method timeout
  overrides; default ~5s for browse/auth, ~60s for `catalog.downloadTrack`.
- Playlist download stays a **front-side loop**: call `catalog.downloadTrack`
  once per track in the playlist. No separate `catalog.downloadPlaylist`
  RPC method — keeps the protocol surface smaller, and per-track errors are
  handled the same way `download_tracks()` already does today.
- If a source has `download: false`, the front hides/disables the save
  action for that source's tracks instead of calling a method it declared
  it doesn't support (capability errors in the 1100–1199 range exist as a
  server-side backstop, not the primary UX guard).

---

## 5. Process lifecycle management (front side)

- Spawn each source via `asyncio.create_subprocess_exec(*argv, stdin=PIPE,
  stdout=PIPE, stderr=PIPE)`.
- One reader task per source: read lines, `json.loads`, dispatch — `id` +
  (`result`|`error`) resolves a pending `dict[id, asyncio.Future]`; `method`
  with no `id` goes to a notification event bus.
- Single writer path per source guarded by an `asyncio.Lock` so concurrent
  coroutines never interleave partial lines.
- Per-request timeouts (default ~5s browse/auth, per-method override for
  `catalog.downloadTrack` per §4b). On timeout: drop the pending Future;
  a late response arriving afterward is logged and discarded.
- Crash handling: reader sees EOF/exit → source marked "unavailable" in the
  UI, in-flight requests to it fail, last ~50 lines of its stderr kept for
  display; auto-restart with backoff (2s/5s/10s, max 3 attempts) before
  requiring a manual reconnect.
- Clean shutdown: send `shutdown` request (short timeout) → close stdin
  (source's read loop hits EOF and exits naturally) → SIGTERM after a grace
  period if still alive → SIGKILL as last resort.

---

## 6. Repo layout & migration

Only two top-level component folders: `fronts/` and `backends/`. Protocol
prose lives in `docs/protocol.md` (this repo's `docs/` folder, not a separate
top-level `protocol/`); the shared RPC transport library nests inside
`backends/` since backends are what run a server loop with it.

```
cloudmus/
  docs/
    architecture-plan.md          # this file
    protocol.md                   # everything in §1–§4 above, written out formally (moved here, not
                                   # a separate top-level protocol/ folder)

  backends/
    py-rpc-common/
      rpc_common/transport.py     # NDJSON reader/writer over asyncio streams, both sides
      rpc_common/jsonrpc.py       # envelope helpers, id generation
      rpc_common/errors.py        # error code constants from §1
      rpc_common/models.py        # Track/Playlist/PlaybackState/StreamDescriptor (shared dataclasses)
      rpc_common/testing/         # conformance suite + fake-source/fake-front harnesses (§8),
                                   # including a synthetic selfPlayback fixture (no real backend uses it)
      schema/*.json                # JSON Schema per message shape (Track, Playlist, capabilities, ...)
      examples/*.ndjson             # canned transcripts: handshake, play sequence, wave session,
                                   # device-code auth, download — used by the conformance suite (§8)

    yandex-music/
      manifest.json                 # {"id":"yandex-music","argv":[...],"protocolVersion":"1.0",...}
      cloudmus_backend_yandex/
        __main__.py                 # stdio server loop; installs the stdout-purity guard from §1
        auth.py                     # from ym_player/auth.py — device flow now emits auth/prompt +
                                     # auth/statusChanged instead of print()
        client.py                   # from ym_player/client.py, essentially unchanged
        catalog.py                  # catalog.listPlaylists/listTracks/listLiked, built on resolver.py's
                                     # existing playlist/track fetching logic
        radio.py                    # rotor/wave logic extracted from ym_player/player.py's start_wave/
                                     # _fetch_wave_batch/_advance: implements catalog.startRadio +
                                     # feedback.trackStarted/Finished/skip
        playback.py                 # playback.play: get_download_info(get_direct_links=True) →
                                     # StreamDescriptor via track/streamReady; no mpv import here at all
        download.py                 # catalog.downloadTrack — thin wrapper around today's downloader.py
        config.py                   # token path becomes ~/.config/cloudmus/backends/yandex-music/config.json
      # capabilities: providesStream=true, selfPlayback=false,
      #   browse.{playlists,likedTracks,radio}=true, browse.search=false (not built today),
      #   feedback.{like,dislike,skip}=true, download=true, auth.required=true, auth.flow="deviceCode"

    local-folder/
      manifest.json
      cloudmus_backend_local/
        __main__.py, scanner.py, catalog.py, playback.py, config.py
        # Plays a local music directory tree instead of a remote service: subdirectories of the
        # configured root become individual playlists; if the root only contains audio files
        # directly (no subfolders), the whole root is exposed as a single playlist. providesStream=true
        # (StreamDescriptor is a file:// URL) — front's existing mpv playback path handles it unchanged.
        # No auth, no download, no feedback/radio.

  fronts/tui/
    cloudmus_tui/
      __main__.py                 # `cloudmus` entrypoint
      discovery.py                 # scans ~/.config/cloudmus/backends.d/*.json (manifest dir);
                                   # dev-mode env var also allows repo-relative backends/*/manifest.json
      rpc_client.py                 # per-backend subprocess + NDJSON client, reader task, pending-id map (§5)
      source_manager.py             # owns N rpc_client instances, capability aggregation, crash/restart
      playback_engine.py            # generalized ym_player/player.py: mpv queue/play/pause/next/prev/
                                   # seek/volume/eof-watcher — used only for providesStream backends,
                                   # driven by track/streamReady + requestId correlation (§4)
      app.py                        # generalized ym_player/tui.py: no longer imports yandex_music;
                                   # sidebar aggregates catalog.listPlaylists across all backends
                                   # (tagged by sourceId), status bar mirrors playback_engine OR raw
                                   # state/changed depending on the active backend's capability
```

**What moves where, concretely (today's files → new home):**
| Today | Becomes |
|---|---|
| `ym_player/config.py`, `client.py` | `backends/yandex-music/cloudmus_backend_yandex/config.py`, `client.py` — same logic, new home |
| `ym_player/auth.py` | same file, `on_code` callback becomes `auth/prompt` notification emission instead of `print()` |
| `ym_player/resolver.py` | folded into `backends/yandex-music/cloudmus_backend_yandex/catalog.py` |
| `ym_player/downloader.py` | `backends/yandex-music/cloudmus_backend_yandex/download.py`, called by `catalog.downloadTrack` |
| `ym_player/player.py` | split: mpv queue/eof-watcher logic → `fronts/tui/cloudmus_tui/playback_engine.py`; rotor/wave logic → `backends/yandex-music/cloudmus_backend_yandex/radio.py` |
| `ym_player/tui.py` | `fronts/tui/cloudmus_tui/app.py`, rewritten against `source_manager`/`rpc_client` instead of `yandex_music` |
| `ym_player/cli.py` | `ym auth`/`ym wave` become each backend's own standalone debug CLI mode (e.g. `python -m cloudmus_backend_yandex --cli-wave`), reusing the same modules in-process without spawning RPC — keeps the "debug without the TUI" workflow from this session alive per-backend |
| (none — new) | `backends/local-folder/*` built from scratch |

**Deferred / explicitly not decided now** (small, non-blocking, flag if it
matters before implementation):
- Exact per-backend data directory convention beyond the example above.
- `catalog.resolveUrl` (paste a music.yandex.ru URL/ID directly) — not in
  v1 protocol; today's URL-parsing in `resolver.py` only needs to survive
  inside the Yandex backend's own debug CLI, not as a generic front feature,
  unless you want it later.
- oauthRedirect's local-loopback-listener ownership — no concrete backend
  needs it yet.

---

## 7. Testing & verification strategy

- **Front-side, no live account**: `rpc_common/testing/fixtures.py` provides
  canned request→response mappings with configurable delay/reordering to
  unit-test `source_manager.py` / `playback_engine.py` — including a fixture
  that specifically reorders `track/streamReady` notifications relative to
  rapid `playback.play` calls, proving the `requestId`-discard logic (§4)
  actually works, and a synthetic `selfPlayback` fixture that exercises the
  branch neither real v1 backend uses.
- **Backend-side, no live front**: a "fake front" harness that replays
  `backends/py-rpc-common/examples/*.ndjson` into a backend's stdin and
  asserts on stdout (golden-file/transcript replay). For `yandex-music`,
  record real API responses once via a cassette library (`vcr.py`/`responses`)
  so CI never needs a live account. For `local-folder`, unit-test `scanner.py`
  directly against a small fixture directory tree covering both the
  "subfolders = playlists" and "flat files = single playlist" cases.
- **Conformance suite** (`backends/py-rpc-common/rpc_common/testing/conformance.py`):
  runnable against *any* backend — spawn it, `initialize`, validate the
  result against `backends/py-rpc-common/schema/`, check capability-flag
  internal consistency (`providesStream`/`selfPlayback` not both true), then
  exercise every method implied by a true capability flag and validate result
  shapes. Run against both `yandex-music` and `local-folder` in CI — this is
  the real mechanism keeping future third-party backends honest.
- **Manual smoke checklist** (run once the migration lands):
  1. No manifests present → front shows an empty/graceful state.
  2. Both manifests present, relaunch → both backends spawn, sidebar shows both.
  3. Trigger Yandex auth → device code shown, completes, playlists populate.
  4. Play a Yandex track → `track/streamReady` → local mpv plays; pause/seek/volume all work locally.
  5. Start My Wave → auto-advance triggers `feedback.trackFinished` + `radio/tracksAdded`.
  6. Save current Yandex track (`s`) → `catalog.downloadTrack` → file appears at returned path.
  7. Point `local-folder` at a directory with subfolders → each subfolder appears as a playlist; play a track from it.
  8. Point `local-folder` at a directory with only loose files → single playlist, all tracks listed.
  9. `kill -9` the Yandex subprocess mid-session → front marks it unavailable without crashing; local-folder keeps working.
  10. Quit → both subprocesses exit cleanly, no orphans (`ps` check).
  11. Pipe a backend's raw stdout through a strict per-line `json.loads` validator to catch stdout-purity regressions early.

---

## Suggested implementation order

1. `docs/protocol.md` written out fully from §1–§4b (moved here, no separate top-level `protocol/` folder).
2. `backends/py-rpc-common` (transport, jsonrpc envelope, error constants, shared models, schema, conformance harness, fixtures).
3. `backends/local-folder` first (small, proves the `providesStream` path end-to-end, no external API dependency) —
   validates the transport/lifecycle plumbing before touching real Yandex code.
4. `backends/yandex-music`: migrate `client.py`/`auth.py` mostly as-is, rebuild `catalog.py`/`radio.py`/
   `playback.py`/`download.py` on top of the existing `yandex_music` calls already proven in this repo.
5. `fronts/tui`: `rpc_client.py` + `source_manager.py` + `discovery.py`, then `playback_engine.py`
   (port of today's `player.py` mpv logic), then `app.py` (port of today's `tui.py`).
6. Wire the conformance suite into both backends; run the manual smoke checklist end-to-end.
7. Retire the old flat `ym_player/` package once the new tree is verified working.
