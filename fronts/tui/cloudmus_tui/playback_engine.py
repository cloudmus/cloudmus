"""mpv-based playback engine, used only for providesStream backends
(docs/protocol.md §4, §7.2, §11.1). Generalized from ym_player/player.py:
the mpv queue/eof-watcher logic is unchanged in spirit, but the source of
truth for "what to play next" is now a track/streamReady notification
correlated by requestId, not a direct Yandex download link resolved
in-process. selfPlayback backends bypass this engine entirely — the front
would just mirror their state/changed pushes (no real v1 backend uses that
capability; see backends/py-rpc-common's synthetic test fixture instead).
"""
from __future__ import annotations

import asyncio
import ctypes.util
import glob
import logging
import os
import sys
import threading
from typing import Any, Callable, Optional

logger = logging.getLogger(__name__)

PLAY_TIMEOUT_SEC = 10.0


def _patch_find_library_for_bundled_mpv() -> None:
    """See ym_player/player.py's original comment: in a PyInstaller build,
    libmpv ships inside the unpacked bundle (sys._MEIPASS), but ctypes.util
    find_library only looks at the system ldconfig cache on Linux and misses
    it — so point it at the bundled copy directly when running packaged."""
    meipass = getattr(sys, "_MEIPASS", None)
    if not meipass:
        return
    candidates = sorted(glob.glob(os.path.join(meipass, "libmpv.so*")))
    if not candidates:
        return
    bundled_path = candidates[0]
    original_find_library = ctypes.util.find_library

    def patched_find_library(name):
        if name == "mpv":
            return bundled_path
        return original_find_library(name)

    ctypes.util.find_library = patched_find_library


class QueueEntry:
    __slots__ = ("source_id", "track")

    def __init__(self, source_id: str, track: dict[str, Any]):
        self.source_id = source_id
        self.track = track


