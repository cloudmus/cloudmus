# cloudmus-backend-spotify

CloudMus backend for Spotify, written in Rust on top of
[librespot](https://github.com/librespot-org/librespot), an open-source
Spotify client library. It signs in as a Spotify client, fetches and
decrypts the audio itself, and serves it to the front on `127.0.0.1`, so to
the front Spotify is an ordinary stream source (`providesStream`).

**Spotify Premium is required.** librespot plays nothing on a free account.

librespot is an unofficial client: Spotify doesn't endorse it, and using it
may be against Spotify's terms of use. Spotify's official Web API gives no
audio at all, so there is no official way for a desktop player to play
Spotify itself.

## What it supports

- Your playlists (those in your library, folders flattened) and Liked Songs.
- Like / unlike.
- Streams with preloading and seeking, Ogg Vorbis (or MP3 where that's all
  there is). The **Quality** setting picks 320, 160 or 96 kbps. Tracks not
  available in your country play one of Spotify's alternatives, as the
  official clients do.
- Saving tracks: the front saves the stream (`downloadFromStream`).
- Not yet: radio, search, editing playlists.

## Signing in

**Sign in** opens accounts.spotify.com in your browser. After you sign in
there, Spotify sends the browser back to `http://127.0.0.1:<port>/login`,
where the backend is listening. The browser has to run on the same computer
as CloudMus.

The backend keeps the *reusable credentials* Spotify gives a signed-in
session, not your password, and signs in with them on later starts.
**Sign out** deletes them. If nothing happens in the browser, **Start over**
gets a new link. After 5 minutes the sign-in gives up by itself.

No Spotify developer app is involved: the backend signs in with the client
id of Spotify's desktop app, as librespot does. The library is read through
Spotify's internal API (spclient), so the Web API's limits for developer
apps (5 users, Premium owner) don't apply.

## Config files

In `~/.config/cloudmus/backends/spotify/` (`%APPDATA%\cloudmus\backends\spotify\`
on Windows), written with mode 0600:

| File | Purpose |
|---|---|
| `credentials.json` | The saved sign-in. Delete it (or sign out) to sign in again. |
| `config.json` | `"settings"`: the stream quality chosen in **Settings**. |

## Proxies

An HTTP(S) proxy set for the source (`HTTPS_PROXY`/`HTTP_PROXY`/`ALL_PROXY`)
carries all of its traffic. librespot can't use SOCKS proxies: with one set,
the backend logs a warning and connects directly.

## Known issues

- If a saved account stops being Premium, librespot ends the process as soon
  as Spotify says so; the front then shows the source as unavailable. A new
  sign-in reports it properly.

## Building

Generate the protocol code first, then build with cargo:

```bash
python protocol/codegen/generate.py --lang rust
cd backends/spotify && cargo build --release
```

`manifest.json` points at `target/release/cloudmus-backend-spotify`, for
`CLOUDMUS_DEV_BACKENDS=1`. With no local Rust, the command for the pinned
`rust:alpine` image is in [`docs/development.md`](../../docs/development.md).

Tests: `cargo test`; the conformance suite:
`python -m rpc_common.testing.conformance backends/spotify/target/release/cloudmus-backend-spotify`.
