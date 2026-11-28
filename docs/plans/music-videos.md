# Music videos (plan, not started)

Status: not started; wanted. Where a source has a video for a track (a music
video on YouTube Music, a clip on Yandex Music), CloudMus shows it while the
track plays. Music stays the point: no screen means no video, and nothing is
lost without one.

Anything marked *unverified* hasn't been tried in CloudMus.

## Where things stand

- mpv is a video player first. The core turns video off everywhere it makes
  an mpv (`vid=no`: `Playback::AudioPlayer`, `CuePlayer`, `AudioFileProbe`).
- **libmpv and video, by platform** (see [`mpv.md`](mpv.md) for where each
  comes from):
  - Debian 11's `libmpv-dev` (Linux) and shinchiro's Windows build have
    video.
  - The Android AAR (`dev.jdtech.mpv:libmpv`) is built for a video player,
    Findroid, so it most likely has video and MediaCodec decoding
    (*unverified*).
  - The macOS build is audio-only on purpose: `-Dgl=disabled`, libplacebo
    with no GPU API (`packaging/macos/build-deps.sh`). It needs OpenGL back.
- **The protocol** has one stream per track: `stream_descriptor.yaml` is a
  `url` and a `mimeType`, with `kind: [url]` reserved for other kinds.
- **YouTube Music**: track ids are videoIds, and `playback.py` asks yt-dlp for
  `bestaudio/best`.
- **Yandex Music**: some tracks have clips in the official app. The
  unofficial API seems to link a track to its clips, but how a clip's stream
  is had, and in what format (HLS?), is *unverified*.

## A track's video isn't always the same recording

On YouTube Music a song (an "art track", ATV: the cover as a still) and its
music video (OMV) are usually two videoIds. The video often runs longer, with
an intro, a skit or a different edit. So the video can't be laid under the
song's sound: the two would drift apart. Showing the video means playing the
video's own sound with it. That's how the official app's song/video switch
works, too: it changes to the other videoId. ytmusicapi gives each item's
`videoType` (`MUSIC_VIDEO_TYPE_ATV`, `_OMV`, `_UGC`) and, in the watch
playlist, the counterpart.

Where the video is the same recording as the track (an OMV played as is, or a
clip whose source says so), it can simply replace the audio stream.

## Protocol

Through the protocol and capabilities, never `if backend == …` in a front
(`AGENTS.md`). Additive and optional (a MINOR version), roughly:

- A capability, `playback.video`, say.
- On a track: whether a video exists, and whether it's the same recording or
  one of its own (with its own duration). Maybe the video's own track id, for
  the second case.
- A way to ask for it: `playback.play` with a "with video" flag, or a method
  of its own.
- A stream with video: YouTube serves video and audio as separate DASH
  streams, so the descriptor needs a video URL beside the audio one (mpv
  takes the audio as `audio-files`), or a kind for an HLS/DASH manifest.
  Headers, expiry and refresh as for audio streams today.

## Backends

- **YouTube Music**: read `videoType` and the counterpart, and ask yt-dlp for
  a video format plus `bestaudio` when video is wanted, within a resolution
  the front asks for (traffic, the phone's battery).
- **Yandex Music**: find out how clips are listed and streamed first
  (*unverified*), then the same.

## Fronts

- **Desktop** (Qt Widgets): mpv's render API (`mpv_render_context`, OpenGL)
  in a `QOpenGLWidget` on the now-playing view. When the window is hidden or
  the view isn't shown, turn video off (`vid=no`) so it isn't decoded.
- **Android**: mpv runs in the `:playback` service's process, the UI in
  another (`PlaybackService`, bound with `ACTION_VIEWS`), so the render API,
  which needs one process, doesn't fit. A `Surface` can cross processes
  through Binder: the UI's `SurfaceView` hands its `Surface` to the service,
  and mpv draws into it (`wid`, with `vo=gpu` or `mediacodec_embed`). When
  the surface goes (app minimized, screen off), video goes off at once.
  *Unverified.*
- **Android Auto and head units**: no video while driving (Android Auto
  doesn't allow it); music only, as now.
- **TUI**: none.

## Switching

- Turning video on mid-track: reload with the video stream at the same
  position, where it's the same recording; otherwise offer the video as a
  track of its own (from the start) rather than switching silently.
- Turning it off: `vid=no` keeps the sound playing without a reload.
- The setting "show videos when there are" (off by default on mobile
  networks), and a button on the now-playing view where a track has one.

## Order

1. YouTube Music in the protocol and backend; the desktop shows it.
2. Android: the Surface across processes, video off in the background.
3. macOS: the build with OpenGL.
4. Yandex Music, once its clips are figured out.
