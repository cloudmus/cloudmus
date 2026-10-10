"""Download progress and cancelling (docs/protocol.md §7.5, capability
downloadControl, 1.7+), shared by every backend that downloads.

A backend's catalog.downloadTrack runs the transfer on a worker thread
(asyncio.to_thread); `Downloads.track()` hands it a `Tracker` for the
front's `downloadId`, which the transfer calls as it goes: `progress()`
sends download/progress (from the worker thread, throttled), and raises
`Cancelled` once the front has asked for catalog.cancelDownload — so the
transfer stops at its next chunk. `serve()` wires both methods onto a
Server."""
from __future__ import annotations

import asyncio
import shutil
import threading
import time
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Awaitable, Callable, Iterator

from . import errors
from .generated.methods import emit_download_progress
from .generated.models import DownloadProgressParams
from .i18n import Translator
from .server import BackendError

NotifyFn = Callable[[str, dict[str, Any]], Awaitable[None]]

# download/progress no more often than this — "a few times a second".
PROGRESS_INTERVAL_S = 0.25


class Cancelled(Exception):
    """The front cancelled the download (catalog.cancelDownload)."""


class Tracker:
    """One download's side of it, used from the worker thread. A download
    without a downloadId gets one too — it just reports nothing and is
    never cancelled."""

    def __init__(self, download_id: str | None, notify: NotifyFn | None, loop: asyncio.AbstractEventLoop | None):
        self.download_id = download_id
        self._notify = notify
        self._loop = loop
        self._cancelled = threading.Event()
        self._last_sent = 0.0

    @property
    def cancelled(self) -> bool:
        return self._cancelled.is_set()

    def cancel(self) -> None:
        self._cancelled.set()

    def check(self) -> None:
        """Raises Cancelled if the download was cancelled."""
        if self._cancelled.is_set():
            raise Cancelled()

    def progress(self, received: int, total: int | None, *, final: bool = False) -> None:
        """Reports how far the transfer has got (throttled, unless
        `final`), then check()s."""
        self.check()
        if self.download_id is None or self._notify is None or self._loop is None:
            return
        now = time.monotonic()
        if not final and now - self._last_sent < PROGRESS_INTERVAL_S:
            return
        self._last_sent = now
        params = DownloadProgressParams(
            downloadId=self.download_id,
            receivedBytes=int(received),
            totalBytes=int(total) if total else None,
        )
        asyncio.run_coroutine_threadsafe(emit_download_progress(self._notify, params), self._loop)


def audio_extension(path: Path) -> str | None:
    """The file extension for the audio in `path`, told from its first
    bytes; None if it isn't a format a backend here saves. A sourceFile
    (docs/protocol.md §7.5, 1.12+) has no name to go by."""
    try:
        with open(path, "rb") as f:
            head = f.read(12)
    except OSError:
        return None
    if head[4:8] == b"ftyp":
        return ".m4a"
    if head[:4] == b"\x1a\x45\xdf\xa3":
        return ".webm"
    if head[:4] == b"OggS":
        return ".opus"
    if head[:4] == b"fLaC":
        return ".flac"
    if head[:3] == b"ID3" or (len(head) >= 2 and head[0] == 0xFF and head[1] & 0xE0 == 0xE0):
        return ".mp3"
    return None


def save_from_source_file(source_file: str | None, dest_stem: Path, tracker: Tracker) -> Path | None:
    """Saves a track from the audio the front already has (catalog.
    downloadTrack's sourceFile, capability downloadFromFile): copies it to
    `dest_stem` plus the extension its content calls for, and returns that
    path — or None when there is nothing usable to copy, for the caller to
    download as usual. The copy is the front's file byte for byte; naming
    it and tagging the result stay the caller's."""
    if not source_file:
        return None
    source = Path(source_file)
    extension = audio_extension(source)
    if extension is None:
        return None
    tracker.check()
    path = dest_stem.with_name(dest_stem.name + extension)
    try:
        shutil.copyfile(source, path)
    except OSError:
        path.unlink(missing_ok=True)
        return None
    size = path.stat().st_size
    tracker.progress(size, size, final=True)
    return path


class Downloads:
    """The downloads in flight, by the front's downloadId."""

    def __init__(self, notify: NotifyFn, translator: Translator | None = None):
        self._notify = notify
        self._tr = (translator or Translator()).tr
        self._trackers: dict[str, Tracker] = {}

    @contextmanager
    def track(self, download_id: str | None) -> Iterator[Tracker]:
        """A Tracker for the duration of one catalog.downloadTrack call;
        turns a Cancelled out of it into the protocol's error 1410."""
        tracker = Tracker(download_id, self._notify, asyncio.get_running_loop())
        if download_id is not None:
            self._trackers[download_id] = tracker
        try:
            yield tracker
        except Cancelled:
            raise BackendError(
                errors.DOWNLOAD_CANCELLED, self._tr("Download cancelled"), errors.app_error_data(retryable=False)
            )
        finally:
            if download_id is not None and self._trackers.get(download_id) is tracker:
                del self._trackers[download_id]

    def cancel(self, download_id: str) -> None:
        # Unknown or finished already: nothing to do, not an error (§7.5).
        tracker = self._trackers.get(download_id)
        if tracker is not None:
            tracker.cancel()


def serve(server: Any, downloads: Downloads) -> None:
    """Registers catalog.cancelDownload on `server`."""

    @server.method("catalog.cancelDownload")
    def handle_cancel_download(params: dict, request_id: int) -> dict:
        downloads.cancel(params["downloadId"])
        return {}
