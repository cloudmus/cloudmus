# GWM head units: volume and storage access

An investigation of 2026-11-25 on a GWM Haval head unit (logs from the unit,
then its firmware). Storage access has a first fix (commit `1617037`, see
"Storage"). The volume is open work.

## In short

- **Volume.** The unit's Android has a fixed volume, so the player's volume
  slider shows the maximum and changes nothing. The unit's own volume lives in
  a GWM system service that only system apps may write to.
- **Storage.** GWM replaced Android's permission request and app info screen
  with its own pages. Those pages don't know about storage. Android's own
  Settings app is still installed, and storage can be granted from there.
- **No folder picker.** No app on the unit handles `OPEN_DOCUMENT_TREE`,
  `OPEN_DOCUMENT` or `GET_CONTENT`.

## The unit

- GWM Haval, Qualcomm SA8155. Android 9 (API 28) runs as a virtualized guest
  on QNX.
- Fingerprint
  `Android/sa8155_v35_ovs_st_b/sa8155_v35_ovs_st_b:9/PQ1A.190105.004/80:user/test-keys`.
- The firmware is `GW35OVBPU1G091203B11`, dumped on vm-host in
  `~/prog/my/cloudmus/GWM/GW35OVBPU1G091203B11/GW_V35_Images`, with notes
  beside it (`GW35OVBPU1G091203B11-architecture.md`).

### Reading the firmware

`system.img` and `vendor.img` are Android sparse images over ext4. `7z l|x`
reads them without mounting. APK manifests and resources: `aapt dump
xmltree|resources|badging` from the SDK's build-tools. Code: `dexdump -d` on
the `classes*.dex` inside an APK.

Some APKs carry no dex: their code is only in `oat/arm64/*.vdex`.
`services.jar`, `framework.jar` and `GWMSystemSettings` are among them. These
vdex files are v019, and `GWMSystemSettings.vdex` holds compact dex
(`cdex001`), which `dexdump` can't read. Without vdexExtractor or
`compact_dex_converter`, only their strings can be read (`strings -n 5`).
With jadx, the readable dex becomes Java.

## Volume

### What the user sees

When the player's screen opens, the volume slider is at the maximum. The
slider moves, but the volume doesn't change. The volume in the system's
notification shade works.

### What CloudMus does

- `UiBridge.volume()` reads `STREAM_MUSIC` with `getStreamVolume()` and
  `getStreamMaxVolume()`. `UiBridge.setVolume()` sets it with
  `setStreamVolume()`.
- On the C++ side: `Platform::readVolume()` and
  `Platform::setSystemVolume()` (`fronts/mobile/src/Ui/Platform.cpp`). The
  slider is in `qml/TrackPanel.qml`.

### What the firmware shows

1. **The volume is fixed.** `config_useFixedVolume` is `true` in
   `framework-res.apk` (`config_handleVolumeKeysInWindowManager` is `true`
   too). With a fixed volume Android reports the maximum to apps and ignores
   `setStreamVolume()` and `adjustStreamVolume()`. That accounts for the
   whole symptom.
2. **The shade goes through GWM's own service, not `AudioManager`.** In
   `system/priv-app/GWMSystemUI` the chain is:
   - `com.gwm.systemui.data.repository.VolumeRepository`
   - `com.gwm.gdc.systemsettings.viewmodel.sound.VolumeViewModel`, key
     `AUDIO_MEDIA_VOLUME`
   - `…datasource.bux.BuxDataSource`, which maps it to
     `sys.settings.audio.media_volume`
   - `com.gwm.gdc.datasource.DataSourceProxy`
   - `com.gwm.android.adapter.client.GwmAdapterClient`, which gets the
     `gwm_adapter` binder from `ServiceManager.getService()` (by reflection)

   Navigation, phone, voice and ringtone volumes have keys of the same kind
   (`sys.settings.audio.phone_volume` and so on). The values are strings.
