# Backends in Go (plan, not started)

Status: evaluated, not started. Before starting, check whether the gap
on Windows is still noticeable after the fixes of September 2026 (shared
HTTP session, precompiled bytecode, no console per backend).

## Why

- **Start-up.** A Python backend takes 1.5–2.5 s from spawn to answering
  `initialize` on Windows (debug log, September 2026). A Go binary starts
  in tens of milliseconds. Precompiled `.pyc` files already cover part of
  this gap.
- **Size and memory.**
  - No embedded Python in the installer (currently 70 MB in total).
  - No three interpreters at 40–80 MB each.
- **Not a speed-up for track loading.** That was dominated by the
  network, and the per-request connection setup has already been fixed.
  Go's `http.Client` would have kept connections alive by default, though.

## What the architecture already allows

A backend is any process speaking JSON-RPC 2.0 over NDJSON on
stdin/stdout, started from the `argv` in its `manifest.json`.
`rpc_common.testing.conformance` drives a backend over the wire, whatever
its language, so a Go backend is checked by the same suite.

## Work needed in every case

1. **Go target in `protocol/codegen/`**: model structs and the method
   table from `methods.yaml`/`schema/*.yaml`, like the Python and C++
   targets. Generated code stays out of git.
2. **Go counterpart of `backends/py-rpc-common/`**: the NDJSON transport,
   the JSON-RPC server, error codes and data, settings, downloads, and
   platform paths. Straightforward in Go.

## Per backend

### local-folder: easy, the pilot
Tag reading with `github.com/dhowden/tag`, plus walking folders and
cover art. Use it to prove the codegen and runtime.

### Yandex Music: feasible, about a week
- **Existing Go clients** exist, but they look small or unevenly
  maintained, so we would not depend on them:
  - [ndrewnee/go-yamusic](https://github.com/ndrewnee/go-yamusic)
  - [Igorprostoff/go-ya-music](https://pkg.go.dev/github.com/Igorprostoff/go-ya-music)
    (a port of MarshalX's Python library, which we use today)
  - [Shurik12/go-yamusic-api](https://pkg.go.dev/github.com/Shurik12/go-yamusic-api)
  - [optclblast/go-yandex-music-api](https://pkg.go.dev/github.com/optclblast/go-yandex-music-api)
- **Our own client** needs only the subset we use. The API is plain
  HTTP+JSON:
  - device-code OAuth;
  - `account/status`;
  - playlists, including editing;
  - likes and dislikes;
  - `tracks`;
  - `download-info` plus signing the `get-mp3` link (MD5 with a salt);
  - `rotor` for radio;
  - `feed`.
- **Reference:** MarshalX's `yandex-music` Python library.
- **Risk:** the API is undocumented and changes occasionally. That is the
  same risk the Python library carries.

### YouTube Music: don't write our own stream extraction
- **Catalog** (the `ytmusicapi` part): InnerTube's deeply nested JSON and
  thousands of lines of parsers that break when YouTube changes its
  layout. There is no mature Go port. The subset we need could be written
  in 1–2 weeks, but then we maintain the fixes ourselves instead of the
  `ytmusicapi` community.
- **Stream URLs** (the yt-dlp part): an ongoing arms race.
  - [kkdai/youtube](https://github.com/kkdai/youtube) deciphers
    signatures by running `base.js` in goja and breaks whenever YouTube
    changes its player.
  - YouTube now also demands PO tokens and JS challenges. yt-dlp keeps
    up only thanks to its large community, and now needs an external JS
    runtime itself.
- **Realistic option, a hybrid:** the catalog in Go, streams through the
  standalone yt-dlp binary (a single executable, no Python install
  needed). Otherwise keep YouTube on Python.

## Order

1. Go codegen target and shared runtime.
2. local-folder in Go (the pilot); passes the conformance suite.
3. Yandex Music in Go.
4. YouTube Music: stay on Python, or the hybrid above.

Python and Go backends can ship side by side while migrating, since each
backend is a separate process with its own manifest.
