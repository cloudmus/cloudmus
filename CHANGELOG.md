# 0.1.1 (2026-09-24)

- Local folder plays your system music folder (`xdg-user-dir MUSIC`, e.g. `~/Музыка`) instead of always `~/Music`; downloads go there too unless another folder is chosen in Settings
- Fix the About dialog opening narrow and very tall on a screen with a different scale
- Fix Yandex Music My Wave stopping at the end of a batch when tracks are played through without skipping

# 0.1.0 (2026-09-24)

- AppImage for Linux with the Qt app and all backends bundled; built on every release and attached to its GitHub release
- Tray icon with playback controls, like/dislike and the playlists checklist for the playing track; Show/Hide brings back a minimized window
- Add the track to your playlists or remove it from them via the toolbar, track menu or tray (Yandex Music, YouTube Music)
- In-app toasts with a countdown bar and a close button
- Desktop notifications for every track, with its cover; clicking one brings up the player
- About dialog with the exact app version
- Yandex Music My Wave: start a wave from any track, following its style; the queue stays after the wave ends, and tracks no longer repeat endlessly
- Like and dislike tracks (and undo either) from the toolbar
- Queue view and a browse sheet for opening playlists alongside the one playing
- Sidebar with service and history icons, a context menu, loading indicators, and double-click to play a playlist
- Download tracks from Yandex Music and YouTube Music via the track menu or the downloads button
- Playback history in a History playlist, with the time each track was played
- YouTube Music support: library, liked tracks, playlists, radio from a track (sign in by pasting browser request headers)
- Qt desktop app: light and dark themes, overlay scrollbars, smooth scrolling and animated transitions, generated covers for playlists without one, an "open track page" button, per-service sign-in panel, MPRIS, global hotkeys; plays audio via libmpv
- Terminal (TUI) player
- Local music folder support, subfolders shown as playlists, with embedded cover art
- Yandex Music support: library, playlists, My Wave, likes
- Every music service runs as a separate backend process talking to the UI over a documented JSON-RPC protocol (version 1.3, `docs/protocol.md`), with a conformance suite for new backends
- `CLOUDMUS_QT_DEBUG` / `CLOUDMUS_TUI_DEBUG` debug logs, including every RPC call