3. **The `gwm_adapter` service** is `system/priv-app/GwmAdapterServer`
   (`com.gwm.android.adapter.server`, `sharedUserId` `android.uid.system`).
   Its interface is `com.gwm.android.adapter.IGwmAdapterService`, with
   `setData`, `getData`, `getDataForcely` and
   `registerDataChangeListener`/`unregisterDataChangeListener`.
   - SELinux: the service is labeled `gwm_adapter_service`. That label is in
     `app_api_service`, so a third-party app (`untrusted_app`) may look it up.
   - `setData` requires `com.gwm.android.adapter.permission.SET_DATA`, which
     is `signature|privileged` (0x12). A third-party app doesn't get it, and
     `pm grant` can't give it.
   - `com.gwm.android.adapter.permission.GET_DATA` is `normal` (0x0), so any
     app that declares it gets it. What the server checks on `getData` and on
     listener registration hasn't been read.

`gwm_adapter`'s write permission isn't to be worked around. That would mean
getting past a system service's access check. The ways below stay within what
a third-party app is allowed.

### Open questions

1. **Read the unit's volume.** With `GET_DATA` declared, can we read
   `sys.settings.audio.media_volume` (and its range) and follow its changes?
   This needs the exact AIDL contract (transaction codes; the Parcelable
   `com.gwm.android.adapter.a.c`, whose fields are a key, an array of keys and
   an array of values) and the format of the value.
2. **Open the system volume panel.** Does GWMSystemUI, or another GWM app,
   export an activity, service or broadcast that shows
   `com.gwm.systemui.dialog.VolumeDialog` or the shade's sound panel? Or one
   that opens the sound page of the settings (`GWMSystemSettings`,
   `GWMVehicleSettings`)? Exact action or component, and the permission it
   takes.
3. **Volume keys.** How are `KEYCODE_VOLUME_UP/DOWN` and the steering-wheel
   keys handled (`CarService`, `CarInput.InputService` in GwmAdapterServer)?
   With a fixed volume, does anything an app can call reach the unit's volume,
   such as a MediaSession with a `VolumeProviderCompat` or
   `AudioManager.adjustVolume()`?
4. **Other public paths.** Is `android.car.media.CarAudioManager` (volume
   groups) here, and what does it need? GWM's own media apps hold
   `android.car.permission.CAR_CONTROL_AUDIO_VOLUME`. Check its protection
   level, and whether volume groups mean anything with a fixed volume. Also
   check `com.beantechs.adapterservice` and the
   `com.beantechs.permission.ext.*` permissions.
5. **The player's own volume** works without any of this: mpv's `volume`
   (the prebuilt libmpv has no `ao-volume` and applies it in `ao.c`:
   `docs/android.md`, "Open issue: a click on pause and track change") or Media3's. It can only make the music quieter than the unit's
   volume, not louder.

Places to look: `GWMSystemUI.apk` and `GwmAdapterServer.apk` (both with dex),
`CarService.apk` and `system/framework/arm64/boot-android.car.vdex`,
`GWMSystemSettings`, `GWMVehicleSettings`, `AudioTuning`, `GWMMediaMixService`,
`vendor.img:etc/audio_policy_configuration.xml`, and SELinux in
`system/etc/selinux/plat_service_contexts`, `plat_sepolicy.cil` and
`vendor.img:etc/selinux/`.

### Proposal

Where `AudioManager.isVolumeFixed()` is true (only once the questions above
are answered):

- the slider sets the player's own volume (question 5), or it's hidden;
- if question 1 works, the slider shows the unit's volume, read-only, or a
  hint sits next to the player's slider;
- if question 2 finds a way, a button opens the system's volume panel.

A first step either way: log `isVolumeFixed()`, the stream's volume and its
maximum at start.

## Storage

### What the user saw

- The storage request at start (`WRITE_EXTERNAL_STORAGE`, Android 8–9), and
  "Access to media storage" (`READ_EXTERNAL_STORAGE`), came back refused at
  once, with no dialog: `permissions result: […] [-1]`, a dozen times in a
  row.
