"""playback.play stream resolution: get_download_info(get_direct_links=True)
-> StreamDescriptor, delivered via track/streamReady. No mpv import here at
all — that engine now lives entirely on the front side.

The retry-with-backoff loop is ported from ym_player/player.py's
best_download_info()/_retry_until_success(): resolving a Yandex direct link
occasionally blips transiently, so retry a handful of times with growing
backoff before giving up, instead of surfacing the very first failure.
"""
from __future__ import annotations

import asyncio

from yandex_music import Client
from yandex_music.exceptions import TimedOutError

from rpc_common.errors import OfflineError
from rpc_common.generated.models import StreamDescriptor

from . import quality

RETRY_INITIAL_DELAY = 1.0
RETRY_MAX_DELAY = 5.0
MAX_ATTEMPTS = 5
# With the network down every attempt fails the same way: one more try (a
# blip) and the track is reported as offline, not after the full backoff.
OFFLINE_ATTEMPTS = 2


class TrackUnavailable(LookupError):
    """Yandex itself marks the track as not playable (removed, blocked in the
    region): retrying cannot change that. A plain LookupError from an empty
    answer stays retried — it was always a possible blip."""


def _resolve_stream_sync(client: Client, track_id: str, quality_level: str) -> StreamDescriptor:
    tracks = client.tracks([track_id])
    if not tracks:
        raise LookupError(f"track not found: {track_id}")
    track = tracks[0]
    if getattr(track, "available", True) is False:
        raise TrackUnavailable(f"track is not available: {track_id}")
    infos = track.get_download_info(get_direct_links=True)
    if not infos:
        raise RuntimeError(f"no download links available for track {track_id}")
    info = quality.pick(infos, quality_level)
    mime_type = "audio/mpeg" if info.codec == "mp3" else f"audio/{info.codec}"
    return StreamDescriptor(
        kind="url",
        url=info.direct_link,
        mimeType=mime_type,
        codec=info.codec,
        bitrateKbps=info.bitrate_in_kbps or None,
    )


def _is_offline(exc: BaseException) -> bool:
    """yandex_music wraps a failed connection as NetworkError(requests'
    exception) and a timeout as TimedOutError; an HTTP status becomes
    BadRequestError/NotFoundError, which are the service's answer."""
    if isinstance(exc, TimedOutError):
        return True
    cause = exc.__cause__
    return isinstance(cause, OSError)  # requests.RequestException is an OSError


async def resolve_stream_with_retry(
    client: Client, track_id: str, cancel_event: asyncio.Event, quality_level: str = quality.BEST
) -> StreamDescriptor:
    delay = RETRY_INITIAL_DELAY
    last_exc: Exception | None = None
    offline_failures = 0
    for attempt in range(1, MAX_ATTEMPTS + 1):
        if cancel_event.is_set():
            raise asyncio.CancelledError
        try:
            return await asyncio.to_thread(_resolve_stream_sync, client, track_id, quality_level)
        except TrackUnavailable:
            raise
        except Exception as e:
            last_exc = e
            if _is_offline(e):
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
