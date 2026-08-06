# cloudmus-backend-youtube-music

CloudMus backend for YouTube Music, built on
[`ytmusicapi`](https://ytmusicapi.readthedocs.io/) for catalog/library
access and [`yt-dlp`](https://github.com/yt-dlp/yt-dlp) to resolve a
playable audio stream URL per track (`ytmusicapi` itself only gives back
metadata, not a stream — see `cloudmus_backend_ytmusic/playback.py`).

Package: `cloudmus_backend_ytmusic`. Manifest: `manifest.json` (auto-
discovered by fronts — see the top-level repo docs on backend discovery).

## Capabilities (MVP scope)

```
playback: providesStream (yt-dlp resolves the stream URL; no self-playback)
browse:   playlists, likedTracks   (radio and search are NOT implemented yet)
feedback: like, dislike            (skip is not implemented)
download: not implemented
auth:     required, flow "usernamePassword" (see below — NOT deviceCode)
```

Not implemented: `catalog.startRadio`, `catalog.downloadTrack`,
`browse.search` (the protocol doesn't even have a `catalog.search` method
yet — see `docs/protocol.md`).

## Authentication — read this before anything else

This backend currently authenticates by **pasting browser request headers**,
not the device-code flow you might expect from `capabilities.auth.flow`
being absent a more obvious name. This is a deliberate, temporary
workaround:

- Device-code OAuth (`ytmusicapi`'s `OAuthCredentials`, see `auth.py`'s
  `DeviceOAuthSession`) **does still log you in successfully** — the token
  is issued, passes Google's own `tokeninfo` introspection with the correct
  `client_id`/scope. But **every actual data call made with that token then
  fails** with a generic `HTTP 400: Request contains an invalid argument`
  (`search`, `get_home`, `get_library_playlists`, `get_liked_songs` — all of
  them). Anonymous (unauthenticated) calls work fine, isolating the problem
  to the OAuth-authenticated request path specifically.
- This is a **confirmed, currently-active upstream bug**, not a CloudMus or
  configuration problem: [`sigma67/ytmusicapi#676`](https://github.com/sigma67/ytmusicapi/issues/676)
  reports the exact same symptom and is labeled `yt-update` by the
  maintainers — their convention for "a server-side change on Google's end
  broke this." No fix or ETA exists as of this writing.
- So `CAPABILITIES["auth"]["flow"]` is set to `"usernamePassword"` instead,
  repurposed (via a `multiline: true` hint on the one `headers` field — see
  `docs/protocol.md` §10.2) to collect a pasted blob of browser request
  headers rather than an actual username/password pair. `DeviceOAuthSession`
  and all its OAuth machinery are still in `auth.py`, just unused —
  reverting once upstream is fixed is: flip the flow string back to
  `"deviceCode"` and instantiate `DeviceOAuthSession()` instead of
  `BrowserAuthSession()` in `server.py`'s `build_server()`.

### Setting up browser-header auth

1. Open [music.youtube.com](https://music.youtube.com) in any browser,
   signed in to the Google account you want CloudMus to use.
2. Open DevTools (F12) → **Network** tab.
3. Click any request to `music.youtube.com` (reload the page if the list is
   empty).
4. Copy its request headers as raw text:
   - **Firefox**: right-click the request → *Copy Value* → *Copy Request
     Headers*.
   - **Chrome**: open the request's *Headers* panel and copy the raw
     request headers block.
5. In the CloudMus front, select the YouTube Music source — a form with a
   large paste box appears (the front auto-triggers `auth.start` on
   connect, no button needed). Paste the copied text in and submit.

The backend hands this straight to `ytmusicapi.auth.browser.setup_browser()`
(no custom parsing of its own — see `auth.py`'s `BrowserAuthSession.submit`),
which validates that a `cookie` and `x-goog-authuser` header are present,
and that the cookie contains a `__Secure-3PAPISID` value — if either check
fails, `auth/statusChanged` reports the exact missing piece and the front
shows it as an error with a **Retry** button; just copy the headers again
(make sure you copied from a request while actually signed in) and
resubmit.

**Caveat**: unlike OAuth, this cookie has no refresh mechanism. When it
eventually expires (signing out elsewhere, Google forcing re-auth, etc.)
there's no transparent renewal — calls will start failing and you'll need
to repeat the steps above.

### Config files

All under `~/.config/cloudmus/backends/youtube-music/`, each created only
when relevant:

| File | Written by | Purpose |
|---|---|---|
| `browser_headers.json` | `setup_browser()`, on a successful `auth.submit` | **The active credential.** `client.py` passes its path straight to `YTMusic(auth=...)`. Delete it (or call `auth.logout`) to force re-authentication. |
| `oauth_token.json` | `ytmusicapi`'s own `RefreshingToken`, during a (currently non-functional) device-code login | Dormant leftover from the OAuth path; harmless to have around, `client.py` only falls back to it if `browser_headers.json` doesn't exist. |
| `config.json` | You, by hand | `{"clientId": "...", "clientSecret": "..."}` — only needed to revive the dormant OAuth path (a Google Cloud OAuth client, **"TVs and Limited Input devices"** type, YouTube Data API v3 enabled). Not used by the current browser-header flow at all. |

All files are written `chmod 0600` (contain session cookies / tokens).

## Dependencies

- `ytmusicapi>=1.8,<2` — catalog/library access, both auth mechanisms.
- `yt-dlp>=2024.1` — the only thing `ytmusicapi` can't do: resolving a
  playable audio stream URL for a track (`playback.py`).
- `cloudmus-rpc-common` — shared NDJSON/JSON-RPC scaffolding (see
  `backends/py-rpc-common`).

## Running tests

```
cd backends/youtube-music
python -m pytest tests/ -q
```

`tests/test_catalog.py`/`test_playback.py` are pure unit tests (no
network). `tests/test_auth.py` exercises `BrowserAuthSession.submit()`
against fake header text (also no network — `setup_browser()` itself is
pure parsing; only actual catalog calls or `YTMusic()`'s SAPISID check hit
anything resembling validation, and the latter isn't invoked by `submit()`
at all).

Conformance suite (spawns the real backend, drives it through the wire
protocol, validates against `protocol/schema/*.yaml`):

```
python -m rpc_common.testing.conformance python3 -m cloudmus_backend_ytmusic
```

This passes without any real credentials — it only exercises
`catalog.listPlaylists` when `auth.getStatus` reports `authenticated`.
