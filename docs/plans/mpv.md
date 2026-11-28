# A libmpv of our own (plan, not started)

Status: not started. Today every platform takes libmpv as someone else built
it. A build of our own, from one set of patches, would give smooth fades
without clicks, the music analyzed on Android, OpenSL ES's audio session, and
the ground for an equalizer, effects, visualizations and crossfade.

Anything marked *unverified* hasn't been tried in CloudMus.

## Where libmpv comes from now

| Platform | Source | Filters (`libavfilter`) |
|---|---|---|
| Android | prebuilt AAR `dev.jdtech.mpv:libmpv`, unpacked by `build-android.sh` (`CLOUDMUS_MPV_ANDROID_DIR` in `fronts/mobile/CMakeLists.txt`) | cut down to ~125 KB: no `afade`, `aeval`, `volume`, `astats` |
| Linux (AppImage) | Debian 11's `libmpv-dev` (`packaging/appimage/Dockerfile`) | whole |
| Windows | shinchiro's `mpv-dev` archive, mirrored in our `build-deps` release (`packaging/windows/Dockerfile`) | whole |
| macOS | built from pinned sources, LGPL only (`packaging/macos/build-deps.sh`) | whole |
| Dev builds | the system's libmpv | whole, usually |

The AAR also lacks `ao-volume`: `ao_audiotrack` never calls
`AudioTrack.setVolume`. mpv's own volume (`volume`, `volume-gain`) is applied
in `ao.c` (`process_plane`) at one gain for a whole chunk the output pulls:
75–150 ms for AudioTrack, 250 ms by default for OpenSL ES.

## The patches

One set, kept in the repository (`packaging/mpv-patches/`, say) and applied by
every build below. Each one is small and lives in mpv's sources, not ours.

1. **The gain ramped across a chunk** (`ao.c`, `process_plane`): from the last
   value to the new one, per sample, instead of one gain for the whole chunk.
   The existing `AudioPlayer::setEdges()` then gives a smooth fade on any
   output, and only its defaults need turning on. This is what fixes the click
   on pause and track change (`docs/android.md`, "Open issue: a click on pause
   and track change"), and what fades and crossfade are built on.
2. **AudioTrack quieted before it stops** (`ao_audiotrack.c`, `stop()`): call
   `AudioTrack.setVolume(0)` and wait for Android's ramp (a few ms) before
   `pause()`/`flush()`. That fixes the click at its source, without
   `setEdges()`. Either this or patch 1 is enough for the click.
3. **OpenSL ES's audio session** (`ao_opensles.c`). Android's OpenSL ES gives
   each player an AudioTrack of its own, and that one gets a session from
   AudioFlinger: it shows in `dumpsys media.audio_flinger`, but there's no
   setting to put the player in our session (Android's OpenSL ES keys are only
   the stream type, the performance mode and the recording preset). It can be
   read, on API 24 and later: after `Realize`,
   `SLAndroidConfigurationItf::AcquireJavaProxy(SL_ANDROID_JAVA_PROXY_ROUTING)`
   gives a Java `AudioTrack` around the player's own, and its
   `getAudioSessionId()` is the session. mpv makes it known (a property, say),
   and `CorePlayer` sends `CLOSE_AUDIO_EFFECT_CONTROL_SESSION` for the old
   session and `OPEN_…` for the new one each time the output is reopened,
   since every reopen makes a new player. *Unverified.* See
   `docs/android.md`, "System equalizers", for how sessions work with
   AudioTrack.
4. **A tap on the PCM** (`ao.c`): the samples as they go to the output, handed
   to a callback, for the core to compute a spectrum (kissfft or the like).
   The readings have to be shifted by the output's delay (`ao_get_delay`), or
   the picture runs ahead of the sound.

On Android the same build needs a whole `libavfilter`, at least `asplit`,
`anull`, `pan`, `lowpass`, `join`, `asetnsamples`, `astats`, `ametadata` and
`anullsink`, for the level analyzer (`AudioPlayer::setLevelsEnabled()`). With
the AAR, `af add` fails ("couldn't add the level analyzer" in the log), and
the music the views move with (`ViewModel::PulseScreen`: the About page's logo
and clouds) is made up by `ViewModel::ImitatedMusic`, a kick at a tempo of its
own. With the analyzer working, drop the imitation in `App::MobileCore` (the
`Q_OS_ANDROID` block) and the pulse follows the real music, as on the desktop.

## What it makes possible

- **Equalizer and effects** through lavfi filters in mpv's `af` chain:
  `equalizer`, `superequalizer`, `firequalizer`, `bass`/`treble`,
  `acompressor`, `dynaudnorm`/`loudnorm`, `extrastereo`, `crystalizer`, `afir`
  (convolution: headphone correction from AutoEQ, say). They are added and
  removed with `af add`/`af remove`; some (`equalizer`, `firequalizer`,
  `volume`) take new parameters through `af-command` without the chain being
  rebuilt, so moving a slider doesn't click. The code would live in `core`
  and work with any output, OpenSL ES included. The desktop's libmpv has these
  filters already, so the desktop doesn't wait for the patches.
- **Visualizations**: loudness from the level analyzer (patch-free, needs the
  whole `libavfilter`), a spectrum from the tap (patch 4).
- **Fades** on pause, seek and track change: patch 1 plus `setEdges()`.
- **Crossfade.** mpv decodes one file at a time, and `acrossfade` in
  `--lavfi-complex` wants both inputs known up front, which doesn't suit
  streamed URLs. What's realistic: two mpv instances taking turns (`CuePlayer`
  shows a second one is possible), one fading out while the other fades in,
  on patch 1. Both AudioTracks can share our session, so an equalizer like
  Poweramp's covers both; with OpenSL ES each would have a session of its own.
  Preloading the next track, choosing when to start, and keeping albums
  gapless are the `PlaybackController`'s to do; no build gives them.

Without a build of our own, Android offers some of this on our AudioTrack
session: `android.media.audiofx` effects (`Equalizer`, `BassBoost`,
`DynamicsProcessing` on API 28+ with a multiband EQ, compressor and limiter)
and `Visualizer` (waveform and FFT, but it needs `RECORD_AUDIO`). Android only,
and not with OpenSL ES.

## Builds

- **macOS**: add the patches to `build_mpv` in `packaging/macos/build-deps.sh`.
  The easiest start.
- **Android**: build FFmpeg and mpv with the NDK for each ABI the APK ships,
  replacing the AAR in `build-android.sh`. The APK grows by a few MB.
- **Linux**: build mpv (and FFmpeg, if Debian 11's is too old for what's
  wanted) in the AppImage's Docker image instead of `libmpv-dev`.
- **Windows**: a cross-build of our own (shinchiro's mpv-winbuild-cmake, or
  MinGW in the existing Docker image) instead of the downloaded archive.

Dev builds link the system's libmpv, which has none of the patches. `core`
checks for each patch (by a property it adds, say) and does without, as
today, when it's missing.

## License

The app is MIT and bundles LGPL code only (see `build_ffmpeg` in
`packaging/macos/build-deps.sh`): mpv with `-Dgpl=false`, FFmpeg without
`--enable-gpl`, on every platform. Whether shinchiro's Windows archive is a
GPL build is *unverified*; a build of our own settles it either way.

## Order

1. macOS: patches 1 and 4 where the build already exists.
2. Android: the whole build, patches 1–4, the level analyzer.
3. Linux and Windows.
4. Then the features: equalizer and effects, visualizations, crossfade.
