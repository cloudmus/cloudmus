# 0.2.0 (2026-09-28)

- Theme setting: follow the system, or force light or dark
- The next track is preloaded while the current one plays, so moving on starts without waiting on the network; the seek slider shows how much of the track is already buffered
- The last playlist and track are restored on startup, along with the sidebar's collapsed rows and open page; the playlist scrolls to the playing track
- MPRIS: seeking, volume, shuffle and repeat
- Anonymous usage analytics, which can be turned off in Settings
- Glass background: a blur of what's behind the window for the sidebar, lists, toolbar, menus and tooltips on KDE Plasma and any Wayland compositor with the standard blur protocol (also in the AppImage), and a see-through mode for GNOME with an extension such as Blur my Shell; switchable in Settings, in both light and dark themes
- Downloads manager: downloads are off until turned on in Settings; whole playlists can be downloaded, with a progress ring on the toolbar and a panel to follow and cancel each download, and optional subfolders by source, artist, or artist and album
- Shuffle and repeat (off, whole list, current track) in the toolbar, remembered across restarts
- Sidebar favorites: a source's stations and mixes live on its page, where a star pins any of them to the sidebar
- Yandex Music: personal playlists (Playlist of the Day, Déjà Vu, Premiere, Secret Stash), recommended stations, playlist covers, and stream and download quality settings
- YouTube Music: all personal mixes (My Supermix, My Mix 1–7, Discover, Replay…) with covers, playing like My Wave; listened tracks are added to the account's YouTube history
- Proxies: named HTTP and SOCKS5 proxies in Settings and a connection choice per source (system, direct or a proxy); a source's streams and covers go through it too
- Settings rebuilt: a sidebar of groups, a page per source to switch it on and off, sign in and out, and edit the source's own settings (e.g. Local Folder's music folder); launch at login, optionally hidden in the tray
- Protocol 1.4–1.8: source settings (1.4), sign-in instructions worded by the source (1.5), several stations per source and `Playlist.featured` (1.6), download progress and cancelling (1.7), `playback.resolveStream` for preloading (1.8)
- Toasts are colored for errors and successes
- A radio station opened from the sidebar gets the full hero panel with a big Play
- Panels can no longer be dragged shut or squeezed below their content; horizontal scrolling uses the overlay scrollbar; the hero cover shrinks in a short window
- Tooltips and the track card fade in on Wayland too and let clicks through; the track card shows only after the cursor rests on a row
- On X11 the window comes back from the tray where it was hidden from
- Fix a track whose stream is refused (e.g. YouTube's 403) leaving the player stuck: it's retried, then left paused for Play to load anew
- Fix the Settings page jumping to the top on signing in or out
- Fix the seek slider shifting as the time labels tick
- Fix a list with repeat off replaying its last track at the end

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
