"""cloudmus TUI front. Generalized from ym_player/tui.py: no longer imports
yandex_music or any backend-specific module — everything goes through
source_manager/rpc_client, and the sidebar/track list/save-action are all
driven by each connected backend's declared capabilities instead of being
hardcoded to one service.
"""
from __future__ import annotations

import logging
from pathlib import Path
from typing import Any, Optional

from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal
from textual.widgets import Footer, Header, Label, ListItem, ListView, Static

from .playback_engine import PlaybackEngine, QueueEntry
from . import session
from .source_manager import BACKEND_UNAVAILABLE, SourceManager

logger = logging.getLogger(__name__)


def _track_label(track: dict[str, Any]) -> str:
    artists = ", ".join(a["name"] for a in track.get("artists", [])) or "?"
    return f"{artists} — {track.get('title', '?')}"


class SourceItem(ListItem):
    def __init__(self, label: str, kind: str, source_id: str, payload: dict[str, Any] | None = None):
        super().__init__(Label(label))
        self.kind = kind
        self.source_id = source_id
        self.payload = payload


class TrackItem(ListItem):
    def __init__(self, index: int, track: dict[str, Any]):
        super().__init__(Label(f"{index + 1}. {_track_label(track)}"))
        self.track_index = index


class PlayerApp(App):
    TITLE = "CloudMus"

    CSS = """
    #sidebar { width: 38%; border-right: solid $accent; }
    #tracks { width: 62%; }
    #auth-banner {
        height: 3; border-top: solid $warning; padding: 0 1;
        content-align: left middle; background: $warning 15%; display: none;
    }
    #now-playing { height: 3; border-top: solid $accent; padding: 0 1; content-align: left middle; }
    """

    BINDINGS = [
        Binding("space", "toggle_pause", "play/pause"),
        Binding("n", "next_track", "next"),
        Binding("p", "prev_track", "prev"),
        Binding("=", "vol_up", "vol+"),
        Binding("-", "vol_down", "vol-"),
        Binding("s", "save_track", "save"),
        Binding("q", "quit", "quit"),
    ]

    def __init__(self, manifests=None, session_path: Path | None = None):
        super().__init__()
        self._manifests = manifests
        self._session_path = session_path or session.default_path()
        self._saved_session = session.load(self._session_path)
        self._restoring = False
        self.source_manager = SourceManager(self._on_backend_notification)
        self.playback_engine: Optional[PlaybackEngine] = None
        self._current_source_id: Optional[str] = None
        self._current_playlist_id: Optional[str] = None
        self._current_playlist_title = "playlist"
        self._current_kind = "playlist"
        self._current_tracks: list[dict[str, Any]] = []
        self._resume_index = 0

    def compose(self) -> ComposeResult:
        yield Header()
        with Horizontal():
            yield ListView(id="sidebar")
            yield ListView(id="tracks")
        yield Static("", id="auth-banner")
        yield Static("Starting backends...", id="now-playing")
        yield Footer()

    async def on_mount(self) -> None:
        self.set_interval(1.0, self._refresh_now_playing)
        await self.source_manager.start_all(self._manifests)
        self.playback_engine = PlaybackEngine(
            self.source_manager,
            on_track_change=self._on_track_change,
            on_error=self._on_error,
        )
        if self._saved_session is not None and self._saved_session.tracks:
            await self._show_cached_session(self._saved_session)
        for source_id, client in list(self.source_manager.clients.items()):
            if client.capabilities and client.capabilities["auth"]["required"]:
                status = await client.request("auth.getStatus", {})
                if status["status"] != "authenticated":
                    await client.request("auth.start", {})
                    continue
            await self._populate_sidebar_for_source(source_id)
        await self._restore_session()
        self._refresh_now_playing()

    # --- sidebar population ---

    async def _populate_sidebar_for_source(self, source_id: str) -> None:
        client = self.source_manager.clients.get(source_id)
        if client is None or client.capabilities is None:
            return
        caps = client.capabilities
        source_name = client.source_info["name"] if client.source_info else source_id
        sidebar = self.query_one("#sidebar", ListView)

        # listPlaylists carries the source's stations (My Wave, personal
        # mixes, ...) and its liked entry too — docs/protocol.md §7.1.
        if caps["browse"]["playlists"] or caps["browse"]["likedTracks"] or caps["browse"]["radio"]:
            try:
                result = await client.request("catalog.listPlaylists", {})
            except Exception as e:
                self.notify(f"Failed to load playlists from {source_name}: {e}", severity="error", timeout=6)
                return
            item_kinds = {"radioStation": "wave", "liked": "likes"}
            for playlist in result["playlists"]:
                label = f"{playlist['title']}  ({source_name})"
                kind = item_kinds.get(playlist["kind"], "playlist")
                await sidebar.append(SourceItem(label, kind, source_id, payload=playlist))
            await self._restore_session()

    async def _restore_session(self) -> None:
        saved = self._saved_session
        if saved is None or self._restoring:
            return
        sidebar = self.query_one("#sidebar", ListView)
        for index, item in enumerate(sidebar.children):
            if item.source_id != saved.source_id or (item.payload or {}).get("id") != saved.playlist_id:
                continue
            sidebar.index = index
            self._restoring = True
            try:
                if item.kind == "wave" or await self._load_source(item, restoring=saved):
                    self._saved_session = None
            finally:
                self._restoring = False
            return

    @staticmethod
    def _saved_track_index(saved: session.Session, tracks: list[dict[str, Any]]) -> int:
        if saved.track_id is None:
            return 0
        index = saved.track_index
        if index is not None and index < len(tracks) and tracks[index]["id"] == saved.track_id:
            return index
        return next((i for i, track in enumerate(tracks) if track["id"] == saved.track_id), 0)

    async def _show_cached_session(self, saved: session.Session) -> None:
        self._current_source_id = saved.source_id
        self._current_playlist_id = saved.playlist_id
        self._current_playlist_title = saved.playlist_title
        self._current_kind = saved.kind
        self._current_tracks = saved.tracks or []
        self._resume_index = self._saved_track_index(saved, self._current_tracks)
        tracks_view = self.query_one("#tracks", ListView)
        for i, track in enumerate(self._current_tracks):
            await tracks_view.append(TrackItem(i, track))
        tracks_view.index = self._resume_index
        self.call_after_refresh(self._scroll_to_saved_track)

    def _scroll_to_saved_track(self) -> None:
        tracks_view = self.query_one("#tracks", ListView)
        if 0 <= self._resume_index < len(tracks_view.children):
            tracks_view.scroll_to_widget(tracks_view.children[self._resume_index], animate=False)

    def _save_session(self, track_id: str | None = None, track_index: int | None = None) -> None:
        if self._current_source_id is None or self._current_playlist_id is None:
            return
        try:
            session.save(
                self._session_path,
                session.Session(
                    self._current_source_id,
                    self._current_playlist_id,
                    track_id,
                    track_index,
                    self._current_playlist_title,
                    self._current_kind,
                    self._current_tracks,
                ),
            )
        except OSError as error:
            logger.warning("could not save TUI session: %s", error)

    # --- notifications from backends ---

    def _on_backend_notification(self, source_id: str, method: str, params: dict[str, Any]) -> None:
        if method == "track/streamReady":
            assert self.playback_engine is not None
            self.playback_engine.handle_stream_ready(source_id, params["requestId"], params["stream"])
        elif method == "radio/tracksAdded":
            assert self.playback_engine is not None
            self.playback_engine.on_tracks_added(source_id, params["stationId"], params["tracks"])
            if (self._current_kind == "wave" and self._current_source_id == source_id
                    and self.playback_engine.wave and self.playback_engine.wave_station_id == params["stationId"]):
                self._current_tracks = [entry.track for entry in self.playback_engine.queue]
                if self._current_tracks:
                    self._resume_index = min(self._resume_index, len(self._current_tracks) - 1)
                    self._save_session(self._current_tracks[self._resume_index]["id"], self._resume_index)
                else:
                    self._save_session()
                self.run_worker(self._show_radio_tracks())
        elif method == "auth/prompt":
            self._show_auth_prompt(source_id, params)
        elif method == "auth/statusChanged":
            self._on_auth_status_changed(source_id, params)
        elif method == "error":
            self.notify(params.get("message", "Backend error"), severity="error", timeout=6)
        elif method == BACKEND_UNAVAILABLE:
            self.notify(f"{source_id} is unavailable: {params.get('reason', '?')}", severity="error", timeout=8)
        elif method == "state/changed":
            pass  # selfPlayback mirroring: no real v1 backend uses this capability yet

    def _show_auth_prompt(self, source_id: str, params: dict[str, Any]) -> None:
        banner = self.query_one("#auth-banner", Static)
        if params.get("flow") == "deviceCode":
            minutes = params.get("expiresInSec", 600) // 60
            banner.update(
                f"{source_id} — sign in: open {params['url']} and enter code  {params['code']}"
                f"   (expires in ~{minutes} min)"
            )
        else:
            banner.update(f"{source_id} — sign-in required ({params.get('flow')})")
        banner.display = True

    def _on_auth_status_changed(self, source_id: str, params: dict[str, Any]) -> None:
        banner = self.query_one("#auth-banner", Static)
        if params["status"] == "authenticated":
            banner.display = False
            self.notify(f"{source_id}: signed in", timeout=3)
            self.run_worker(self._populate_sidebar_for_source(source_id))
        elif params["status"] == "error":
            banner.update(f"{source_id} — sign-in failed: {params.get('message', '?')}")
            banner.display = True
            self.notify(f"{source_id}: sign-in failed: {params.get('message', '?')}", severity="error", timeout=8)

    # --- playback engine callbacks ---

    def _on_track_change(self, entry: Optional[QueueEntry]) -> None:
        self._refresh_now_playing()
        if entry is None or self.playback_engine is None:
            return
        index = self.playback_engine.index
        if (entry.source_id == self._current_source_id
                and 0 <= index < len(self._current_tracks)
                and self._current_tracks[index] is entry.track):
            self._resume_index = index
            self._save_session(entry.track["id"], index)
            tracks_view = next(iter(self.query("#tracks")), None)
            if tracks_view is not None and index < len(tracks_view.children):
                tracks_view.scroll_to_widget(tracks_view.children[index], animate=False)

    def _on_error(self, message: str) -> None:
        self.notify(message, severity="error", timeout=6)

    def _refresh_now_playing(self) -> None:
        bar = next(iter(self.query("#now-playing")), None)
        if bar is None or self.playback_engine is None:
            return
        entry = self.playback_engine.current()
        if entry is None:
            if self._current_tracks and self._resume_index < len(self._current_tracks):
                bar.update(f"Ready: {_track_label(self._current_tracks[self._resume_index])}  (Space to play)")
                return
            bar.update("Queue finished" if self.playback_engine.queue else "Nothing is playing")
            return
        pos, dur = self.playback_engine.position()
        state = "paused" if self.playback_engine.is_paused() else "playing"
        wave = " [wave]" if self.playback_engine.wave else ""
        bar.update(
            f"{state}{wave}: {_track_label(entry.track)}  "
            f"[{int(pos) // 60}:{int(pos) % 60:02d}/{int(dur) // 60}:{int(dur) % 60:02d}]  "
            f"vol {int(self.playback_engine.mpv.volume)}"
        )

    # --- user interaction ---

    async def _show_radio_tracks(self) -> None:
        tracks_view = self.query_one("#tracks", ListView)
        await tracks_view.clear()
        for index, track in enumerate(self._current_tracks):
            await tracks_view.append(TrackItem(index, track))
        if self._current_tracks:
            tracks_view.index = min(self._resume_index, len(self._current_tracks) - 1)

    async def _start_radio(self, source_id: str, playlist_id: str, title: str,
                           resume_track: dict[str, Any] | None = None) -> bool:
        client = self.source_manager.clients.get(source_id)
        if client is None:
            self.notify(f"{source_id} is unavailable", severity="error", timeout=5)
            return False
        self.notify("Starting radio...", timeout=3)
        try:
            result = await client.request("catalog.startRadio", {"seed": playlist_id})
        except Exception as error:
            self.notify(f"Failed to start radio: {error}", severity="error", timeout=6)
            return False
        tracks = result["initialTracks"]
        if resume_track is not None:
            tracks = [resume_track, *(track for track in tracks if track["id"] != resume_track["id"])]
        self._current_source_id = source_id
        self._current_playlist_id = playlist_id
        self._current_playlist_title = title
        self._current_kind = "wave"
        self._current_tracks = tracks
        self._resume_index = 0
        self._saved_session = None
        self._save_session()
        await self._show_radio_tracks()
        assert self.playback_engine is not None
        self.playback_engine.start_radio(source_id, result["stationId"], tracks)
        return True

    async def on_list_view_selected(self, event: ListView.Selected) -> None:
        if event.list_view.id == "sidebar":
            await self._load_source(event.item)
        elif event.list_view.id == "tracks":
            start_index = event.item.track_index
            assert self.playback_engine is not None
            if self._current_kind == "wave":
                await self._start_radio(self._current_source_id, self._current_playlist_id,
                                        self._current_playlist_title, self._current_tracks[start_index])
            else:
                self.notify("Starting...", timeout=2)
                self.playback_engine.load_queue(self._current_source_id, self._current_tracks, start_index)

    async def _load_source(self, item: SourceItem, restoring: session.Session | None = None) -> bool:
        tracks_view = self.query_one("#tracks", ListView)
        client = self.source_manager.clients.get(item.source_id)
        if client is None:
            self.notify(f"{item.source_id} is unavailable", severity="error", timeout=5)
            return False

        if item.kind == "wave":
            return await self._start_radio(item.source_id, item.payload["id"], item.payload["title"])

        label = (item.payload or {}).get("title", "playlist")
        self.notify(f"Loading “{label}”...", timeout=3)
        try:
            if item.kind == "likes":
                result = await client.request("catalog.listLiked", {})
            else:
                result = await client.request("catalog.listTracks", {"playlistId": item.payload["id"]})
        except Exception as e:
            self.notify(f"Failed to load “{label}”: {e}", severity="error", timeout=6)
            return False

        await tracks_view.clear()
        self._current_source_id = item.source_id
        self._current_playlist_id = item.payload["id"]
        self._current_playlist_title = label
        self._current_kind = item.kind
        self._current_tracks = result["tracks"]
        self._resume_index = self._saved_track_index(restoring, self._current_tracks) if restoring else 0
        if restoring is None:
            self._saved_session = None
            self._save_session()
        else:
            restored_id = (
                self._current_tracks[self._resume_index]["id"]
                if self._current_tracks and restoring.track_id else None
            )
            self._save_session(restored_id, self._resume_index if restored_id else None)
        for i, track in enumerate(self._current_tracks):
            await tracks_view.append(TrackItem(i, track))

        if self._current_tracks:
            tracks_view.index = self._resume_index
            if restoring is not None:
                self.call_after_refresh(self._scroll_to_saved_track)
            if restoring is None:
                tracks_view.focus()
                self.notify(f"“{label}”: {len(self._current_tracks)} tracks", timeout=2)
        else:
            self.notify(f"“{label}” is empty", timeout=3)
        return True

    def action_toggle_pause(self) -> None:
        assert self.playback_engine is not None
        if self.playback_engine.current() is None and self._current_tracks:
            if self._current_kind == "wave":
                self.run_worker(self._start_radio(self._current_source_id, self._current_playlist_id,
                                                  self._current_playlist_title, self._current_tracks[self._resume_index]))
            else:
                self.playback_engine.load_queue(self._current_source_id, self._current_tracks, self._resume_index)
        else:
            self.playback_engine.toggle_pause()
            self._refresh_now_playing()

    def action_next_track(self) -> None:
        self.notify("Next track...", timeout=2)
        assert self.playback_engine is not None
        self.playback_engine.next()

    def action_prev_track(self) -> None:
        self.notify("Previous track...", timeout=2)
        assert self.playback_engine is not None
        self.playback_engine.prev()

    def action_save_track(self) -> None:
        assert self.playback_engine is not None
        entry = self.playback_engine.current()
        if entry is None:
            self.notify("Nothing is playing", severity="warning", timeout=3)
            return
        client = self.source_manager.clients.get(entry.source_id)
        if client is None or not (client.capabilities and client.capabilities["download"]):
            self.notify("This backend doesn't support downloading", severity="warning", timeout=4)
            return
        self.notify(f"Saving “{_track_label(entry.track)}”...", timeout=3)
        self.run_worker(self._save_track(client, entry))

    async def _save_track(self, client, entry: QueueEntry) -> None:
        try:
            result = await client.request(
                "catalog.downloadTrack",
                {"trackId": entry.track["id"], "destDir": str(Path.cwd())},
                timeout=60.0,
            )
        except Exception as e:
            self.notify(f"Failed to save “{_track_label(entry.track)}”: {e}", severity="error", timeout=6)
            return
        self.notify(f"Saved: {result['path']}", timeout=4)

    def action_vol_up(self) -> None:
        assert self.playback_engine is not None
        self.playback_engine.set_volume(int(self.playback_engine.mpv.volume) + 5)
        self._refresh_now_playing()

    def action_vol_down(self) -> None:
        assert self.playback_engine is not None
        self.playback_engine.set_volume(int(self.playback_engine.mpv.volume) - 5)
        self._refresh_now_playing()

    async def action_quit(self) -> None:
        if self.playback_engine is not None:
            self.playback_engine.shutdown()
        await self.source_manager.shutdown_all()
        self.exit()


def run() -> None:
    PlayerApp().run()
