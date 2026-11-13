# Android (the mobile front, MVP)

`fronts/mobile/` is a QML front for Android — phones and car head units —
with a Linux build of the same code for debugging.

## Layout

- **Core**: the desktop front's own `App::Core` and view models
  (`fronts/qt/core`, the `cloudmus-core` library), kept to Qt 6.7 APIs. On top,
  `src/App/MobileCore` adds what only a phone or a car has, and `src/Remote`
  offers it all to views in another process. A replica only carries
  properties, signals and slots, so what is offered acts through slots, passes
  plain types, and gives each property a notify signal of its own.
- **Backends**: the APK carries CPython and the Yandex Music and YouTube Music
  backends (`tools/bundle-python.sh`; no local folder source yet), unpacked
  into the app's files on the first run. The service starts them like the
  desktop does, through `libcmpython.so`, the one place Android lets an app
  exec from. There's no CPython for 32-bit units, so no APK for them.
- **Views** (`qml/`, `src/Ui`): `Ui::Backend` hands QML the view models
  themselves (one process) or replicas of them (`Remote::Host`).
- **Android** (`android/`): the player is a Media3 `MediaLibraryService` in
  its own process (`:playback`) which loads only `libcloudmus-core.so`. It
  starts in tens of milliseconds, keeps playing when the UI process is
  killed, answers media buttons and Android Auto, and resumes the last
  track. The UI process (Qt) connects to the core over a local socket.
- **Log**: `Logging::` (C++) and `CmLog` (Java) write one format, one file per
  process, mirrored to `Download/Cloudmus/logs` (and the app's external
  files dir). The Log page reads it back. Startup milestones are lines
  tagged `Startup` with the seconds since the system booted.

## Desktop

```bash
CMAKE_PREFIX_PATH=~/Qt/6.7.3/gcc_64 cmake -S fronts/mobile -B fronts/mobile/build -G Ninja
cmake --build fronts/mobile/build
export CLOUDMUS_DEV_BACKENDS=1                 # the backends of this checkout
fronts/mobile/build/cloudmus-mobile            # one process
fronts/mobile/build/cloudmus-mobile --split    # core in its own process, as on Android
ctest --test-dir fronts/mobile/build           # CLOUDMUS_MOBILE_SHOTS=<dir> saves screenshots
```

It shares the settings, sign-ins and history of an installed desktop CloudMus;
set `XDG_CONFIG_HOME` and `XDG_CACHE_HOME` to keep them apart.

Any Qt ≥ 6.7 works on the desktop; keep to 6.7 APIs (see below).

## Building the APK

Qt 6.7.3 is the newest release that supports Android 8 (API 26); 6.8 needs
Android 9. Setup:

```bash
pip install aqtinstall
aqt install-qt linux desktop 6.7.3 linux_gcc_64 -O ~/Qt -m qtremoteobjects
aqt install-qt linux android 6.7.3 android_arm64_v8a -O ~/Qt -m qtremoteobjects
aqt install-qt linux android 6.7.3 android_x86_64 -O ~/Qt -m qtremoteobjects  # the emulator
```

Plus an Android SDK with `platforms/android-34` and `build-tools/35.0.0`,
NDK r26b (`ndk/26.1.10909125`) and JDK 17 (`CLOUDMUS_JAVA17_HOME`).

```bash
./build-android.sh                    # arm64-v8a → dist/CloudMus-<version>-arm64-v8a.apk
./build-android.sh x86_64             # for the emulator
```

The script downloads the prebuilt libmpv (`dev.jdtech.mpv:libmpv` AAR,
mpv 0.39) and signs with a dev key, unless given another (see the script). CI
builds and publishes the arm64 APK with the other platforms (`docs/releasing.md`). Your head unit's ABI:
`adb shell getprop ro.product.cpu.abilist`.

## On a device

```bash
adb install -r -g dist/CloudMus-*-arm64-v8a.apk
adb logcat -s CloudmusStart             # startup milestones
```

- Open the app once: it asks for storage access (Android ≤ 9) so the log
  can go to `Download/Cloudmus/logs`. Android 10+ keeps it in
  `Android/data/app.cloudmus.mobile/files/logs`; **Export zip** in
  Settings → Developer tools (or on the Log page) writes a zip to
  `Download/Cloudmus`.
- Autoplay at boot is on by default; the switch is in Settings (the sliders
  icon on the playlist page).
