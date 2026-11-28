# cloudmus-backend-youtube-music

CloudMus backend for YouTube Music (package `cloudmus_backend_ytmusic`),
built on [`ytmusicapi`](https://ytmusicapi.readthedocs.io/) for the
catalog and library, and [`yt-dlp`](https://github.com/yt-dlp/yt-dlp) to
resolve stream URLs.

## What it supports

- Library, liked tracks, playlists (editable), like/dislike.
- Radio from any track, and the account's personal mixes (My Supermix,
  My Mix 1–7, Discover, Replay…) played like My Wave — see `radio.py`.
  Listened tracks are added to the account's YouTube history, which feeds
  its recommendations; there's no other radio feedback on YouTube's side.
- Streams (`providesStream`, with preloading), audio-only, no `ffmpeg`. The
  **Quality** setting caps the bitrate (best ≈130 kbps, or up to 70 / 50
  kbps to save traffic); Opus is preferred to AAC at the same tier.
  Saving a track is the front's job (`downloadFromStream`): it downloads
  the stream and writes the tags and cover itself.
- No search yet.

## Authentication

The session is a browser's cookies. The backend offers two ways to get them
(`docs/protocol.md` §10), in this order:

1. **`browser`** — Google's sign-in page in the front's built-in browser; the
   front returns the cookies. The mobile front does this: tap **Sign in**.
2. **`form`** — pasting request headers copied from a browser, for a front
   with no built-in browser (desktop, TUI).

Device-code OAuth isn't offered: it logs in fine, but every data call made
with its token fails with `HTTP 400` — a confirmed upstream break,
[sigma67/ytmusicapi#676](https://github.com/sigma67/ytmusicapi/issues/676).

### Pasting request headers

1. Open [music.youtube.com](https://music.youtube.com) in a private window,
   signed in to the Google account you want CloudMus to use.
2. Open DevTools (F12) → **Network** tab.
3. Click any request to `music.youtube.com` (reload the page if the list is
   empty).
4. Copy its request headers as raw text:
   - **Firefox**: right-click the request → *Copy Value* → *Copy Request
     Headers*.
   - **Chrome**: right-click the request → *Copy* → *Copy as cURL (bash)*.
     The pasted `curl` command is converted to headers automatically.
5. Close the window without signing out. In CloudMus, open YouTube Music's
   page in **Settings**, paste the copied text into the sign-in box and
   submit.

If the headers lack a `cookie` with `__Secure-3PAPISID` or an
`x-goog-authuser` header, the app says which one is missing — copy them
again from a request made while signed in, and retry.

The cookie can't be refreshed: once it expires (signing out elsewhere,
Google forcing re-auth), sign in again.

### Config files

In `~/.config/cloudmus/backends/youtube-music/`:

| File | Purpose |
|---|---|
| `browser_headers.json` | The signed-in session, written `chmod 0600`. Delete it (or sign out) to force re-authentication. |
| `config.json` | `"settings"`: the stream quality chosen in **Settings**. |

## Dependencies

- `ytmusicapi>=1.8,<2` — catalog and library, and the session headers.
- `yt-dlp` (pinned in `pyproject.toml`, bumped by hand) — stream URLs (`playback.py`).
- `cloudmus-rpc-common` — shared transport (`backends/py-rpc-common`).

Tests and the conformance suite: see [`docs/development.md`](../../docs/development.md#tests).
