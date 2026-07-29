import ctypes.util
import glob
import logging
import os
import sys
import threading
from typing import Callable, Optional

from yandex_music import Client, Track

logger = logging.getLogger(__name__)

WAVE_STATION = "user:onyourwave"
RETRY_INITIAL_DELAY = 1.0
RETRY_MAX_DELAY = 5.0
MAX_ATTEMPTS_PER_TRACK = 5
MAX_CONSECUTIVE_TRACK_FAILURES = 5


class _Abandoned(Exception):
    """Signals that the in-progress track transition was superseded by a
    newer one (user pressed next/prev, or the player is shutting down)."""


class _GiveUp(Exception):
    """Signals that a track failed MAX_ATTEMPTS_PER_TRACK times in a row —
    likely permanently unavailable (licensing/region), not a transient
    network blip — so it should be skipped instead of retried forever."""


def _patch_find_library_for_bundled_mpv() -> None:
    """В PyInstaller-сборке libmpv лежит внутри распакованного бандла
    (sys._MEIPASS), но ctypes.util.find_library на Linux смотрит только в
    системный ldconfig-кэш и эту директорию не видит. На машине без
    системного mpv/libmpv python-mpv из-за этого не находит библиотеку,
    хотя она реально лежит рядом — поэтому подсовываем ей путь напрямую.
    """
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
        _patch_find_library_for_bundled_mpv()
        import mpv as mpv_mod  # импорт отложен: требует установленной libmpv

        self.client = client
        self.on_track_change = on_track_change
        self.on_error = on_error

        # Родной AO PipeWire в libmpv ненадёжен при таком варианте
        # пакетирования (расхождение версий клиент/сервер ломает locking
        # внутри pw_stream_*), поэтому явно идём через pulse/alsa — на
        # PipeWire-десктопах (KDE и т.п.) это попадёт в pipewire-pulse,
        # минуя нативный клиент PipeWire целиком.
        self.mpv = mpv_mod.MPV(ao="pulse,alsa")
        self.mpv.volume = 100

        self.queue: list[Track] = []
        self.index = -1
        self.wave = False
        self.station = WAVE_STATION
        self._batch_id: Optional[str] = None
        self._loaded = False
        self._consecutive_track_failures = 0

        self._lock = threading.RLock()
        self._manual_transition = threading.Event()
        self._watcher_stop = threading.Event()
        # Свежий Event на каждый переход (см. _advance): позволяет отменить
        # ретрай-цикл именно предыдущего трека, не трогая общее состояние.
        self._cancel_event = threading.Event()
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
        self.next()

    def start_wave(self, station: str = WAVE_STATION) -> None:
        with self._lock:
            self.wave = True
            self.station = station
            self.queue = []
            self.index = -1
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
        self._advance(direction=1, auto=False)

    def prev(self) -> None:
        if self.wave:
            return
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
        self._cancel_event.set()
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
        logger.debug(
            "_advance start direction=%s auto=%s index=%s queue_len=%s wave=%s",
            direction, auto, self.index, len(self.queue), self.wave,
        )
        with self._lock:
            # Начинается новый переход — отменяем ретрай-цикл предыдущего
            # трека (если он ещё крутится) и заводим для нового отдельный
            # cancel_event, чтобы отмена не зависела от общего состояния.
            self._cancel_event.set()

            prev_track = self.current()
            if prev_track is not None and self.wave:
                played, dur = self.position()
                logger.debug(
                    "wave feedback track_id=%s played=%.1f/%.1f auto=%s",
                    prev_track.track_id, played, dur, auto,
                )
                try:
                    if auto:
                        self.client.rotor_station_feedback_track_finished(
                            self.station, prev_track.track_id, played, batch_id=self._batch_id
                        )
                    else:
                        self.client.rotor_station_feedback_skip(
                            self.station, prev_track.track_id, played, batch_id=self._batch_id
                        )
                except Exception as e:
                    logger.debug("wave feedback failed: %s", e)

            self.index += direction
            if self.index < 0:
                self.index = 0
                logger.debug("_advance: hit start of queue, index reset to 0")
                return

            if self.wave and self.index >= len(self.queue) - 2:
                try:
                    self._fetch_wave_batch()
                    logger.debug(
                        "wave batch fetched, queue_len now=%s batch_id=%s",
                        len(self.queue), self._batch_id,
                    )
                except Exception as e:
                    logger.debug("wave batch fetch failed: %s", e)
                    self._report_error(f"Failed to continue the wave: {e}")

            if self.index >= len(self.queue):
                self.index = len(self.queue)
                logger.debug(
                    "_advance: queue exhausted (index=%s queue_len=%s), stopping",
                    self.index, len(self.queue),
                )
                self.on_track_change and self.on_track_change(None)
                return

            track = self.current()
            cancel_event = threading.Event()
            self._cancel_event = cancel_event
            logger.debug(
                "_advance: moving to index=%s track=%r cancel_event=%s",
                self.index, track.title, id(cancel_event),
            )

        # Играем уже вне self._lock, чтобы next()/prev()/stop() могли
        # прервать ретрай-цикл, а не ждать его на входе в лок.
        self._play_current(track, cancel_event, auto=auto)

    def _retry_until_success(self, cancel_event: threading.Event, description: str, fn):
        """Повторяет fn() с растущей паузой между попытками, пока не
        получится, пока не наберётся MAX_ATTEMPTS_PER_TRACK неудач подряд
        (тогда трек считается сломанным — см. _GiveUp), пока не отменят
        (новый переход/next/prev/stop) или пока плеер не остановлен целиком.
        Пауза растёт от RETRY_INITIAL_DELAY до RETRY_MAX_DELAY (не больше)."""
        delay = RETRY_INITIAL_DELAY
        attempt = 0
        while not (cancel_event.is_set() or self._watcher_stop.is_set()):
            attempt += 1
            try:
                result = fn()
                logger.debug("%s: succeeded on attempt %s", description, attempt)
                return result
            except Exception as e:
                logger.debug("%s: attempt %s failed: %s", description, attempt, e)
                if attempt >= MAX_ATTEMPTS_PER_TRACK:
                    logger.debug("%s: giving up after %s attempt(s)", description, attempt)
                    raise _GiveUp(str(e)) from e
                self._report_error(f"{description}: {e}. Retrying in {delay:.0f}s...")
                if cancel_event.wait(timeout=delay):
                    logger.debug("%s: cancelled during backoff wait (attempt %s)", description, attempt)
                    break
                delay = min(delay * 2, RETRY_MAX_DELAY)
        logger.debug("%s: abandoned after %s attempt(s)", description, attempt)
        raise _Abandoned

    def _play_current(self, track: Track, cancel_event: threading.Event, auto: bool) -> None:
        logger.debug("_play_current: track=%r auto=%s cancel_event=%s", track.title, auto, id(cancel_event))
        try:
            info = self._retry_until_success(
                cancel_event, f"Failed to get a link for “{track.title}”",
                lambda: best_download_info(track),
            )

            def _start() -> None:
                # Флаг нужен только когда mpv.play() реально прерывает ещё
                # играющий трек (ручной next/prev) — тогда это порождает
                # лишний end_file, который должен проигнорировать watcher.
                # При авто-переходе (natural EOF) предыдущий end_file уже
                # обработан и mpv простаивает, так что взводить флаг не
                # нужно — иначе он зависнет и watcher примет за «ручной»
                # уже собственный end_file нового трека, и авто-переход
                # после него больше никогда не сработает.
                if self._loaded and not auto:
                    logger.debug("_play_current: arming manual_transition before mpv.play()")
                    self._manual_transition.set()
                self.mpv.play(info.direct_link)
                self.mpv.pause = False

            self._retry_until_success(
                cancel_event, f"Failed to start playback of “{track.title}”", _start,
            )
        except _Abandoned:
            logger.debug("_play_current: transition for %r abandoned (superseded or shutting down)", track.title)
            return
        except _GiveUp as e:
            self._consecutive_track_failures += 1
            logger.debug(
                "_play_current: giving up on %r (%s), consecutive_track_failures=%s",
                track.title, e, self._consecutive_track_failures,
            )
            self._report_error(f"Skipping “{track.title}”: {e}")
            if self._consecutive_track_failures >= MAX_CONSECUTIVE_TRACK_FAILURES:
                logger.debug("_play_current: too many broken tracks in a row, stopping")
                self._report_error("Too many consecutive errors, stopping.")
                self._consecutive_track_failures = 0
                return
            # auto прокидывается как есть, а не хардкодится в True: если это
            # был ручной next/prev и старый трек ещё реально играет в mpv,
            # следующая попытка тоже должна прервать его как «ручную», иначе
            # watcher примет лишний end_file от mpv.play() за настоящий конец
            # трека и сделает случайный двойной автопереход.
            self._advance(direction=1, auto=auto)
            return

        self._consecutive_track_failures = 0
        self._loaded = True
        logger.debug("_play_current: mpv.play() issued for %r, waiting on eof watcher now", track.title)

        if self.wave:
            try:
                self.client.rotor_station_feedback_track_started(
                    self.station, track.track_id, batch_id=self._batch_id
                )
            except Exception as e:
                logger.debug("wave feedback (track_started) failed: %s", e)

        if self.on_track_change:
            self.on_track_change(track)

    def _watch_eof(self) -> None:
        logger.debug("_watch_eof: watcher thread started")
        while not self._watcher_stop.is_set():
            try:
                self.mpv.wait_for_playback()
            except Exception as e:
                logger.debug(
                    "_watch_eof: wait_for_playback() raised %r, watcher thread is exiting "
                    "and no further auto-advance will happen",
                    e, exc_info=True,
                )
                self._report_error(f"Playback watcher died: {e}")
                return
            logger.debug(
                "_watch_eof: wait_for_playback() returned (end_file), manual_transition=%s stop=%s",
                self._manual_transition.is_set(), self._watcher_stop.is_set(),
            )
            if self._watcher_stop.is_set():
                return
            if self._manual_transition.is_set():
                self._manual_transition.clear()
                logger.debug("_watch_eof: consumed manual_transition flag, skipping auto-advance")
                continue
            try:
                self._advance(direction=1, auto=True)
            except Exception as e:
                logger.debug("_watch_eof: auto-advance raised %r", e, exc_info=True)
                self._report_error(f"Auto-advance error: {e}")
