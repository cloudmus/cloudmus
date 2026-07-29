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
    TITLE = "cloudmus"

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

    def __init__(self, manifests=None):
        super().__init__()
        self._manifests = manifests
        self.source_manager = SourceManager(self._on_backend_notification)
        self.playback_engine: Optional[PlaybackEngine] = None
        self._current_source_id: Optional[str] = None
        self._current_tracks: list[dict[str, Any]] = []

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
        for source_id, client in list(self.source_manager.clients.items()):
            if client.capabilities and client.capabilities["auth"]["required"]:
                status = await client.request("auth.getStatus", {})
                if status["status"] != "authenticated":
                    await client.request("auth.start", {})
                    continue
            await self._populate_sidebar_for_source(source_id)
        self._refresh_now_playing()

    # --- sidebar population ---

    async def _populate_sidebar_for_source(self, source_id: str) -> None:
        client = self.source_manager.clients.get(source_id)
        if client is None or client.capabilities is None:
            return
        caps = client.capabilities
        source_name = client.source_info["name"] if client.source_info else source_id
        sidebar = self.query_one("#sidebar", ListView)

        if caps["browse"]["radio"]:
            await sidebar.append(SourceItem(f"My Wave  ({source_name})", "wave", source_id))
        if caps["browse"]["likedTracks"]:
            await sidebar.append(SourceItem(f"Liked Tracks  ({source_name})", "likes", source_id))
        if caps["browse"]["playlists"]:
            try:
                result = await client.request("catalog.listPlaylists", {})
            except Exception as e:
                self.notify(f"Failed to load playlists from {source_name}: {e}", severity="error", timeout=6)
                return
            for playlist in result["playlists"]:
                label = f"{playlist['title']}  ({source_name})"
                await sidebar.append(SourceItem(label, "playlist", source_id, payload=playlist))

    # --- notifications from backends ---

    def _on_backend_notification(self, source_id: str, method: str, params: dict[str, Any]) -> None:
        if method == "track/streamReady":
            assert self.playback_engine is not None
            self.playback_engine.handle_stream_ready(source_id, params["requestId"], params["stream"])
        elif method == "radio/tracksAdded":
            assert self.playback_engine is not None
            self.playback_engine.on_tracks_added(source_id, params["stationId"], params["tracks"])
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

    def _on_error(self, message: str) -> None:
        self.notify(message, severity="error", timeout=6)

    def _refresh_now_playing(self) -> None:
        bar = self.query_one("#now-playing", Static)
        if self.playback_engine is None:
            return
        entry = self.playback_engine.current()
        if entry is None:
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

    async def on_list_view_selected(self, event: ListView.Selected) -> None:
        if event.list_view.id == "sidebar":
            await self._load_source(event.item)
        elif event.list_view.id == "tracks":
            start_index = event.item.track_index
            assert self.playback_engine is not None
            self.notify("Starting...", timeout=2)
            self.playback_engine.load_queue(self._current_source_id, self._current_tracks, start_index)

    async def _load_source(self, item: SourceItem) -> None:
        tracks_view = self.query_one("#tracks", ListView)
        await tracks_view.clear()
        client = self.source_manager.clients.get(item.source_id)
        if client is None:
            self.notify(f"{item.source_id} is unavailable", severity="error", timeout=5)
            return

        if item.kind == "wave":
            self.notify("Starting radio...", timeout=3)
            try:
                result = await client.request("catalog.startRadio", {})
            except Exception as e:
                self.notify(f"Failed to start radio: {e}", severity="error", timeout=6)
                return
            assert self.playback_engine is not None
            self.playback_engine.start_radio(item.source_id, result["stationId"], result["initialTracks"])
            return

        label = "Liked Tracks" if item.kind == "likes" else (item.payload or {}).get("title", "playlist")
        self.notify(f"Loading “{label}”...", timeout=3)
        try:
            if item.kind == "likes":
                result = await client.request("catalog.listLiked", {})
            else:
                result = await client.request("catalog.listTracks", {"playlistId": item.payload["id"]})
        except Exception as e:
            self.notify(f"Failed to load “{label}”: {e}", severity="error", timeout=6)
            return

        self._current_source_id = item.source_id
        self._current_tracks = result["tracks"]
        for i, track in enumerate(self._current_tracks):
            await tracks_view.append(TrackItem(i, track))

        if self._current_tracks:
            tracks_view.index = 0
            tracks_view.focus()
            self.notify(f"“{label}”: {len(self._current_tracks)} tracks", timeout=2)
        else:
            self.notify(f"“{label}” is empty", timeout=3)

    def action_toggle_pause(self) -> None:
        assert self.playback_engine is not None
        if self.playback_engine.current() is None and self._current_tracks:
            self.playback_engine.load_queue(self._current_source_id, self._current_tracks, 0)
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
