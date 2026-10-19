"""playback.play stream resolution: yt-dlp lists the audio-only formats
of a YouTube videoId, quality.pick() chooses one -> StreamDescriptor, delivered via
track/streamReady. No ytmusicapi client involved here at all — a public
video id is resolvable without an authenticated session, so playback.play
works even in a logged-out session for a track id obtained elsewhere.

Retry/backoff loop structurally mirrors cloudmus_backend_yandex/playback.py's
resolve_stream_with_retry, but with a smaller attempt budget and a fail-fast
branch: yt-dlp extraction is a slower per-attempt operation (~1-3s, an HTTP
round trip plus parsing YouTube's player response) than yandex's near-instant
API call, and a "video pulled/private/geo-blocked" failure is not transient,
so retrying it is pure wasted latency against the front's ~10s playback.play
timeout (docs/protocol.md §11.1).
"""
from __future__ import annotations

import asyncio
import threading
import time

import requests
import yt_dlp

from rpc_common.errors import OfflineError
from rpc_common.generated.models import StreamDescriptor

from . import quality

RETRY_INITIAL_DELAY = 1.0
RETRY_MAX_DELAY = 4.0
MAX_ATTEMPTS = 3
# With the network down every attempt fails the same way, so one more try
# (a blip) is all it gets before the track is reported as offline.
OFFLINE_ATTEMPTS = 2
# A YoutubeDL session YouTube has flagged keeps getting URLs that
# googlevideo answers with 403, until the process restarts — so it's
# thrown away after this long even when nothing failed.
YDL_MAX_AGE = 30 * 60
VALIDATE_TIMEOUT = 5.0
# A flagged session's URLs serve the first ~1 MiB and answer 403 to any
# byte past it, so the check has to ask for one that lies beyond. Used
# when the format's size is unknown; a stream shorter than this answers
# 416, which is not a refusal.
VALIDATE_OFFSET = 2 * 1024 * 1024
# googlevideo holds each connection to about playback pace; a few ranges
# fetched at once get a track down that much sooner.
STREAM_CONNECTIONS = 4

_YDL_OPTS = {
    "format": "bestaudio/best",
    "noplaylist": True,
    "quiet": True,
    "no_warnings": True,
    "skip_download": True,
}

_MIME_TYPES = {"m4a": "audio/mp4", "webm": "audio/webm", "opus": "audio/opus"}

def _codec_name(acodec: str | None) -> str | None:
    """yt-dlp's codec string ("opus", "mp4a.40.2") as the short name the
    protocol's StreamDescriptor.codec uses."""
    if not acodec or acodec == "none":
        return None
    return "aac" if acodec.startswith("mp4a") else acodec.split(".")[0]


_NON_TRANSIENT_SIGNATURES = (
    "video unavailable",
    "this video is not available",
    "private video",
    "copyright",
    "sign in to confirm",
    "not available in your country",
    "members-only",
)
# yt-dlp reports a dead network as a DownloadError wrapping the transport's
# own text; the signatures are what it (and the OS) say for it.
_OFFLINE_SIGNATURES = (
    "temporary failure in name resolution",
    "name or service not known",
    "failed to resolve",
    "getaddrinfo failed",
    "network is unreachable",
    "no route to host",
    "connection refused",
    "connection reset",
    "connection aborted",
    "timed out",
)


# One YoutubeDL at a time: a fresh one per track threw away its open
# connections and in-memory caches (player JS, client config), so every
# resolve paid the full connection setup again. YoutubeDL isn't
# thread-safe, and the current and the next track may resolve at once —
# hence the lock. It is dropped when it fails or turns out to hand out
# URLs googlevideo refuses, and after YDL_MAX_AGE (see above).
_ydl: yt_dlp.YoutubeDL | None = None
_ydl_created = 0.0
_ydl_lock = threading.Lock()


class StreamRejected(Exception):
    """googlevideo answered 403 to the URL yt-dlp just produced."""


def _drop_session() -> None:
    global _ydl
    with _ydl_lock:
        _ydl = None


def _extract_info(video_id: str) -> dict:
    """Sole yt_dlp seam, kept as its own function so tests can monkeypatch it
    without touching the network."""
    global _ydl, _ydl_created
    with _ydl_lock:
        if _ydl is None or time.monotonic() - _ydl_created > YDL_MAX_AGE:
            _ydl = yt_dlp.YoutubeDL(_YDL_OPTS)
            _ydl_created = time.monotonic()
        try:
            return _ydl.extract_info(f"https://music.youtube.com/watch?v={video_id}", download=False)
        except Exception:
            _ydl = None
            raise


