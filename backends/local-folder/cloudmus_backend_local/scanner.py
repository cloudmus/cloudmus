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

from . import cover_art, matroska
from .i18n import tr

# What mpv plays and a music folder holds. Video containers (.mp4, .mkv,
# .mov, .avi) are left out: they are mostly video, and .webm/.weba/.mka are
# taken as audio. Matroska ones are read by matroska.py (mutagen has no
# reader for them); the rest by mutagen.
AUDIO_EXTENSIONS = {
    # the common ones
    ".mp3", ".flac", ".ogg", ".m4a", ".wav", ".opus", ".wma", ".aac",
    # Ogg, MPEG and MP4 variants
    ".oga", ".spx", ".mp2", ".m4b",
    # Matroska/WebM audio — what YouTube serves, and the player saves
    ".webm", ".weba", ".mka",
    # lossless and tracker-friendly containers
    ".aif", ".aiff", ".aifc", ".ape", ".wv", ".mpc", ".tta", ".dsf", ".dff",
    # Dolby
    ".ac3", ".eac3",
}

# For the descriptor's mimeType: what mimetypes guesses is wrong for some
# (video/webm for .webm) or nothing at all.
_MIME_TYPES = {
    ".webm": "audio/webm",
    ".weba": "audio/webm",
    ".mka": "audio/x-matroska",
    ".opus": "audio/ogg",
    ".oga": "audio/ogg",
    ".spx": "audio/ogg",
    ".m4a": "audio/mp4",
    ".m4b": "audio/mp4",
    ".aac": "audio/aac",
    ".flac": "audio/flac",
    ".mp2": "audio/mpeg",
    ".ape": "audio/x-ape",
    ".wv": "audio/x-wavpack",
    ".mpc": "audio/x-musepack",
    ".tta": "audio/x-tta",
    ".dsf": "audio/x-dsf",
    ".dff": "audio/x-dff",
    ".ac3": "audio/ac3",
    ".eac3": "audio/eac3",
    ".aif": "audio/aiff",
    ".aiff": "audio/aiff",
    ".aifc": "audio/aiff",
}

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
            playlists.append(PlaylistInfo(id=ROOT_PLAYLIST_ID, title=root.name or tr("Root"), files=loose_files))
    elif loose_files:
        playlists.append(PlaylistInfo(id=ROOT_PLAYLIST_ID, title=root.name or tr("Library"), files=loose_files))

    return playlists


def find_playlist(root: Path, playlist_id: str) -> PlaylistInfo | None:
    for playlist in scan(root):
        if playlist.id == playlist_id:
            return playlist
    return None


def read_track(path: Path, root: Path) -> Track:
    track_id = path.relative_to(root).as_posix()
    if path.suffix.lower() in matroska.EXTENSIONS:
        info = matroska.read(path)
        title = info.title or path.stem
        artist_names = info.artists or ["Unknown"]
        album_name = info.album
        duration_ms = int((info.duration_s or 0) * 1000)
        codec, bitrate = _matroska_stream_info(info, path)
    else:
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
        codec, bitrate = _mutagen_stream_info(getattr(audio, "info", None), path)

    album = Album(id=album_name, title=album_name) if album_name else None
    artists = [Artist(id=name, name=name) for name in artist_names]
    cover_url = cover_art.extract_cover_uri(path)

    return Track(
        id=track_id,
        title=title,
        artists=artists,
        durationMs=duration_ms,
        album=album,
        coverUrl=cover_url,
        codec=codec,
        bitrateKbps=bitrate,
    )


def read_stream_info(path: Path) -> tuple[str | None, int | None]:
    """(codec, bitrate in kbit/s) of an audio file, for the stream
    descriptor — either is None when it can't be told. The codec is the
    short lowercase name the protocol uses ("mp3", "flac", "opus")."""
    if path.suffix.lower() in matroska.EXTENSIONS:
        return _matroska_stream_info(matroska.read(path), path)
    try:
        audio = MutagenFile(path)
    except Exception:
        audio = None
    return _mutagen_stream_info(getattr(audio, "info", None), path)


def _matroska_stream_info(info: matroska.MatroskaInfo, path: Path) -> tuple[str | None, int | None]:
    try:
        size = path.stat().st_size
    except OSError:
        size = 0
    # The container's overhead is a few percent at most for audio alone.
    bitrate = size * 8 / info.duration_s if info.duration_s and size else 0
    return info.codec, round(bitrate / 1000) or None


def _mutagen_stream_info(info, path: Path) -> tuple[str | None, int | None]:
    bitrate = getattr(info, "bitrate", 0) or 0
    return _codec_of(info, path), round(bitrate / 1000) or None


def _codec_of(info, path: Path) -> str | None:
    kind = type(info).__name__
    if kind == "MPEGInfo":
        return "mp3" if getattr(info, "layer", 3) == 3 else "mp2"
    if kind == "MP4Info":
        codec = str(getattr(info, "codec", "") or "")
        return "aac" if codec.startswith("mp4a") else codec or "aac"
    named = {
        "FLACInfo": "flac",
        "OggFLACInfo": "flac",
        "OggVorbisInfo": "vorbis",
        "OggOpusInfo": "opus",
        "OggSpeexInfo": "speex",
        "ASFInfo": "wma",
        "WavPackInfo": "wavpack",
        "MonkeysAudioInfo": "ape",
        "MusepackInfo": "musepack",
        "TrueAudioInfo": "tta",
        "AIFFInfo": "pcm",
        "WaveStreamInfo": "pcm",
        "DSFInfo": "dsd",
        "DSDIFFInfo": "dsd",
    }
    if kind in named:
        return named[kind]
    return path.suffix.lower().lstrip(".") or None


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
    known = _MIME_TYPES.get(path.suffix.lower())
    if known:
        return known
    mime, _ = mimetypes.guess_type(str(path))
    return mime or "application/octet-stream"
