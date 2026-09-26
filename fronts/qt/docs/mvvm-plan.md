# MVVM refactoring plan (Qt front)

Status: stages 0–3 done. Update this file as stages land.

## Where we are

`Ui::MainWindow` (~1900 lines) does three jobs at once:

- **Application logic**: receives backend notifications (`wireSource`) and
  forwards them to playback, auth state and caches; runs sign-in
  (`ensureAuthenticatedAsync`); loads playlists (`loadPlaylistsAsync`);
  keeps what's active and what's open in the sheet (`activeContext_`,
  `sheetContext_`) and restores both at startup; like/dislike, downloads,
  playlist editing, starting radio.
- **Owning services**: `CoverArtCache`, `PlaybackHistory`, `TrackStates`,
  `AuthStates`.
- **The view**: sidebar, hero panel, track list, sheet, toolbar, menus,
  dialogs.

So the window can't be recreated without losing state, `TrayIcon` and the
notifications reach straight into `MainWindow` (`nowPlayingFeedback()`,
`coverArtCache()`), and nothing in the Qt front is unit-tested.

Already well separated, to build on: `PlaybackController`,
`SourceManager`/`RpcClient`, `AuthStates`, `PlaybackHistory`, `TrackStates`,
`SidebarModel`/`TrackListModel`.

## Goal

1. The window is thin: it builds widgets, binds them to view models and
   forwards user actions. It can be recreated at any moment with nothing
   lost.
2. Logic and state live in view models and services created in `main()`
   and living for the whole run.
3. View models know nothing about widgets — testable, easier Windows/macOS
   ports, and a possible QML front later.

## Layers

```
main()
 └─ App::Core (lives for the whole run)
     ├─ services: SourceManager, PlaybackController, AuthStates, PlaybackHistory,
     │            TrackStates, CoverArtCache, Settings, SourceSession (notification dispatch)
     └─ view models: Library, NowPlaying, ActivePlaylist, Browse, SourcePage, Messages
 └─ Ui::MainWindow (recreatable) ── binds to the view models
 └─ Tray, MPRIS, notifications ── bind to the view models, not the window
```

Rules:

- A view model is a `QObject` with `Q_PROPERTY`s and `NOTIFY` signals;
  user actions are public methods/slots; async work runs inside it as
  `Rpc::Task`; lists are exposed as `QAbstractItemModel`s.
- Services and view models live in a static library, `cloudmus-core`,
  linked against Qt Core/Gui/Network/DBus but **not** Widgets — the linker
  enforces "a view model never touches a widget".
- Folders follow the existing "folder = namespace" rule: `src/App/`,
  `src/ViewModel/` (`namespace ViewModel`), next to the existing `Rpc/`,
  `Playback/`, `History/`, `Library/`, which move into core.
- View models don't show toasts: they emit messages through
  `ViewModel::Messages`; the window's `ToastNotifier` only displays them.

## View models and what moves into each

| View model | Taken from MainWindow | Bound by |
|---|---|---|
| **App::SourceSession** (service) | `wireSource`, `ensureAuthenticatedAsync`, `submitAuth`/`retryAuth`, forwarding `streamReady`/`tracksAdded` to playback and caches | everything |
| **Library** | `loadPlaylistsAsync`, per-source playlist cache, loading/error flags, favorites, collapsed rows, `pendingRestore_`/`pendingSelection_`, owns `SidebarModel` | sidebar, source page |
| **NowPlaying** | current track, playing/loading, position, play modes, like/dislike (support, state, busy), download, web URL, `refreshNowPlayingFeedback`, `likeToggledAsync`/`dislikeToggledAsync`, `downloadCurrentTrackAsync` | NowPlayingBar, hero, **tray, MPRIS, notifications** |
| **ActivePlaylist** | `activeContext_`, `activeTracks_`, `activate`, `activateAndPlayAsync`, `loadActiveTracksAsync`, `startRadioAsync`, `playActive`, owns the main list's `TrackListModel` | main list, hero in playlist mode |
| **Browse** (sheet) | `sheetContext_`, `openInSheetAsync`, `openHistoryInSheet`, `fillHistorySheet`, `activateFromSheet`, `playAllFromSheet` | `PlaylistSheet`, `SourcePanel` |
| **SourcePage** | a source's auth section state (`refreshAuthSection`), capabilities, `currentStatusPanelSourceId_` | `SourcePanel`, sidebar indicators |
| **PlaylistEditing** (service) | `fillPlaylistsMenuAsync`, `setTrackInPlaylistAsync`, `applyPlaylistEdit`, `sourceCanEditPlaylists` | context menus, tray |
| **Messages** | everything now going to `toastNotifier_->showError/showSuccess` | the window's and the Settings dialog's `ToastNotifier` |

What stays in `MainWindow`: building widgets, splitters, geometry, hiding to
the tray, glass, building menus (their content comes from view models),
sidebar selection from `Library.selectionKey`, delegates and hover.

## Stages

Each stage is its own commit; the app fully works after each and behaves
the same.

0. **Groundwork.** Split out the `cloudmus-core` static library (`Rpc`,
   `Playback`, `Config`, `Net`, `History`, `Library`, the generated RPC
   stubs) and add a QtTest target. Tests talk to a fake backend: the test
   binary itself, run with `--fake-backend`, speaking NDJSON JSON-RPC on
   stdin/stdout — so `RpcClient` is tested through its real transport,
   with no test-only seams in production code.
1. **`App::Core` in `main()`.** Move constructing `CoverArtCache` (into
   core, as `Covers::CoverArtCache`), `PlaybackHistory`, `TrackStates`,
   `AuthStates` out of `MainWindow` into `App::Core`, passed to the window
   by reference. No logic moves yet.
2. **`App::SourceSession`.** Backend notification dispatch and sign-in —
   playback and sign-in no longer depend on the window.
3. **`ViewModel::Messages`.** Replaces direct toast calls.
4. **`ViewModel::NowPlaying`.** NowPlayingBar, the hero's now-playing mode,
   **tray and MPRIS** move onto it; `MainWindow::nowPlayingFeedback()`,
   `setNowPlayingLiked()`, `fillNowPlayingPlaylistsMenu()` go away. Tests:
   like with rollback on failure, busy state, track change.
5. **`App::PlaylistEditing`.** The "Add to playlist" menus from the list,
   the sheet, the toolbar and the tray.
6. **`ViewModel::Library`.** Playlists, favorites, collapsed rows, selection
   restore. Tests: a refresh keeps favorites; restore waits for its source.
7. **`ViewModel::ActivePlaylist`.** Main list, radio, restoring the active
   playlist.
8. **`ViewModel::Browse`, `ViewModel::SourcePage`.**
9. **Recreatable window.** `main()` holds `App::Core` and the window in
   `std::unique_ptr`s; `MainWindow::bind(viewModels)`; a recreated window
   picks up the current state at once, not only from later signals. Glass
   could then switch by recreating the window instead of the
   `setWindowFlags()` trick (optional).

Stages 4 and 6–7 are the largest (that's where most of `MainWindow`'s code
is); 0–3 are small and pay off right away.

## Risks

- **Startup signal order.** Restoring the active playlist and the selection
  hinges on when a source has listed its playlists — cover with tests
  before moving.
- **Async work outliving the window.** Many `Rpc::Task`s capture the
  window's `this` today; in view models they capture the view model and
  report through signals instead of touching widgets.
- **`TrayIcon`/`MprisService` hold a `QWidget*`** for `bringToFront`/`Raise`
  — keep that behind a thin "window controller" in `main()` that knows the
  current window.
- **Size.** One stage at a time, running the app after each.