- A head unit whose disk fills up may not boot again, so the disk is watched.
  Under 2 GB free nothing is downloaded ahead and cached tracks not in use are
  removed; under 1 GB nothing is downloaded at all, the playing track included
  (it goes on when there's room), and neither are covers saved or Python
  unpacked. A warning icon on the main screen leads to Settings, which says why.
  Other writers are bounded: logs, the covers' caches.
- The window goes on under the system's navigation bar, which is see-through, so
  the generated background reaches the screen's edge; `Theme.insetBottom` is what
  the bar covers, and what's to be read or pressed keeps clear of it. Now Playing,
  on a phone, is a layer over the pages (Main.qml) that follows the finger: up
  from the mini player, down from itself.
- Settings apply at once. A source's page (whether it runs, its connection and
  its own settings) restarts the source when a change takes that; opening it
  starts a source left unstarted for wanting a sign-in. There's no language
  setting yet: the QML has no translations. No download folder either.
- Media browsers (the car's Media Center, Android Auto) list the playlists:
  a folder for each source, even a single one (Media Center makes tabs of the
  root's folders and shows nothing else there), favorites first, as a grid of
  covers (the folders stay a list). A tap starts the playlist, waiting for its
  source if that's still coming up. Tracks inside a playlist aren't listed;
  the playing queue is, by the system's own "queue" screen. Search
  (`MediaTree::search()`) finds, in one list, the queue's tracks by title and
  artists, then playlists by title; a voice request plays the first found.
- The player's buttons beside play, previous and next (the notification, the
  car's widgets): dislike, like and close; on a GWM unit "Open CloudMus" goes
  first (below). On a car's screen through Android Auto they're dislike, like,
  shuffle and repeat instead (see Android Auto).
- GWM head units (Haval): the car's media widget follows a third-party player
  only once the unit's Media Center has picked it. The player asks for that
  itself, once, a couple of seconds after its screen first opens (a blink of
  Media Center, then the player's screen comes back; the pick is a `persist.`
  property, it stays across restarts). On any other Android, or if the
  property can't be read, nothing is asked. A tap on the player in the widget
  opens Media Center, as for any third-party player there: its "Open CloudMus"
  button brings up the player's screen. On such a unit that button goes first,
  in the row Media Center shows without unfolding; like, dislike and close
  are a row up.
- Settings → Developer tools holds what's for working on the app: the device,
  the Log page (it filters by level, process, text, and "this boot"), scrolling
  and drawing experiments, and for a head unit **Export system apps** (the
  launcher's and media apps' APKs with their compiled code, into a zip, to
  decompile) and **Log system probe**.
- To read the startup time: lines tagged `Startup` give `+N ms since process
  start` and `S s since boot`; `receiver:` lines name the broadcast that
  started the player. Head units differ in what they send when the car
  starts (many sleep rather than reboot, so `BOOT_COMPLETED` may not come):
  the log lists every broadcast the receiver gets.

## Android Auto

The phone's Android Auto draws the car's screen itself, from templates; the
player gives it the browse tree, the queue, track data and its buttons
(`PlaybackService`), so there's no layout of ours to fit. What it looks like
is set by hints: the grid of playlists, the order and icons of the buttons.

**On the phone.** An APK not from Google Play isn't listed until Android
Auto's developer settings allow it: Android Auto's settings → tap the version
10 times → ⋮ → Developer settings → **Unknown sources**. Its log on the phone
says why a package isn't listed (`adb logcat -s CAR.VALIDATOR`). Android Auto
under the battery's "Restricted" mode is unreliable: set it to unrestricted.

**Desktop Head Unit (DHU)** plays the car on this computer, over adb to the
phone. `sdkmanager "extras;google;auto"` may not find it; then unpack
`desktop-head-unit-linux-x64_r02.1.zip` (from `dl.google.com/android/repository/`,
its checksum in `repository2-3.xml` there) into
`$ANDROID_SDK_ROOT/extras/google/auto`. Use 2.1: 2.0 gets no picture from a
current Android Auto and leaves it stuck. On the phone: Android Auto's ⋮ →
**Start head unit server**. Then:

```bash
adb forward tcp:5277 tcp:5277
cd $ANDROID_SDK_ROOT/extras/google/auto && ./desktop-head-unit -c config/default_720p.ini
```

If its window stays at "Waiting for phone…", Android Auto is stuck on an old
connection: `adb shell am force-stop com.google.android.projection.gearhead`,
start the head unit server again. The DHU's console takes `screenshot <file>`,
`tap <x> <y>`, `keycode back`, `day`/`night`; from a script, run it under
`script -qfc` with its input from a FIFO. Whether the phone is on a car's
screen: `adb shell content query --uri content://androidx.car.app.connection --projection CarConnectionState`
(2: yes).

**Screens to check**, each with the player's own screen, its list and its
card:
- `config/default.ini`, 800×480: the app takes the whole screen.
- `config/default_720p.ini`, 1280×720: the app in 2/3 beside the map; the
  layout button (bottom left) swaps to the map with the player's card in 1/3.
- Wide: `default_wide.ini` with `resolution = 1920x1080` and
  `marginheight = 360` (1920×720): 2/3 and the 1/3 card, as on a wide car
  screen.

**What's done for Android Auto, and why:**
- The session doesn't send its state every 3 s for the position
  (`setPeriodicPositionUpdateEnabled(false)`): Android Auto scrolled its
  queue back to the playing track each time. The position runs on between
  the core's reports, so a state sent for anything else has it right.
- On a car's screen the buttons are dislike, like, shuffle (not for a radio)
  and repeat (off, all, one). "Close" only paused there (Android Auto keeps
  the player bound) and "Open" did nothing (Android blocks a background
  activity start). Android Auto shows every button and paints them one color,
  so on and off differ by shape (`ic_*_on`: a filled square).
- Android Auto stays connected to the player after the car is gone, so its
  connection says nothing about the car's screen; `CarProjection` asks
  Android Auto's `androidx.car.app.connection` provider, again on its
  broadcast, and the buttons follow.
- Android Auto keeps buttons' icons by resource number while connected: right
  after an update that moves the numbers, the buttons may show other icons
  until the car connects again.

## Open issue: a click on pause and track change

On Android (phone and head unit alike) a pause, a seek or a track changed by
hand clicks; the switch at a track's natural end (gapless) is clean. Not heard
on the desktop.

**Cause** (mpv 0.39 sources, `audio/out/`): `ao_audiotrack` has no pause of
its own, so a pause, a seek and a `loadfile`/`playlist-next` all reach its
`stop()`, which calls `AudioTrack.pause()` and at once `flush()`. The flush
throws the buffer away before Android has ramped the volume down, and the
wave is cut wherever it is.

**What the prebuilt AAR leaves to work with** (`dev.jdtech.mpv:libmpv`):
- `libavfilter` is cut down to ~125 KB, with no `afade`, `aeval`, `volume`,
  `astats`. So no fade in mpv's filter chain, and the level analyzer
  (`@cmlevel` in `AudioPlayer.cpp`, behind `AudioPulse`) most likely gives
  nothing on Android either.
- No `ao-volume`: `ao_audiotrack` never calls `AudioTrack.setVolume`.
- mpv's own volume (`volume`, `volume-gain`) is applied in `ao.c`
  (`process_plane`) at one gain for a whole chunk the output pulls. That's
  75–150 ms for AudioTrack (its whole buffer) and 250 ms by default for OpenSL
  ES (`opensles-frames-per-enqueue` can make it smaller).

**Tried (commit 51d9fa4, kept off):** `AudioPlayer::setEdges()` fades
`volume-gain` from a 4 ms timer (raised cosine), waits for the silence to
reach the speakers, and only then sends the pause/seek/load. Commands sent in
the meantime queue in order, and the sound comes back up after. Settings are in
Settings → Developer tools → Playback (`PlatformSettings`: output, OpenSL
chunk, fade, wait), applied in `App::MobileCore::applyPlaybackExperiments()`.
On a phone it was worse: each step of the gain was heard as a tick of its own,
with either output and with longer fades. Not checked: whether
`opensles-frames-per-enqueue` set at runtime plus `ao-reload` took effect. The
log's `AO: [opensles]` lines and `fading out …` / `silent; running …` would
show it. Even with ~10 ms chunks a stepped gain may still be audible on loud
music.

**Next: a libmpv of our own** instead of the AAR (build mpv + FFmpeg for
Android; `build-android.sh` unpacks the AAR today, `CLOUDMUS_MPV_ANDROID_DIR`
in `fronts/mobile/CMakeLists.txt`). Two small patches, either one is enough:
- In `ao.c`, ramp the gain per sample across a chunk from the last value to
  the new one. The existing `setEdges()` code then gives a smooth fade on any
  output, and only the defaults need turning on.
- In `ao_audiotrack.c` `stop()`, call `AudioTrack.setVolume(0)` and wait for
  Android's ramp (a few ms) before `pause()`/`flush()`. That fixes it at the
  source, without `setEdges()`.

A full `libavfilter` in the same build would bring the level analyzer back.

## Emulator

Android 9, landscape 1920×1080 at 160 dpi mimics a head unit: use `-gpu host`
(`swiftshader` mis-renders Qt Quick). Handy checks:

```bash
adb shell input keyevent KEYCODE_MEDIA_PLAY        # steering-wheel button, app stopped
adb shell am force-stop app.cloudmus.mobile        # then the line above
adb shell dumpsys media_session | grep -A3 app.cloudmus
```
