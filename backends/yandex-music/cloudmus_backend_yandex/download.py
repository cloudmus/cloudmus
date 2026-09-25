"""catalog.downloadTrack — ported from ym_player/downloader.py, thin wrapper
around a single-track download+tag operation."""
from __future__ import annotations

import asyncio
import re
from pathlib import Path

from mutagen.id3 import APIC, ID3, TALB, TIT2, TPE1
from yandex_music import Client

from . import quality


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


def _download_track_sync(client: Client, track_id: str, dest_dir: Path, quality_level: str) -> Path:
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

    info.download(str(path))
    _tag_file(path, track)
    return path


async def download_track(client: Client, track_id: str, dest_dir: str, quality_level: str = quality.BEST) -> dict:
    path = await asyncio.to_thread(_download_track_sync, client, track_id, Path(dest_dir), quality_level)
    return {"path": str(path.resolve())}
