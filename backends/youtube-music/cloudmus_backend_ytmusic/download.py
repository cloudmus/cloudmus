"""catalog.downloadTrack — downloads the resolved best-audio format via
yt-dlp directly to disk (same extraction seam family as playback.py, just
with download=True instead of skip_download), then tags the result from
yt-dlp's own extracted metadata. No ytmusicapi call involved: ytmusicapi
has no single-track lookup returning the same track-dict shape
catalog.to_track() expects (get_song() returns raw player-response JSON;
get_watch_playlist()'s track shape doesn't match either) — but yt-dlp's
YouTube extractor already parses YouTube Music-aware `track`/`artist`/
`album` fields (not just the raw, often-messy video title) for a Music URL,
which is all tagging needs.

No ffmpeg dependency: format is "bestaudio/best" with no postprocessors, so
this downloads whichever single already-encoded audio-only stream YouTube
serves (typically m4a, sometimes webm/opus) as-is — no merge/transcode step
needed since there's no video track, matching playback.py's existing
assumption that this backend needs no ffmpeg CLI at all.
"""
from __future__ import annotations

import asyncio
import re
from pathlib import Path

import yt_dlp
from mutagen.id3 import ID3, TALB, TIT2, TPE1
from mutagen.mp4 import MP4
from mutagen.oggopus import OggOpus

from .playback import _is_non_transient

_YDL_OPTS = {
    "format": "bestaudio/best",
    "noplaylist": True,
    "quiet": True,
    "no_warnings": True,
}


def _safe_name(name: str) -> str:
    return re.sub(r'[\\/:*?"<>|]', "_", name).strip()


def _tag_file(path: Path, info: dict) -> None:
    title = info.get("track") or info.get("title") or ""
    artist = info.get("artist") or info.get("uploader") or ""
    album = info.get("album") or ""
    try:
        if path.suffix == ".m4a":
            tags = MP4(path)
            tags["\xa9nam"] = [title]
            tags["\xa9ART"] = [artist]
            if album:
                tags["\xa9alb"] = [album]
            tags.save()
        elif path.suffix == ".opus":
            tags = OggOpus(path)
            tags["title"] = [title]
            tags["artist"] = [artist]
            if album:
                tags["album"] = [album]
            tags.save()
        elif path.suffix == ".mp3":
            tags = ID3()
            tags["TIT2"] = TIT2(encoding=3, text=title)
            tags["TPE1"] = TPE1(encoding=3, text=artist)
            if album:
                tags["TALB"] = TALB(encoding=3, text=album)
            tags.save(path)
        # else (e.g. .webm): no mutagen writer for this container — leave
        # the file untagged rather than fail the download over it.
    except Exception:
        pass  # tagging is best-effort; a missing/partial tag isn't fatal


def _extract_and_download(video_id: str, outtmpl: str) -> dict:
    """Sole yt_dlp seam, kept as its own function so tests can monkeypatch
    it without touching the network — mirrors playback.py's
    _extract_info(). Returns yt-dlp's info dict plus the actual path it
    wrote to (ydl.prepare_filename(info), computed while still inside the
    YoutubeDL context so any internal extension resolution has settled)."""
    opts = {**_YDL_OPTS, "outtmpl": outtmpl}
    with yt_dlp.YoutubeDL(opts) as ydl:
        info = ydl.extract_info(f"https://music.youtube.com/watch?v={video_id}", download=True)
        info["_filepath"] = ydl.prepare_filename(info)
        return info


def _download_track_sync(video_id: str, dest_dir: Path) -> Path:
    dest_dir.mkdir(parents=True, exist_ok=True)
    # Download under the plain video id first — sidesteps guessing how
    # yt-dlp's own outtmpl sanitizer would mangle a title/artist string,
    # and avoids any chance of the "actual" and "predicted" filenames
    # disagreeing. Renamed to a human-readable name below once the file
    # (and its real extension) actually exists.
    try:
        info = _extract_and_download(video_id, str(dest_dir / "%(id)s.%(ext)s"))
    except yt_dlp.utils.DownloadError as e:
        if _is_non_transient(e):
            raise LookupError(f"video unavailable: {video_id}") from e
        raise

    raw_path = Path(info["_filepath"])
    title = info.get("track") or info.get("title") or video_id
    artist = info.get("artist") or info.get("uploader") or "Unknown"
    final_path = dest_dir / (_safe_name(f"{artist} - {title}") + raw_path.suffix)
    raw_path.replace(final_path)
    _tag_file(final_path, info)
    return final_path


async def download_track(video_id: str, dest_dir: str) -> dict:
    path = await asyncio.to_thread(_download_track_sync, video_id, Path(dest_dir))
    return {"path": str(path.resolve())}
