"""playback.play stream resolution: yt-dlp extracts the best audio-only
format for a YouTube videoId -> StreamDescriptor, delivered via
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

import yt_dlp

from rpc_common.generated.models import StreamDescriptor

RETRY_INITIAL_DELAY = 1.0
RETRY_MAX_DELAY = 4.0
MAX_ATTEMPTS = 3

_YDL_OPTS = {
    "format": "bestaudio/best",
    "noplaylist": True,
    "quiet": True,
    "no_warnings": True,
    "skip_download": True,
}

_MIME_TYPES = {"m4a": "audio/mp4", "webm": "audio/webm", "opus": "audio/opus"}

_NON_TRANSIENT_SIGNATURES = ("video unavailable", "private video", "copyright", "sign in to confirm")


def _extract_info(video_id: str) -> dict:
    """Sole yt_dlp seam, kept as its own function so tests can monkeypatch it
    without touching the network."""
    with yt_dlp.YoutubeDL(_YDL_OPTS) as ydl:
        return ydl.extract_info(f"https://music.youtube.com/watch?v={video_id}", download=False)


def _resolve_stream_sync(video_id: str) -> StreamDescriptor:
    info = _extract_info(video_id)
    formats = info.get("formats") or []
    audio_formats = [
        f for f in formats if f.get("acodec") not in (None, "none") and f.get("vcodec") in (None, "none")
    ]
    if not audio_formats:
        raise LookupError(f"no audio-only formats for {video_id}")
    best = max(audio_formats, key=lambda f: f.get("abr") or 0)
    mime = _MIME_TYPES.get(best.get("ext"), "audio/mp4")
    headers = best.get("http_headers") or info.get("http_headers") or None
    return StreamDescriptor(kind="url", url=best["url"], mimeType=mime, headers=headers)


def _is_non_transient(exc: Exception) -> bool:
    message = str(exc).lower()
    return any(sig in message for sig in _NON_TRANSIENT_SIGNATURES)


async def resolve_stream_with_retry(video_id: str, cancel_event: asyncio.Event) -> StreamDescriptor:
    delay = RETRY_INITIAL_DELAY
    last_exc: Exception | None = None
    for attempt in range(1, MAX_ATTEMPTS + 1):
        if cancel_event.is_set():
            raise asyncio.CancelledError
        try:
            return await asyncio.to_thread(_resolve_stream_sync, video_id)
        except LookupError:
            raise
        except Exception as e:
            last_exc = e
            if isinstance(e, yt_dlp.utils.DownloadError) and _is_non_transient(e):
                raise LookupError(f"video unavailable: {video_id}") from e
            if attempt == MAX_ATTEMPTS:
                break
            try:
                await asyncio.wait_for(cancel_event.wait(), timeout=delay)
                raise asyncio.CancelledError  # cancel_event was set during the backoff wait
            except asyncio.TimeoutError:
                pass
            delay = min(delay * 2, RETRY_MAX_DELAY)
    raise RuntimeError(f"failed after {MAX_ATTEMPTS} attempts: {last_exc}")