def _probe_range(size: int | None) -> str:
    """One byte to ask for: the stream's last when its size is known (a
    flagged session's URL refuses it in any stream longer than ~1 MiB),
    else the one at VALIDATE_OFFSET."""
    offset = size - 1 if size and size > 0 else VALIDATE_OFFSET
    return f"bytes={offset}-{offset}"


def _stream_accepted(url: str, headers: dict | None, size: int | None = None) -> bool:
    """False only when the server says 403. Any other outcome (a network
    hiccup, a 5xx) is not proof the URL is bad, and must not block playback.
    Own seam so tests don't touch the network. requests takes the proxy
    from the environment, the same one the front's relay uses."""
    try:
        with requests.get(
            url,
            headers={**(headers or {}), "Range": _probe_range(size)},
            stream=True,
            timeout=VALIDATE_TIMEOUT,
        ) as response:
            return response.status_code != 403
    except requests.RequestException:
        return True


def _resolve_stream_sync(video_id: str, quality_level: str) -> StreamDescriptor:
    info = _extract_info(video_id)
    formats = info.get("formats") or []
    audio_formats = [
        f for f in formats if f.get("acodec") not in (None, "none") and f.get("vcodec") in (None, "none")
    ]
    if not audio_formats:
        raise LookupError(f"no audio-only formats for {video_id}")
    best = quality.pick(audio_formats, quality_level)
    mime = _MIME_TYPES.get(best.get("ext"), "audio/mp4")
    headers = best.get("http_headers") or info.get("http_headers") or None
    if not _stream_accepted(best["url"], headers, best.get("filesize")):
        _drop_session()
        raise StreamRejected(f"googlevideo refused the stream URL for {video_id} (403)")
    bitrate = round(best.get("abr") or best.get("tbr") or 0)
    return StreamDescriptor(
        kind="url",
        url=best["url"],
        mimeType=mime,
        headers=headers,
        codec=_codec_name(best.get("acodec")),
        bitrateKbps=bitrate or None,
        maxConnections=STREAM_CONNECTIONS,
    )


def _is_non_transient(exc: Exception) -> bool:
    message = str(exc).lower()
    return any(sig in message for sig in _NON_TRANSIENT_SIGNATURES)


def _is_offline(exc: Exception) -> bool:
    """True for a failure of the transport itself, not of the service's
    answer: yt-dlp's TransportError, or the OS's network errors, anywhere
    in the chain, or their well-known text."""
    seen: set[int] = set()
    cur: BaseException | None = exc
    while cur is not None and id(cur) not in seen:
        seen.add(id(cur))
        if isinstance(cur, (yt_dlp.networking.exceptions.TransportError, requests.ConnectionError, OSError)):
            return True
        inner = getattr(cur, "exc_info", None)
        cur = cur.__cause__ or cur.__context__ or (inner[1] if inner else None)
    message = str(exc).lower()
    return any(sig in message for sig in _OFFLINE_SIGNATURES)


async def resolve_stream_with_retry(
    video_id: str, cancel_event: asyncio.Event, quality_level: str = quality.BEST
) -> StreamDescriptor:
    delay = RETRY_INITIAL_DELAY
    last_exc: Exception | None = None
    offline_failures = 0
    for attempt in range(1, MAX_ATTEMPTS + 1):
        if cancel_event.is_set():
            raise asyncio.CancelledError
        try:
            return await asyncio.to_thread(_resolve_stream_sync, video_id, quality_level)
        except LookupError:
            raise
        except Exception as e:
            last_exc = e
            if isinstance(e, yt_dlp.utils.DownloadError) and _is_non_transient(e):
                raise LookupError(f"video unavailable: {video_id}") from e
            if not isinstance(e, StreamRejected) and _is_offline(e):
                offline_failures += 1
                if offline_failures >= OFFLINE_ATTEMPTS:
                    raise OfflineError(str(e)) from e
            if attempt == MAX_ATTEMPTS:
                break
            try:
                await asyncio.wait_for(cancel_event.wait(), timeout=delay)
                raise asyncio.CancelledError  # cancel_event was set during the backoff wait
            except asyncio.TimeoutError:
                pass
            delay = min(delay * 2, RETRY_MAX_DELAY)
    raise RuntimeError(f"failed after {MAX_ATTEMPTS} attempts: {last_exc}")
