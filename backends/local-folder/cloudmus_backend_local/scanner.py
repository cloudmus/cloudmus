"""Scans a local music directory tree into playlists + tracks.

Rule (per the user's request): if the configured root contains
subdirectories, each subdirectory becomes one playlist (recursively
collecting all audio files under it); loose files directly in the root are
also exposed, as an implicit "Root" playlist, so nothing in the tree is
silently dropped. If the root has no subdirectories at all, its files are
exposed as a single playlist.
"""
from __future__ import annotations

import mimetypes
from dataclasses import dataclass
from pathlib import Path

from mutagen import File as MutagenFile
from rpc_common.generated.models import Album, Artist, Track

from . import cover_art

AUDIO_EXTENSIONS = {".mp3", ".flac", ".ogg", ".m4a", ".wav", ".opus", ".wma", ".aac"}

ROOT_PLAYLIST_ID = "__root__"


def is_audio_file(path: Path) -> bool:
    return path.is_file() and path.suffix.lower() in AUDIO_EXTENSIONS


@dataclass
class PlaylistInfo:
    id: str
    title: str
    files: list[Path]


def scan(root: Path) -> list[PlaylistInfo]:
    if not root.exists() or not root.is_dir():
        return []

    entries = sorted(root.iterdir())
    subdirs = [e for e in entries if e.is_dir()]
    loose_files = sorted(e for e in entries if is_audio_file(e))

    playlists: list[PlaylistInfo] = []
    if subdirs:
        for subdir in subdirs:
            files = sorted(p for p in subdir.rglob("*") if is_audio_file(p))
            if files:
                playlists.append(
                    PlaylistInfo(id=subdir.relative_to(root).as_posix(), title=subdir.name, files=files)
                )
        if loose_files:
            playlists.append(PlaylistInfo(id=ROOT_PLAYLIST_ID, title=root.name or "Root", files=loose_files))
    elif loose_files:
        playlists.append(PlaylistInfo(id=ROOT_PLAYLIST_ID, title=root.name or "Library", files=loose_files))

    return playlists


def find_playlist(root: Path, playlist_id: str) -> PlaylistInfo | None:
    for playlist in scan(root):
        if playlist.id == playlist_id:
            return playlist
    return None


def read_track(path: Path, root: Path) -> Track:
    track_id = path.relative_to(root).as_posix()
    audio = None
    try:
        audio = MutagenFile(path, easy=True)
    except Exception:
        audio = None

    tags = (audio.tags or {}) if audio is not None else {}
    title = (tags.get("title") or [path.stem])[0]
    artist_names = tags.get("artist") or ["Unknown"]
    album_name = (tags.get("album") or [None])[0]
    duration_ms = int((audio.info.length if audio is not None and audio.info else 0) * 1000)

    album = Album(id=album_name, title=album_name) if album_name else None
    artists = [Artist(id=name, name=name) for name in artist_names]
    cover_url = cover_art.extract_cover_uri(path)

    return Track(id=track_id, title=title, artists=artists, durationMs=duration_ms, album=album, coverUrl=cover_url)


def resolve_track_path(root: Path, track_id: str) -> Path | None:
    """Resolves a track id (a root-relative path) back to an absolute path,
    rejecting anything that would escape the configured root directory.
    """
    candidate = (root / track_id).resolve()
    root_resolved = root.resolve()
    if root_resolved not in candidate.parents and candidate != root_resolved:
        return None
    if not candidate.is_file():
        return None
    return candidate


def guess_mime_type(path: Path) -> str:
    mime, _ = mimetypes.guess_type(str(path))
    return mime or "application/octet-stream"
