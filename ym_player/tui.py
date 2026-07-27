import asyncio
import threading
from pathlib import Path
from typing import Optional

from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal
from textual.widgets import Footer, Header, Label, ListItem, ListView, Static
from yandex_music import Track

from . import downloader
from .client import get_client
from .player import Player, WAVE_STATION


def _track_label(track: Track) -> str:
    artists = ", ".join(track.artists_name()) if track.artists_name() else "?"
    return f"{artists} — {track.title}"


class SourceItem(ListItem):
    def __init__(self, label: str, kind: str, payload=None):
        super().__init__(Label(label))
        self.kind = kind
        self.payload = payload


class TrackItem(ListItem):
    def __init__(self, index: int, track: Track):
        super().__init__(Label(f"{index + 1}. {_track_label(track)}"))
        self.track_index = index


class PlayerApp(App):
    TITLE = "Yandex Music Player"

    CSS = """
    #sidebar { width: 38%; border-right: solid $accent; }
    #tracks { width: 62%; }
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

    def __init__(self):
        super().__init__()
        self.client = get_client()
        self.player = Player(
            self.client,
            on_track_change=self._on_track_change_threadsafe,
            on_error=self._on_error_threadsafe,
        )
        self._current_tracks: list[Track] = []

    def compose(self) -> ComposeResult:
        yield Header()
        with Horizontal():
            yield ListView(
                SourceItem("My Wave", "wave"),
                SourceItem("Liked Tracks", "likes"),
                *self._playlist_items(),
                id="sidebar",
            )
            yield ListView(id="tracks")
        yield Static("Nothing is playing", id="now-playing")
        yield Footer()

    def _playlist_items(self) -> list[SourceItem]:
        try:
            playlists = self.client.users_playlists_list()
        except Exception as e:
            self.call_later(lambda: self.notify(f"Failed to load playlists: {e}", severity="error"))
            playlists = []
        return [SourceItem(pl.title or "(untitled)", "playlist", pl) for pl in playlists]

    def on_mount(self) -> None:
        self.set_interval(1.0, self._refresh_now_playing)

    # --- thread-safe callbacks from Player ---
    #
    # Player calls these either from its own background watcher thread (auto
    # advance, wave errors), or synchronously from the app thread (when the
    # TUI itself called next()/start_wave() etc. inside asyncio.to_thread —
    # that's a separate pool thread, not the app thread). call_from_thread is
    # only allowed from a non-app thread, hence the branch below.

    def _on_track_change_threadsafe(self, track: Optional[Track]) -> None:
        if threading.current_thread() is threading.main_thread():
            self._refresh_now_playing()
        else:
            self.call_from_thread(self._refresh_now_playing)

    def _on_error_threadsafe(self, message: str) -> None:
        def show():
            self.notify(message, severity="error", timeout=6)

        if threading.current_thread() is threading.main_thread():
            show()
        else:
            self.call_from_thread(show)

    def _refresh_now_playing(self) -> None:
        bar = self.query_one("#now-playing", Static)
        track = self.player.current()
        if track is None:
            bar.update("Queue finished" if self.player.queue else "Nothing is playing")
            return
        pos, dur = self.player.position()
        state = "paused" if self.player.is_paused() else "playing"
        wave = " [wave]" if self.player.wave else ""
        bar.update(
            f"{state}{wave}: {_track_label(track)}  "
            f"[{int(pos) // 60}:{int(pos) % 60:02d}/{int(dur) // 60}:{int(dur) % 60:02d}]  "
            f"vol {int(self.player.mpv.volume)}"
        )

    async def on_list_view_selected(self, event: ListView.Selected) -> None:
        if event.list_view.id == "sidebar":
            await self._load_source(event.item)
        elif event.list_view.id == "tracks":
            start_index = event.item.track_index
            self.notify("Starting...", timeout=2)
            await self._run_safely(self.player.load_queue, self._current_tracks, start_index)

    async def _load_source(self, item: SourceItem) -> None:
        tracks_view = self.query_one("#tracks", ListView)
        await tracks_view.clear()

        if item.kind == "wave":
            self._current_tracks = []
            self.notify("Starting My Wave...", timeout=3)
            await self._run_safely(self.player.start_wave, WAVE_STATION)
            return

        label = "Liked Tracks" if item.kind == "likes" else (item.payload.title or "playlist")
        self.notify(f"Loading “{label}”...", timeout=3)

        try:
            if item.kind == "likes":
                tracks = await asyncio.to_thread(self._fetch_liked_tracks)
            else:
                tracks = await asyncio.to_thread(downloader.playlist_tracks, item.payload)
        except Exception as e:
            self.notify(f"Failed to load “{label}”: {e}", severity="error", timeout=6)
            return

        self._current_tracks = tracks
        for i, track in enumerate(tracks):
            await tracks_view.append(TrackItem(i, track))

        if tracks:
            tracks_view.index = 0
            tracks_view.focus()
            self.notify(f"“{label}”: {len(tracks)} tracks", timeout=2)
        else:
            self.notify(f"“{label}” is empty", timeout=3)

    def _fetch_liked_tracks(self) -> list[Track]:
        liked = self.client.users_likes_tracks()
        ids = liked.tracks_ids if liked else []
        return self.client.tracks(ids) if ids else []

    async def _run_safely(self, fn, *args) -> None:
        """Runs a blocking player call in a worker thread so the UI never freezes,
        and shows an error toast instead of crashing the app."""
        try:
            await asyncio.to_thread(fn, *args)
        except Exception as e:
            self.notify(str(e), severity="error", timeout=6)

    def action_toggle_pause(self) -> None:
        if self.player.current() is None and self._current_tracks:
            self.run_worker(self._run_safely(self.player.load_queue, self._current_tracks, 0))
        else:
            self.player.toggle_pause()
            self._refresh_now_playing()

    def action_next_track(self) -> None:
        self.notify("Next track...", timeout=2)
        self.run_worker(self._run_safely(self.player.next))

    def action_prev_track(self) -> None:
        self.notify("Previous track...", timeout=2)
        self.run_worker(self._run_safely(self.player.prev))

    def action_save_track(self) -> None:
        track = self.player.current()
        if track is None:
            self.notify("Nothing is playing", severity="warning", timeout=3)
            return
        self.notify(f"Saving “{_track_label(track)}”...", timeout=3)
        self.run_worker(self._save_track(track))

    async def _save_track(self, track: Track) -> None:
        try:
            path = await asyncio.to_thread(downloader.download_track, track, Path.cwd(), None, lambda _msg: None)
        except Exception as e:
            self.notify(f"Failed to save “{_track_label(track)}”: {e}", severity="error", timeout=6)
            return
        self.notify(f"Saved: {path}", timeout=4)

    def action_vol_up(self) -> None:
        self.player.set_volume(int(self.player.mpv.volume) + 5)
        self._refresh_now_playing()

    def action_vol_down(self) -> None:
        self.player.set_volume(int(self.player.mpv.volume) - 5)
        self._refresh_now_playing()

    def action_quit(self) -> None:
        self.player.shutdown()
        self.exit()


def run() -> None:
    PlayerApp().run()
