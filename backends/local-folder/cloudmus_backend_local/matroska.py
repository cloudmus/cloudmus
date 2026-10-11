"""Reads what the library needs from Matroska files — WebM, .weba, .mka:
duration, codec, title, artists, album and the cover.

mutagen has no Matroska reader, and YouTube's audio (saved by the front, or
by yt-dlp) is WebM. Only the few elements wanted are read, and only the
parts of the file they live in: clusters, which are all of the audio, are
skipped by seeking over them. Nothing here raises for a damaged or foreign
file — what couldn't be read is left out.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import BinaryIO, Iterator

EXTENSIONS = {".webm", ".weba", ".mka"}

# Element IDs (the ID's own length marker bits are part of it).
_SEGMENT = 0x18538067
_INFO = 0x1549A966
_TRACKS = 0x1654AE6B
_TRACK_ENTRY = 0xAE
_CODEC_ID = 0x86
_TIMECODE_SCALE = 0x2AD7B1
_DURATION = 0x4489
_TITLE = 0x7BA9
_TAGS = 0x1254C367
_TAG = 0x7373
_TARGETS = 0x63C0
_TARGET_TYPE_VALUE = 0x68CA
_SIMPLE_TAG = 0x67C8
_TAG_NAME = 0x45A3
_TAG_STRING = 0x4487
_ATTACHMENTS = 0x1941A469
_ATTACHED_FILE = 0x61A7
_FILE_MEDIA_TYPE = 0x4660
_FILE_DATA = 0x465C

_DEFAULT_TIMECODE_SCALE = 1_000_000  # nanoseconds per tick
# The track as ffmpeg and TagLib write it (30), and the album (50, what
# the spec defaults to — but a Targets with none is the track's, for ffmpeg).
_TRACK_LEVEL = 30
_ALBUM_LEVEL = 50
# Past this a metadata element is skipped rather than read into memory.
_MAX_ELEMENT_BYTES = 32 * 1024 * 1024


@dataclass
class MatroskaInfo:
    duration_s: float | None = None
    # The first track's codec, as the short lowercase name the protocol's
    # StreamDescriptor.codec uses ("opus", "vorbis", "aac").
    codec: str | None = None
    title: str | None = None
    artists: list[str] = field(default_factory=list)
    album: str | None = None
    # (image bytes, mime type)
    cover: tuple[bytes, str] | None = None


def _vint(buf: bytes, pos: int, *, keep_marker: bool) -> tuple[int, int, bool]:
    """Reads a variable-length integer: (value, length, all value bits set).
    An ID keeps its length marker, a size doesn't."""
    first = buf[pos]
    length = 1
    while length <= 8 and not first & (0x80 >> (length - 1)):
        length += 1
    if length > 8 or pos + length > len(buf):
        raise ValueError("bad variable-length integer")
    value = first if keep_marker else first & (0xFF >> length)
    for k in range(1, length):
        value = (value << 8) | buf[pos + k]
    unknown = not keep_marker and value == (1 << (7 * length)) - 1
    return value, length, unknown


def _elements(buf: bytes, start: int = 0, end: int | None = None) -> Iterator[tuple[int, int, int]]:
    """(id, payload start, payload end) of each element in buf[start:end]; an
    element of unknown size extends to the end."""
    end = len(buf) if end is None else end
    pos = start
    while pos < end:
        element_id, id_length, _ = _vint(buf, pos, keep_marker=True)
        pos += id_length
        size, size_length, unknown = _vint(buf, pos, keep_marker=False)
        pos += size_length
        stop = end if unknown else min(pos + size, end)
        yield element_id, pos, stop
        pos = stop


def _uint(buf: bytes, start: int, end: int) -> int:
    return int.from_bytes(buf[start:end], "big")


def _text(buf: bytes, start: int, end: int) -> str:
    return buf[start:end].decode("utf-8", "replace").rstrip("\x00").strip()


def _parse_info(buf: bytes, start: int, end: int, result: MatroskaInfo) -> None:
    scale = _DEFAULT_TIMECODE_SCALE
    duration = None
    for element_id, a, b in _elements(buf, start, end):
        if element_id == _TIMECODE_SCALE:
            scale = _uint(buf, a, b) or scale
        elif element_id == _DURATION and b - a in (4, 8):
            duration = struct.unpack(">f" if b - a == 4 else ">d", buf[a:b])[0]
        elif element_id == _TITLE and not result.title:
            result.title = _text(buf, a, b) or None
    if duration is not None and duration > 0:
        result.duration_s = duration * scale / 1e9


