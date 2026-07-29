"""catalog.downloadTrack — ported from ym_player/downloader.py, thin wrapper
around a single-track download+tag operation."""
from __future__ import annotations

import asyncio
import re
from pathlib import Path

from mutagen.id3 import APIC, ID3, TALB, TIT2, TPE1
from yandex_music import Client


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


def _download_track_sync(client: Client, track_id: str, dest_dir: Path) -> Path:
    tracks = client.tracks([track_id])
    if not tracks:
        raise LookupError(f"track not found: {track_id}")
    track = tracks[0]

    dest_dir.mkdir(parents=True, exist_ok=True)
    artists = ", ".join(track.artists_name()) if track.artists_name() else "Unknown"
    filename = _safe_name(f"{artists} - {track.title}") + ".mp3"
    path = dest_dir / filename

    track.download(str(path), codec="mp3", bitrate_in_kbps=320)
    _tag_file(path, track)
    return path


async def download_track(client: Client, track_id: str, dest_dir: str) -> dict:
    path = await asyncio.to_thread(_download_track_sync, client, track_id, Path(dest_dir))
    return {"path": str(path.resolve())}
