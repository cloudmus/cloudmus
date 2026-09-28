"""catalog.downloadTrack — ported from ym_player/downloader.py, thin wrapper
around a single-track download+tag operation."""
from __future__ import annotations

import asyncio
import re
from pathlib import Path

from mutagen.id3 import APIC, ID3, TALB, TIT2, TPE1
from yandex_music import Client

from rpc_common.downloads import Tracker

from . import http_session, quality

# Read and reported in pieces of this size — small enough that a cancel
# lands quickly, big enough not to spend the time in Python.
CHUNK_BYTES = 64 * 1024


def _safe_name(name: str) -> str:
    return re.sub(r'[\\/:*?"<>|]', "_", name).strip()


def _tag_file(path: Path, track) -> None:
    try:
        tags = ID3()
        tags["TIT2"] = TIT2(encoding=3, text=track.title or "")
        tags["TPE1"] = TPE1(encoding=3, text=track.artists_name() or [])
        if track.albums:
            tags["TALB"] = TALB(encoding=3, text=track.albums[0].title or "")
        cover = track.download_cover_bytes(size="400x400")
        if cover:
            tags["APIC"] = APIC(encoding=3, mime="image/jpeg", type=3, desc="Cover", data=cover)
        tags.save(path)
    except Exception:
        pass  # tagging is best-effort; a missing/partial tag isn't fatal


def _fetch(client: Client, url: str, path: Path, tracker: Tracker) -> None:
    """Streams `url` into `path`, reporting progress; a cancel (or any
    failure) leaves no partial file behind. Same headers and proxies as
    the library's own Request.download(), which reads the whole file in one
    go and so can report nothing."""
    request = client.request
    try:
        with http_session.session().get(
            url, headers=request.headers, proxies=request.proxies, stream=True, timeout=30
        ) as response:
            response.raise_for_status()
            total = int(response.headers.get("Content-Length") or 0) or None
            received = 0
            with open(path, "wb") as out:
                for chunk in response.iter_content(CHUNK_BYTES):
                    out.write(chunk)
                    received += len(chunk)
                    tracker.progress(received, total)
            tracker.progress(received, total, final=True)
    except BaseException:
        path.unlink(missing_ok=True)
        raise


def _download_track_sync(
    client: Client, track_id: str, dest_dir: Path, quality_level: str, tracker: Tracker
) -> Path:
    tracks = client.tracks([track_id])
    if not tracks:
        raise LookupError(f"track not found: {track_id}")
    track = tracks[0]

    infos = track.get_download_info()
    if not infos:
        raise RuntimeError(f"no download links available for track {track_id}")
    info = quality.pick(infos, quality_level)

    dest_dir.mkdir(parents=True, exist_ok=True)
    artists = ", ".join(track.artists_name()) if track.artists_name() else "Unknown"
    extension = ".mp3" if info.codec == "mp3" else ".m4a"
    filename = _safe_name(f"{artists} - {track.title}") + extension
    path = dest_dir / filename

    tracker.check()
    _fetch(client, info.get_direct_link(), path, tracker)
    _tag_file(path, track)
    return path


async def download_track(
    client: Client, track_id: str, dest_dir: str, tracker: Tracker, quality_level: str = quality.BEST
) -> dict:
    path = await asyncio.to_thread(_download_track_sync, client, track_id, Path(dest_dir), quality_level, tracker)
    return {"path": str(path.resolve())}
