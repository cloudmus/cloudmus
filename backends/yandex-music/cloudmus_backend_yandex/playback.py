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

from rpc_common.models import StreamDescriptor

RETRY_INITIAL_DELAY = 1.0
RETRY_MAX_DELAY = 5.0
MAX_ATTEMPTS = 5


def _resolve_stream_sync(client: Client, track_id: str) -> StreamDescriptor:
    tracks = client.tracks([track_id])
    if not tracks:
        raise LookupError(f"track not found: {track_id}")
    track = tracks[0]
    infos = track.get_download_info(get_direct_links=True)
    if not infos:
        raise RuntimeError(f"no download links available for track {track_id}")
    mp3_infos = [i for i in infos if i.codec == "mp3"] or infos
    best = max(mp3_infos, key=lambda i: i.bitrate_in_kbps)
    return StreamDescriptor(kind="url", url=best.direct_link, mimeType="audio/mpeg")


async def resolve_stream_with_retry(
    client: Client, track_id: str, cancel_event: asyncio.Event
) -> StreamDescriptor:
    delay = RETRY_INITIAL_DELAY
    last_exc: Exception | None = None
    for attempt in range(1, MAX_ATTEMPTS + 1):
        if cancel_event.is_set():
            raise asyncio.CancelledError
        try:
            return await asyncio.to_thread(_resolve_stream_sync, client, track_id)
        except Exception as e:
            last_exc = e
            if attempt == MAX_ATTEMPTS:
                break
            try:
                await asyncio.wait_for(cancel_event.wait(), timeout=delay)
                raise asyncio.CancelledError  # cancel_event was set during the backoff wait
            except asyncio.TimeoutError:
                pass
            delay = min(delay * 2, RETRY_MAX_DELAY)
    raise RuntimeError(f"failed after {MAX_ATTEMPTS} attempts: {last_exc}")