def _codec_name(codec_id: str) -> str | None:
    """Matroska's CodecID ("A_OPUS", "A_AAC/MPEG4/LC") as a short name."""
    base = codec_id.removeprefix("A_").split("/")[0].lower()
    if base == "mpeg":
        return "mp3" if codec_id.endswith("L3") else "mp2"
    return base or None


def _parse_tracks(buf: bytes, start: int, end: int, result: MatroskaInfo) -> None:
    for element_id, a, b in _elements(buf, start, end):
        if element_id != _TRACK_ENTRY:
            continue
        for inner_id, c, d in _elements(buf, a, b):
            if inner_id == _CODEC_ID and result.codec is None:
                result.codec = _codec_name(_text(buf, c, d))
        if result.codec:
            return


def _parse_tags(buf: bytes, start: int, end: int, result: MatroskaInfo) -> None:
    album_level_title = None
    for tag_id, tag_start, tag_end in _elements(buf, start, end):
        if tag_id != _TAG:
            continue
        level = _TRACK_LEVEL
        simple: list[tuple[str, str]] = []
        for element_id, a, b in _elements(buf, tag_start, tag_end):
            if element_id == _TARGETS:
                for target_id, c, d in _elements(buf, a, b):
                    if target_id == _TARGET_TYPE_VALUE:
                        level = _uint(buf, c, d)
            elif element_id == _SIMPLE_TAG:
                name = value = None
                for field_id, c, d in _elements(buf, a, b):
                    if field_id == _TAG_NAME:
                        name = _text(buf, c, d).upper()
                    elif field_id == _TAG_STRING:
                        value = _text(buf, c, d)
                if name and value:
                    simple.append((name, value))
        for name, value in simple:
            if level == _ALBUM_LEVEL:
                if name == "TITLE" and album_level_title is None:
                    album_level_title = value
            elif level < _ALBUM_LEVEL:  # the track's (or a part of it); 60 and up is a collection's
                if name == "TITLE" and not result.title:
                    result.title = value
                elif name == "ARTIST" and value not in result.artists:
                    result.artists.append(value)
                elif name == "ALBUM" and not result.album:
                    result.album = value
    if album_level_title and not result.album:
        result.album = album_level_title


def _parse_attachments(buf: bytes, start: int, end: int, result: MatroskaInfo) -> None:
    for file_id, file_start, file_end in _elements(buf, start, end):
        if file_id != _ATTACHED_FILE or result.cover is not None:
            continue
        mime = data = None
        for element_id, a, b in _elements(buf, file_start, file_end):
            if element_id == _FILE_MEDIA_TYPE:
                mime = _text(buf, a, b).lower()
            elif element_id == _FILE_DATA:
                data = bytes(buf[a:b])
        if data and mime and mime.startswith("image/"):
            result.cover = (data, mime)


def _read_header(f: BinaryIO) -> tuple[int, int, bool] | None:
    """(element id, payload size, size unknown) at the file position; None
    at the end of the file."""
    head = f.read(12)
    if len(head) < 2:
        return None
    element_id, id_length, _ = _vint(head, 0, keep_marker=True)
    size, size_length, unknown = _vint(head, id_length, keep_marker=False)
    f.seek(-(len(head) - id_length - size_length), 1)
    return element_id, size, unknown


def read(path: Path) -> MatroskaInfo:
    result = MatroskaInfo()
    try:
        with open(path, "rb") as f:
            _scan(f, result)
    except (OSError, ValueError, struct.error, IndexError):
        pass  # a damaged file shows what was read before the damage
    return result


def _scan(f: BinaryIO, result: MatroskaInfo) -> None:
    # The EBML header, then the segment, whose children are walked.
    header = _read_header(f)
    if header is None or header[0] != 0x1A45DFA3 or header[2]:
        return
    f.seek(header[1], 1)
    segment = _read_header(f)
    if segment is None or segment[0] != _SEGMENT:
        return
    while True:
        element = _read_header(f)
        if element is None:
            return
        element_id, size, unknown = element
        if unknown:
            return  # an open-ended cluster can't be skipped over
        if element_id in (_INFO, _TRACKS, _TAGS, _ATTACHMENTS) and size <= _MAX_ELEMENT_BYTES:
            payload = f.read(size)
            if len(payload) < size:
                return
            if element_id == _INFO:
                _parse_info(payload, 0, len(payload), result)
            elif element_id == _TRACKS:
                _parse_tracks(payload, 0, len(payload), result)
            elif element_id == _TAGS:
                _parse_tags(payload, 0, len(payload), result)
            else:
                _parse_attachments(payload, 0, len(payload), result)
        else:
            f.seek(size, 1)