- "Grant access" opened GWM's app page, which lists only Internet.
- Saving a track reported an error. The track was in fact saved, to the
  app's own folder: `save stage=shared … WRITE_EXTERNAL_STORAGE denied`,
  then `save stage=fallback location=…/Android/data/app.cloudmus.mobile/files/Music/CloudMus`.

### What the firmware shows

1. **GWM handles the permission request.**
   `com.gwm.settings.view.ui.permission.AuthorizationActivity` in
   `com.gwm.app.settings` (`system/app/GWMSystemSettings`) is exported with
   `android.content.pm.action.REQUEST_PERMISSIONS`. `services.vdex` names it
   too. The stock `GrantPermissionsActivity` in PackageInstaller is still
   there, unchanged, but it isn't what opens.
2. **GWM's page knows three permission groups:** `CAMERA`, `LOCATION`,
   `MICROPHONE` (in its strings: `CAMERA`, `RECORD_AUDIO`,
   `ACCESS_FINE/COARSE_LOCATION`, `ACCESS_NETWORK_STATE`). Storage isn't
   among them, so a storage request is refused without a dialog. The same
   code has `WhiteApp`, `PolicyFileModel` and `PolicyHttpModel` (an app
   whitelist, a local or downloaded policy). Their logic is in compact dex
   and hasn't been read.
3. **GWM handles the app info screen.**
   `android.settings.APPLICATION_DETAILS_SETTINGS` resolves only to
   `com.gwm.settings.view.ui.MainActivity`. GWM removed that filter from
   Android's own Settings (`com.android.settings`).
4. **Android's own Settings is still reachable.** Its
   `.applications.InstalledAppDetails` (an alias of `InstalledAppDetailsTop`)
   and `Settings$ManageApplicationsActivity` are exported, with no permission.
   From the app's page there (Permissions → Storage), PackageInstaller's
   stock screen grants it. Settings holds `GRANT_RUNTIME_PERMISSIONS`.
   PackageInstaller's `ManagePermissionsActivity` itself needs
   `GRANT_RUNTIME_PERMISSIONS`, so an app can't open it directly.
5. **No document picker.** There's no DocumentsUI. None of the 172 APKs in
   `system/app` and `system/priv-app` handles `OPEN_DOCUMENT_TREE`,
   `OPEN_DOCUMENT` or `GET_CONTENT`. `PathPicker.pick()` ends in its "no
   picker" branch.
6. **MediaStore needs the permission too.** The `media` provider's read
   permission is `READ_EXTERNAL_STORAGE`.

### Done

Commit `1617037`: when `com.gwm.app.settings` is installed
(`GwmAppSettings.present()`), "Grant access" (`UiBridge.grantMusicStorage()`)
and "Access to media storage" (`MainActivity.requestMusicPermission()`) open
Android's own app page. If that page can't open, they open its list of apps,
and only after that do they fall back to what other devices do. The log says
which screen opened (`GwmAppSettings: opened …` or `can't open …`).

### Open

- **Check it on the unit:** grant storage from Android's page, then save a
  track. The log should show `save stage=shared-file`.
- **After a reboot,** is the permission still there? GWM's settings have a
  `BootBroadcastReceiver` (`BOOT_COMPLETED`, `ACCOUNT_STATUS_CHANGED`,
  `REVOKE_AGREEMENT_RESULT`), and whether it revokes anything isn't known.
- **The fallback isn't an error.** When saving ends up in the app's own
  folder, the message should say where the track went, not report a failure.
- **No picker:** where nothing handles `OPEN_DOCUMENT_TREE`, offer the
  folders the app can reach instead of a silent cancel. Those are its own
  folder on internal storage and on each USB drive
  (`getExternalFilesDirs()`), plus shared folders once storage is granted.
  Whether the app's folder on a USB drive works on this unit hasn't been
  checked: no drive showed up in the logs.
- **For development only,** `adb shell pm grant app.cloudmus.mobile
  android.permission.READ_EXTERNAL_STORAGE` (and `WRITE_…`) should work if
  adb is on (the engineering menu) and the permission isn't system- or
  policy-fixed.
