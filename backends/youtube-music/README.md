# cloudmus-backend-youtube-music

CloudMus backend for YouTube Music (package `cloudmus_backend_ytmusic`),
built on [`ytmusicapi`](https://ytmusicapi.readthedocs.io/) for the
catalog and library, and [`yt-dlp`](https://github.com/yt-dlp/yt-dlp) to
resolve stream URLs and download tracks.

## What it supports

- Library, liked tracks, playlists (editable), like/dislike.
- Radio from any track, and the account's personal mixes (My Supermix,
  My Mix 1–7, Discover, Replay…) played like My Wave — see `radio.py`.
  Listened tracks are added to the account's YouTube history, which feeds
  its recommendations; there's no other radio feedback on YouTube's side.
- Streams (`providesStream`, with preloading) and downloads (`bestaudio`,
  no `ffmpeg`; tags written for `.m4a`/`.opus` only).
- No search yet.

## Authentication

Sign-in is done by **pasting browser request headers**. Device-code OAuth
logs in fine, but every data call made with its token fails with
`HTTP 400` — a confirmed upstream break,
[sigma67/ytmusicapi#676](https://github.com/sigma67/ytmusicapi/issues/676).
The `usernamePassword` auth flow is repurposed to collect the headers; the
OAuth code is kept in `auth.py` for when that's fixed (see the comment on
`CAPABILITIES` in `server.py`).

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
5. In CloudMus, open YouTube Music's page in **Settings**, paste the copied
   text into the sign-in box and submit.

If the headers lack a `cookie` with `__Secure-3PAPISID` or an
`x-goog-authuser` header, the app says which one is missing — copy them
again from a request made while signed in, and retry.

The cookie can't be refreshed: once it expires (signing out elsewhere,
Google forcing re-auth), repeat the steps above.

### Config files

In `~/.config/cloudmus/backends/youtube-music/`, all written `chmod 0600`:

| File | Purpose |
|---|---|
| `browser_headers.json` | The active credential. Delete it (or sign out) to force re-authentication. |
| `oauth_token.json` | Leftover from the dormant OAuth path; used only if `browser_headers.json` doesn't exist. |
| `config.json` | `{"clientId": "...", "clientSecret": "..."}`, written by hand — only to revive the OAuth path (a Google Cloud OAuth client of the "TVs and Limited Input devices" type, YouTube Data API v3 enabled). |

## Dependencies

- `ytmusicapi>=1.8,<2` — catalog and library, both auth mechanisms.
- `yt-dlp>=2024.1` — stream URLs (`playback.py`) and downloads (`download.py`).
- `mutagen>=1.47` — best-effort tags on downloaded files.
- `cloudmus-rpc-common` — shared transport (`backends/py-rpc-common`).

Tests and the conformance suite: see [`docs/development.md`](../../docs/development.md#tests).
