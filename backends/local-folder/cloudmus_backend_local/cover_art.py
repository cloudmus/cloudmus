"""Extracts embedded cover art from local audio files.

Covers are written once to a cache directory keyed by the track's absolute
path, and exposed to the front as a file:// URL — the same URL scheme
playback.py already uses for the audio stream itself (see resolve_stream()),
so the front's CoverArtCache (QNetworkAccessManager-based) fetches it the
same way it fetches an http(s) cover: QNetworkAccessManager handles file://
requests natively, no extra code needed on the front side.
"""
from __future__ import annotations

import base64
import hashlib
from pathlib import Path

from mutagen import File as MutagenFile
from mutagen.aiff import AIFF
from mutagen.apev2 import APEv2
from mutagen.flac import FLAC, Picture
from mutagen.id3 import ID3, APIC
from mutagen.mp4 import MP4, MP4Cover
from rpc_common.platform_paths import cache_home

from . import matroska

_MP4_COVER_EXTENSIONS = {
    MP4Cover.FORMAT_JPEG: ".jpg",
    MP4Cover.FORMAT_PNG: ".png",
}


def _cache_dir() -> Path:
    cache_root = cache_home()
    cache_dir = cache_root / "cloudmus" / "backends" / "local-folder" / "covers"
    cache_dir.mkdir(parents=True, exist_ok=True)
    return cache_dir


def _mime_to_extension(mime: str) -> str:
    if mime == "image/png":
        return ".png"
    return ".jpg"  # image/jpeg is by far the common case; a safe default otherwise


def _sniff_extension(data: bytes) -> str:
    return ".png" if data.startswith(b"\x89PNG") else ".jpg"


def _extract_bytes(path: Path) -> tuple[bytes, str] | None:
    """Returns (image_bytes, file_extension) for the first embedded picture
    found, or None if the format is unsupported or the file has no embedded
    cover. Exceptions from a malformed file are the caller's problem to
    swallow — cover art is optional, never worth failing the whole track
    listing over.
    """
    suffix = path.suffix.lower()

    if suffix in (".mp3", ".mp2"):
        tags = ID3(path)
        pictures = tags.getall("APIC")
        if not pictures:
            return None
        return pictures[0].data, _mime_to_extension(pictures[0].mime)

    if suffix in (".aif", ".aiff", ".aifc"):
        tags = AIFF(path).tags  # an ID3 tag in a chunk of its own
        pictures = tags.getall("APIC") if tags else []
        if not pictures:
            return None
        return pictures[0].data, _mime_to_extension(pictures[0].mime)

    if suffix in matroska.EXTENSIONS:
        cover = matroska.read(path).cover
        if cover is None:
            return None
        return cover[0], _mime_to_extension(cover[1])

    if suffix in (".ape", ".wv", ".mpc"):
        # APEv2: "<file name>\0<image>" under "Cover Art (Front)".
        tags = APEv2(path)
        for key in tags.keys():
            if key.lower() == "cover art (front)":
                _, _, image = bytes(tags[key].value).partition(b"\x00")
                return (image, _sniff_extension(image)) if image else None
        return None

    if suffix == ".flac":
        audio = FLAC(path)
        if not audio.pictures:
            return None
        return audio.pictures[0].data, _mime_to_extension(audio.pictures[0].mime)

    if suffix in (".m4a", ".m4b", ".aac"):
        audio = MP4(path)
        covers = audio.tags.get("covr") if audio.tags else None
        if not covers:
            return None
        cover = covers[0]
        return bytes(cover), _MP4_COVER_EXTENSIONS.get(cover.imageformat, ".jpg")

    if suffix in (".ogg", ".opus", ".oga", ".spx"):
        # Vorbis, Opus, FLAC or Speex in Ogg — told from the content.
        audio = MutagenFile(path)
        raw = audio.get("metadata_block_picture") if audio is not None else None
        if not raw:
            return None
        picture = Picture(base64.b64decode(raw[0]))
        return picture.data, _mime_to_extension(picture.mime)

    return None  # .wav/.wma and the rest: no embedded-art reader wired up


def extract_cover_uri(path: Path) -> str | None:
    try:
        result = _extract_bytes(path)
    except Exception:
        return None
    if result is None:
        return None
    data, extension = result
    if not data:
        return None

    key = hashlib.sha1(str(path.resolve()).encode("utf-8")).hexdigest()
    cover_path = _cache_dir() / f"{key}{extension}"
    if not cover_path.exists():
        cover_path.write_bytes(data)
    return cover_path.as_uri()
