# Android (the mobile front, MVP)

`fronts/mobile/` is a QML front for Android — phones and car head units —
with a Linux build of the same code for debugging. It plays three dev tracks
from the APK; it exists to answer how the player starts in a car, whether it
autoplays, and whether it shows up as the system media player.

## Layout

- **Core**: the desktop front's own `App::Core` and view models
  (`fronts/qt/core`, the `cloudmus-core` library), kept to Qt 6.7 APIs. On top,
  `src/App/MobileCore` adds what only a phone or a car has, and `src/Remote`
  offers it all to views in another process. A replica only carries
  properties, signals and slots, so what is offered acts through slots, passes
  plain types, and gives each property a notify signal of its own.
- **APK, for now**: the backends are Python and don't run on Android yet, so
  the APK (`CLOUDMUS_MOBILE_SHARED_CORE=OFF`, the default there) still plays
  three dev tracks through the stand-ins in `src/DevTracks`. The desktop build
  runs `App::Core` with the real backends.
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
fronts/mobile/tools/fetch-dev-tracks.sh        # 3 tracks from ~/Music → media/ (gitignored)
CMAKE_PREFIX_PATH=~/Qt/6.7.3/gcc_64 cmake -S fronts/mobile -B fronts/mobile/build -G Ninja
cmake --build fronts/mobile/build
export CLOUDMUS_DEV_BACKENDS=1                 # the backends of this checkout
fronts/mobile/build/cloudmus-mobile            # one process
fronts/mobile/build/cloudmus-mobile --split    # core in its own process, as on Android
ctest --test-dir fronts/mobile/build           # CLOUDMUS_MOBILE_SHOTS=<dir> saves screenshots
```

It shares the settings, sign-ins and history of an installed desktop CloudMus;
set `XDG_CONFIG_HOME` and `XDG_CACHE_HOME` to keep them apart. Dev tracks are
only for the APK.

Any Qt ≥ 6.7 works on the desktop; keep to 6.7 APIs (see below).

## Building the APK

Qt 6.7.3 is the newest release that supports Android 8 (API 26); 6.8 needs
Android 9. Setup:

```bash
pip install aqtinstall
aqt install-qt linux desktop 6.7.3 linux_gcc_64 -O ~/Qt -m qtremoteobjects
aqt install-qt linux android 6.7.3 android_arm64_v8a -O ~/Qt -m qtremoteobjects
aqt install-qt linux android 6.7.3 android_armv7 -O ~/Qt -m qtremoteobjects   # 32-bit units
```

Plus an Android SDK with `platforms/android-34` and `build-tools/35.0.0`,
NDK r26b (`ndk/26.1.10909125`) and JDK 17 (`CLOUDMUS_JAVA17_HOME`).

```bash
./build-android.sh                    # arm64-v8a → dist/cloudmus-mobile-arm64-v8a.apk
./build-android.sh armeabi-v7a x86_64 # several; x86_64 is for the emulator
```

The script downloads the prebuilt libmpv (`dev.jdtech.mpv:libmpv` AAR,
mpv 0.39) and signs with a dev key. Your head unit's ABI:
`adb shell getprop ro.product.cpu.abilist`.

## On a device

```bash
adb install -r -g dist/cloudmus-mobile-arm64-v8a.apk
adb logcat -s CloudmusStart             # startup milestones
```

- Open the app once: it asks for storage access (Android ≤ 9) so the log
  can go to `Download/Cloudmus/logs`. Android 10+ keeps it in
  `Android/data/dev.cloudmus.mobile/files/logs`; **Export zip** in
  Settings → Developer tools (or on the Log page) writes a zip to
  `Download/Cloudmus`.
- Autoplay at boot is on by default; the switch is in Settings (the sliders
  icon on the playlist page).
- GWM head units (Haval): the car's media widget follows a third-party player
  only once the unit's Media Center has picked it. The player asks for that
  itself, once, a couple of seconds after its screen first opens (a blink of
  Media Center, then the player's screen comes back; the pick is a `persist.`
  property, it stays across restarts). On any other Android, or if the
  property can't be read, nothing is asked. A tap on the player in the widget
  opens Media Center, as for any third-party player there: its "Open CloudMus"
  button brings up the player's screen.
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

## Emulator

Android 9, landscape 1920×1080 at 160 dpi mimics a head unit: use `-gpu host`
(`swiftshader` mis-renders Qt Quick). Handy checks:

```bash
adb shell input keyevent KEYCODE_MEDIA_PLAY        # steering-wheel button, app stopped
adb shell am force-stop dev.cloudmus.mobile        # then the line above
adb shell dumpsys media_session | grep -A3 dev.cloudmus
```