class PlaybackEngine:
    def __init__(
        self,
        source_manager,
        on_track_change: Optional[Callable[[Optional[QueueEntry]], None]] = None,
        on_error: Optional[Callable[[str], None]] = None,
    ):
        _patch_find_library_for_bundled_mpv()
        import mpv as mpv_mod  # deferred: requires libmpv to be installed

        self.source_manager = source_manager
        self.on_track_change = on_track_change
        self.on_error = on_error

        # See ym_player/player.py: native PipeWire AO is unreliable with this
        # packaging, route through pulse/alsa (pipewire-pulse on PipeWire desktops).
        self.mpv = mpv_mod.MPV(ao="pulse,alsa")
        self.mpv.volume = 100

        self.queue: list[QueueEntry] = []
        self.index = -1
        self.wave = False
        self.wave_source_id: Optional[str] = None
        self.wave_station_id: Optional[str] = None

        self._latest_request_id: Optional[int] = None
        self._latest_request_source: Optional[str] = None
        self._resolved_request_id: Optional[int] = None
        self._loaded = False

        self._loop = asyncio.get_running_loop()
        self._manual_transition = threading.Event()
        self._watcher_stop = threading.Event()
        self._watcher = threading.Thread(target=self._watch_eof, daemon=True)
        self._watcher.start()

    # --- public state ---

    def current(self) -> Optional[QueueEntry]:
        if 0 <= self.index < len(self.queue):
            return self.queue[self.index]
        return None

    def is_paused(self) -> bool:
        return bool(self.mpv.pause)

    def position(self) -> tuple[float, float]:
        return self.mpv.time_pos or 0.0, self.mpv.duration or 0.0

    # --- queue loading ---

    def load_queue(self, source_id: str, tracks: list[dict[str, Any]], start_index: int = 0) -> asyncio.Task:
        self.wave = False
        self.wave_source_id = None
        self.wave_station_id = None
        self.queue = [QueueEntry(source_id, t) for t in tracks]
        self.index = start_index - 1
        return asyncio.create_task(self._advance(direction=1, auto=False))

    def start_radio(self, source_id: str, station_id: str, initial_tracks: list[dict[str, Any]]) -> asyncio.Task:
        self.wave = True
        self.wave_source_id = source_id
        self.wave_station_id = station_id
        self.queue = [QueueEntry(source_id, t) for t in initial_tracks]
        self.index = -1
        return asyncio.create_task(self._advance(direction=1, auto=False))

    def on_tracks_added(self, source_id: str, station_id: str, tracks: list[dict[str, Any]]) -> None:
        if self.wave and self.wave_source_id == source_id and self.wave_station_id == station_id:
            self.queue.extend(QueueEntry(source_id, t) for t in tracks)

    # --- control ---

    def next(self) -> asyncio.Task:
        return asyncio.create_task(self._advance(direction=1, auto=False))

    def prev(self) -> Optional[asyncio.Task]:
        if self.wave:
            return None
        return asyncio.create_task(self._advance(direction=-1, auto=False))

    def toggle_pause(self) -> None:
        self.mpv.pause = not self.mpv.pause

    def seek(self, seconds: float) -> None:
        try:
            self.mpv.seek(seconds, reference="relative")
        except Exception:
            pass

    def set_volume(self, volume: int) -> None:
        self.mpv.volume = max(0, min(100, volume))

    def stop(self) -> None:
        self._watcher_stop.set()
        try:
            self.mpv.stop()
        except Exception:
            pass

    def shutdown(self) -> None:
        self.stop()
        try:
            self.mpv.terminate()
        except Exception:
            pass

    # --- internal ---

    def _report_error(self, message: str) -> None:
        if self.on_error:
            self.on_error(message)

    def _cancel_latest_request(self) -> None:
        if self._latest_request_id is None or self._latest_request_source is None:
            return
        client = self.source_manager.clients.get(self._latest_request_source)
        request_id = self._latest_request_id
        if client is not None:
            asyncio.create_task(client.notify_fire_and_forget("playback.cancel", {"requestId": request_id}))
        self._latest_request_id = None
        self._latest_request_source = None

    async def _advance(self, direction: int, auto: bool) -> None:
        prev_entry = self.current()
        if prev_entry is not None and self.wave:
            played, _dur = self.position()
            client = self.source_manager.clients.get(prev_entry.source_id)
            if client is not None:
                method = "feedback.trackFinished" if auto else "feedback.skip"
                try:
                    await client.request(method, {"trackId": prev_entry.track["id"], "playedMs": int(played * 1000)})
                except Exception as e:
                    logger.debug("%s failed: %s", method, e)

        self._cancel_latest_request()

        self.index += direction
        if self.index < 0:
            self.index = 0
            return

        if self.index >= len(self.queue):
            self.index = len(self.queue)
            if self.on_track_change:
                self.on_track_change(None)
            return

        entry = self.current()
        await self._play_entry(entry, auto=auto)

    async def _play_entry(self, entry: QueueEntry, auto: bool) -> None:
        client = self.source_manager.clients.get(entry.source_id)
        if client is None:
            self._report_error(f"Backend “{entry.source_id}” is unavailable")
            asyncio.create_task(self._advance(direction=1, auto=True))
            return

        try:
            request_id, pending = await client.start_call("playback.play", {"trackId": entry.track["id"]})
        except Exception as e:
            self._report_error(f"Failed to start “{entry.track.get('title', '?')}”: {e}")
            asyncio.create_task(self._advance(direction=1, auto=True))
            return

        # Recorded immediately (no `await` since start_call returned) so a
        # fast-resolving backend's track/streamReady can't arrive and be
        # dispatched before this id is in place — see start_call's docstring.
        self._latest_request_id = request_id
        self._latest_request_source = entry.source_id
        asyncio.create_task(self._play_timeout_watchdog(entry, request_id))

        try:
            await pending
        except Exception as e:
            self._report_error(f"Failed to start “{entry.track.get('title', '?')}”: {e}")
            if self._latest_request_id == request_id:
                asyncio.create_task(self._advance(direction=1, auto=True))
            return

        if self.wave:
            try:
                await client.request("feedback.trackStarted", {"trackId": entry.track["id"]})
            except Exception as e:
                logger.debug("feedback.trackStarted failed: %s", e)

        if self.on_track_change:
            self.on_track_change(entry)

    async def _play_timeout_watchdog(self, entry: QueueEntry, request_id: int) -> None:
        await asyncio.sleep(PLAY_TIMEOUT_SEC)
        if self._latest_request_id != request_id or self._resolved_request_id == request_id:
            return  # already resolved or superseded
        self._report_error(f"Timed out resolving “{entry.track.get('title', '?')}”, skipping")
        self._cancel_latest_request()
        asyncio.create_task(self._advance(direction=1, auto=True))

    def handle_stream_ready(self, source_id: str, request_id: int, stream: dict[str, Any]) -> None:
        if request_id != self._latest_request_id or source_id != self._latest_request_source:
            return  # superseded by a newer play — discard (docs/protocol.md §11.1)
        self._resolved_request_id = request_id
        if self._loaded:
            self._manual_transition.set()
        self.mpv.play(stream["url"])
        self.mpv.pause = False
        self._loaded = True

    def _watch_eof(self) -> None:
        while not self._watcher_stop.is_set():
            try:
                self.mpv.wait_for_playback()
            except Exception as e:
                self._report_error(f"Playback watcher died: {e}")
                return
            if self._watcher_stop.is_set():
                return
            if self._manual_transition.is_set():
                self._manual_transition.clear()
                continue
            asyncio.run_coroutine_threadsafe(self._advance(direction=1, auto=True), self._loop)
