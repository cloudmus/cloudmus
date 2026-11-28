# Android: battery use while playing in the background

An investigation of 2026-11-28 (commit `e2f67ec`): what CloudMus does while
music plays and the app is minimized, not swiped away. Nothing has been changed
yet; the ideas at the end are open work.

## In short

- The minimized UI costs next to nothing: its animations don't run (checked
  from one screen, see "Not verified").
- The playback service runs off the fastest processors.
- It still does work nobody sees: network checks, position reports, position
  saves.

## Measurements

Pixel 7, Android 17, a Yandex Music track playing, the phone on USB power. CPU
is a percentage of one processor; wakeups are voluntary context switches.
Screen off, a 90 s window:

| Process or thread | CPU | Wakeups/s |
|---|---|---|
| UI process | 0.00% | 0.4 |
| Playback service (`:playback`), all threads | 8.3% | 57 |
| — mpv's core thread: decoding and playback (shows as `Thread-4`) | 4.8% | 10 |
| — the C++ core's thread (`QtThread`) | 1.6% | 27 |
| — Java main thread: the media session | 1.0% | 7 |
| — `ao/audiotrack`: audio output | 0.6% | 13 |
| Yandex Music backend | 0.14% | < 1 |
| Local folder backend | 0.00% | 0 |

Minimized with the screen on (60 s): the service 9.0%, the UI 0.08%. With the
app's screen up (20 s): the UI 48%, the service 4.8%; what animates there wasn't
looked into.

To repeat: read `utime + stime` from `/proc/<pid>/task/<tid>/stat` and
`voluntary_ctxt_switches` from its `status` twice, some seconds apart, through
`adb shell`, and subtract. The release APK isn't debuggable, so `simpleperf`
can't attach to it: breaking mpv's share down needs a profileable build.
`adb shell cmd media_session volume --stream 3 --set 0` didn't mute this phone: check the
volume before starting playback.

## Processors, wake locks, pause

- Minimized, the service and its backends are in cpuset `/foreground`:
  processors 0–5 on a Pixel 7 (four Cortex-A55, two Cortex-A78), not the two
  Cortex-X1 (6–7). The UI is in `/background` (0–3).
- `top-app` priority (the screen's `BIND_IMPORTANT` binding, `MainActivity`)
  lasts only while the screen is up, as intended.
- The app holds no wake lock of its own; there is the system's `AudioMix` for
  the audio.
- Paused in the background, the service and its backends ended about 70 s later.

## Work done for nobody

1. **A connection check every 30 s while playing, minimized too.** A real request
   to the service (`connection.check`, 50–155 ms in the log). On a mobile
   network each one is likely to wake the modem, so this is probably the largest
   waste; not measured. By design: `ConnectionStatus::applyPause()` pauses the
   checks only when no view is shown *and* nothing plays. Failures during
   playback have their own recheck (`Connectivity::suspect()`).
2. **`viewShown_` starts as true.** A core started without a screen (a media
   button, a car, boot) believes it is seen, and goes on checking even when
   paused, until a view says otherwise. From the code.
3. **The position is reported four times a second** (`kPositionIntervalMs`,
   `NowPlaying.cpp`). Each tick emits `positionMsChanged` and
   `durationMsChanged`, and `ServiceMain.cpp` connects both to `reportState()`:
   about eight JNI calls a second, each making `CorePlayer` rebuild its state
   with the queue (up to about 50 items). Media3 extrapolates the position by
   itself, so it needs a report only when something changes (play, pause, seek,
   track, duration).
4. **The same properties go to the UI process**, whose event loop is blocked
   while it's minimized (it barely woke). They are then presumably buffered in
   the service's memory, an estimated 1–2 MB an hour. Inferred; the growth
   wasn't measured.
5. **The position is saved to the settings every 3 s**
   (`kPositionSaveIntervalMs`, `ActivePlaylist.cpp`). Saving less often would
   restore it less exactly after a sudden power cut, a car's for one.

## Not verified

- **Minimizing from the About page or the welcome panel.** There the core also
  analyzes the audio for the clouds (`AudioPulse`, a 16 ms timer and an mpv
  filter). The UI turns it off when `Qt.application.state` leaves
  `ApplicationActive`, by a message sent just before its event loop blocks.
- **Whether such messages always arrive.** The app was started behind the lock
  screen and went to the background at once; connection checks then continued
  after a pause, so the core still took the screen for shown. That start is
  artificial, but `setViewShown()` and the pulse's release rely on the same
  mechanism.
- Battery drain in mAh, and anything on a mobile network.

## Ideas

- Let the service tell for itself that no screen is up: `PlaybackService` gets
  the `ACTION_VIEWS` binding from the screen and can see it dropped, whether or
  not the UI managed to say anything. Its `onUnbind()` has to return true, so
  that `onRebind()` and later unbinds are delivered.
- With no screen up: stop the audio analysis, the periodic connection checks
  (items 1, 2) and the position updates to the views (item 4).
- Report state to Java only on changes (item 3).
- Save the position less often, still saving at once on pause (item 5).
