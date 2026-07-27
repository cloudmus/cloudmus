import threading
from typing import Callable, Optional

from yandex_music import Client, Track

WAVE_STATION = "user:onyourwave"
MAX_CONSECUTIVE_FAILURES = 5


def best_download_info(track: Track):
    infos = track.get_download_info(get_direct_links=True)
    if not infos:
        raise RuntimeError(f"No download links available for track {track.title}")
    mp3_infos = [i for i in infos if i.codec == "mp3"] or infos
    return max(mp3_infos, key=lambda i: i.bitrate_in_kbps)


class Player:
    """Проигрыватель с очередью треков и режимом «Моей волны».

    Playback идёт через libmpv (без скачивания на диск). Автопереход между
    треками отслеживается фоновым потоком через события конца файла mpv;
    переходы, инициированные пользователем (next/prev), помечаются флагом,
    чтобы не сработал двойной автопереход.

    Все публичные методы делают сетевые запросы синхронно (библиотека
    yandex_music блокирующая) — вызывающая сторона (TUI) отвечает за то,
    чтобы не звать их из потока событий UI напрямую.
    """

    def __init__(
        self,
        client: Client,
        on_track_change: Optional[Callable[[Optional[Track]], None]] = None,
        on_error: Optional[Callable[[str], None]] = None,
    ):
        import mpv as mpv_mod  # импорт отложен: требует установленной libmpv

        self.client = client
        self.on_track_change = on_track_change
        self.on_error = on_error

        self.mpv = mpv_mod.MPV()
        self.mpv.volume = 100

        self.queue: list[Track] = []
        self.index = -1
        self.wave = False
        self.station = WAVE_STATION
        self._batch_id: Optional[str] = None
        self._loaded = False
        self._consecutive_failures = 0

        self._lock = threading.RLock()
        self._manual_transition = threading.Event()
        self._watcher_stop = threading.Event()
        self._watcher = threading.Thread(target=self._watch_eof, daemon=True)
        self._watcher.start()

    # --- публичное состояние ---

    def current(self) -> Optional[Track]:
        if 0 <= self.index < len(self.queue):
            return self.queue[self.index]
        return None

    def is_paused(self) -> bool:
        return bool(self.mpv.pause)

    def position(self) -> tuple[float, float]:
        pos = self.mpv.time_pos or 0.0
        dur = self.mpv.duration or 0.0
        return pos, dur

    # --- загрузка очереди ---

    def load_queue(self, tracks: list[Track], start_index: int = 0) -> None:
        with self._lock:
            self.wave = False
            self.queue = tracks
            self.index = start_index - 1
            self._consecutive_failures = 0
            self.next()

    def start_wave(self, station: str = WAVE_STATION) -> None:
        with self._lock:
            self.wave = True
            self.station = station
            self.queue = []
            self.index = -1
            self._consecutive_failures = 0
            try:
                self.client.rotor_station_feedback_radio_started(station, from_="ym-player")
            except Exception:
                pass  # это просто телеметрия для рекомендаций, не критично для воспроизведения
            try:
                self._fetch_wave_batch()
            except Exception as e:
                self._report_error(f"Failed to fetch wave tracks: {e}")
                return
            self.next()

    def _fetch_wave_batch(self) -> None:
        result = self.client.rotor_station_tracks(self.station)
        if result is None:
            return
        self._batch_id = result.batch_id
        self.queue.extend(seq.track for seq in result.sequence if seq.track)

    # --- управление ---

    def next(self) -> None:
        with self._lock:
            self._advance(direction=1, auto=False)

    def prev(self) -> None:
        if self.wave:
            return
        with self._lock:
            self._advance(direction=-1, auto=False)

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

    # --- внутреннее ---

    def _report_error(self, message: str) -> None:
        if self.on_error:
            self.on_error(message)

    def _advance(self, direction: int, auto: bool) -> None:
        prev_track = self.current()
        if prev_track is not None and self.wave:
            played, _ = self.position()
            try:
                if auto:
                    self.client.rotor_station_feedback_track_finished(
                        self.station, prev_track.track_id, played, batch_id=self._batch_id
                    )
                else:
                    self.client.rotor_station_feedback_skip(
                        self.station, prev_track.track_id, played, batch_id=self._batch_id
                    )
            except Exception:
                pass

        self.index += direction
        if self.index < 0:
            self.index = 0
            return

        if self.wave and self.index >= len(self.queue) - 2:
            try:
                self._fetch_wave_batch()
            except Exception as e:
                self._report_error(f"Failed to continue the wave: {e}")

        if self.index >= len(self.queue):
            self.index = len(self.queue)
            self.on_track_change and self.on_track_change(None)
            return

        self._play_current()

    def _play_current(self) -> None:
        track = self.current()
        if track is None:
            return

        try:
            info = best_download_info(track)
        except Exception as e:
            self._skip_broken_track(track, e)
            return

        if self._loaded:
            self._manual_transition.set()

        try:
            self.mpv.play(info.direct_link)
            self.mpv.pause = False
        except Exception as e:
            self._skip_broken_track(track, e)
            return

        self._loaded = True
        self._consecutive_failures = 0

        if self.wave:
            try:
                self.client.rotor_station_feedback_track_started(
                    self.station, track.track_id, batch_id=self._batch_id
                )
            except Exception:
                pass

        if self.on_track_change:
            self.on_track_change(track)

    def _skip_broken_track(self, track: Track, error: Exception) -> None:
        self._consecutive_failures += 1
        self._report_error(f"Skipping “{track.title}”: {error}")
        if self._consecutive_failures >= MAX_CONSECUTIVE_FAILURES:
            self._report_error("Too many consecutive errors, stopping.")
            self._consecutive_failures = 0
            return
        self._advance(direction=1, auto=True)

    def _watch_eof(self) -> None:
        while not self._watcher_stop.is_set():
            try:
                self.mpv.wait_for_playback()
            except Exception:
                return
            if self._watcher_stop.is_set():
                return
            if self._manual_transition.is_set():
                self._manual_transition.clear()
                continue
            try:
                with self._lock:
                    self._advance(direction=1, auto=True)
            except Exception as e:
                self._report_error(f"Auto-advance error: {e}")
