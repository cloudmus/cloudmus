# Sounds

`cloudmus-logo.mp3` is CloudMus's own sound logo (a handpan, from the project's
showcase), played when the logo is tapped in About; licensed as the app is.
Brought up from -14 to -7.5 LUFS, as loud as the louder tracks around it: a
3:1 compressor from -20 dB with its gain made up, then a limiter at -1 dBFS
(ffmpeg: `acompressor=threshold=0.1:ratio=3:attack=6:release=220:makeup=8,`
`alimiter=limit=0.89:attack=3:release=80:level=false`).

The hotkey sounds are unmodified, renamed copies of sounds from KDE's
**Ocean** sound theme (https://invent.kde.org/plasma/ocean-sound-theme).

Copyright © 2023 Guilherme Marçal Silva <guimarcalsilva@gmail.com>

Licensed under the Creative Commons Attribution-ShareAlike 4.0 International
license (CC-BY-SA-4.0): https://creativecommons.org/licenses/by-sa/4.0/

| File | Ocean original |
|---|---|
| `like.oga` | `stereo/desktop-login.oga` |
| `unlike.oga` | `stereo/outcome-failure.oga` |
| `dislike.oga` | `stereo/battery-low.oga` |
| `undislike.oga` | `stereo/power-plug.oga` |
| `download.oga` | `stereo/dialog-error.oga` |
