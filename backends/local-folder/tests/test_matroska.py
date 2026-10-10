import struct
from pathlib import Path

from cloudmus_backend_local import cover_art, matroska, scanner

DATA = Path(__file__).parent / "data"


# --- a tiny EBML writer, enough to build what the reader reads

def _id(value: int) -> bytes:
    return value.to_bytes((value.bit_length() + 7) // 8, "big")


def _size(n: int) -> bytes:
    for length in range(1, 9):
        if n < (1 << (7 * length)) - 1:
            return ((1 << (7 * length)) | n).to_bytes(length, "big")
    raise ValueError(n)


def el(element_id: int, payload: bytes) -> bytes:
    return _id(element_id) + _size(len(payload)) + payload


def uint(element_id: int, value: int) -> bytes:
    return el(element_id, value.to_bytes(max(1, (value.bit_length() + 7) // 8), "big"))


def text(element_id: int, value: str) -> bytes:
    return el(element_id, value.encode("utf-8"))


def simple_tag(name: str, value: str) -> bytes:
    return el(0x67C8, text(0x45A3, name) + text(0x4487, value))


def tag(level: int | None, *simple: bytes) -> bytes:
    targets = el(0x63C0, uint(0x68CA, level) if level is not None else b"")
    return el(0x7373, targets + b"".join(simple))


EBML_HEADER = el(0x1A45DFA3, text(0x4282, "webm"))
INFO = el(0x1549A966, uint(0x2AD7B1, 1_000_000) + el(0x4489, struct.pack(">d", 185_500.0)))
CLUSTER = el(0x1F43B675, uint(0xE7, 0) + el(0xA3, b"\x81\x00\x00\x80" + b"\x00" * 600))
JPEG = b"\xff\xd8\xff\xe0" + b"\x00" * 40 + b"\xff\xd9"
ATTACHMENTS = el(
    0x1941A469,
    el(0x61A7, text(0x466E, "cover.jpg") + text(0x4660, "image/jpeg") + el(0x465C, JPEG)),
)


def webm(*segment_children: bytes, size_unknown: bool = False) -> bytes:
    body = b"".join(segment_children)
    segment_header = _id(0x18538067) + (b"\x01\xff\xff\xff\xff\xff\xff\xff" if size_unknown else _size(len(body)))
    return EBML_HEADER + segment_header + body


def test_reads_duration_tags_and_cover(tmp_path):
    path = tmp_path / "t.webm"
    path.write_bytes(
        webm(
            INFO,
            CLUSTER,
            # Tags and the attachment after the audio, as ffmpeg and TagLib put them.
            el(0x1254C367, tag(30, simple_tag("TITLE", "My Song"), simple_tag("ARTIST", "First"), simple_tag("ARTIST", "Second"))
               + tag(50, simple_tag("TITLE", "My Album"))),
            ATTACHMENTS,
        )
    )
    info = matroska.read(path)
    assert info.duration_s == 185.5
    assert info.title == "My Song"
    assert info.artists == ["First", "Second"]
    assert info.album == "My Album"
    assert info.cover == (JPEG, "image/jpeg")


def test_tags_without_a_target_are_the_tracks(tmp_path):
    # What ffmpeg (and so yt-dlp) writes: an empty Targets, album as ALBUM.
    path = tmp_path / "t.webm"
    path.write_bytes(
        webm(
            INFO,
            el(0x1254C367, tag(None, simple_tag("title", "Raw Title"), simple_tag("ARTIST", "A"), simple_tag("ALBUM", "Al"))),
            CLUSTER,
        )
    )
    info = matroska.read(path)
    assert (info.title, info.artists, info.album) == ("Raw Title", ["A"], "Al")


def test_the_segment_title_stands_in_for_a_missing_tag(tmp_path):
    path = tmp_path / "t.mka"
    info_with_title = el(0x1549A966, text(0x7BA9, "From The Info") + el(0x4489, struct.pack(">f", 2000.0)))
    path.write_bytes(webm(info_with_title))
    info = matroska.read(path)
    assert info.title == "From The Info"
    assert info.duration_s == 2.0  # ticks of 1 ms by default


def test_an_open_ended_segment_and_a_collection_level_tag(tmp_path):
    path = tmp_path / "t.webm"
    path.write_bytes(
        webm(
            INFO,
            el(0x1254C367, tag(70, simple_tag("TITLE", "A Box Set")) + tag(30, simple_tag("TITLE", "Track"))),
            size_unknown=True,
        )
    )
    assert matroska.read(path).title == "Track"


def test_a_damaged_or_foreign_file_gives_what_could_be_read(tmp_path):
    whole = webm(INFO, el(0x1254C367, tag(30, simple_tag("TITLE", "Cut Off"))))
    cut = tmp_path / "cut.webm"
    cut.write_bytes(whole[: len(whole) - 8])
    info = matroska.read(cut)
    assert info.duration_s == 185.5  # the Info before the damage

    for content in (b"", b"not matroska at all", b"\x1a\x45\xdf\xa3", b"\xff" * 64):
        junk = tmp_path / "junk.webm"
        junk.write_bytes(content)
        info = matroska.read(junk)
        assert info.duration_s is None and info.title is None and info.cover is None

    assert matroska.read(tmp_path / "missing.webm").title is None


def test_a_real_file_from_ffmpeg():
    info = matroska.read(DATA / "sample.webm")
    assert 0.25 < (info.duration_s or 0) < 0.4


def test_the_scanner_lists_a_webm_with_its_tags_and_cover(tmp_path, monkeypatch):
    covers = tmp_path / "covers"
    covers.mkdir()
    monkeypatch.setattr(cover_art, "_cache_dir", lambda: covers)
    song = tmp_path / "music" / "Artist - Song.webm"
    song.parent.mkdir()
    song.write_bytes(
        webm(INFO, el(0x1254C367, tag(30, simple_tag("TITLE", "Song"), simple_tag("ARTIST", "Artist"))), CLUSTER, ATTACHMENTS)
    )
    playlists = scanner.scan(tmp_path / "music")
    assert [p.files for p in playlists] == [[song]]

    track = scanner.read_track(song, tmp_path / "music")
    assert track.title == "Song"
    assert [a.name for a in track.artists] == ["Artist"]
    assert track.durationMs == 185_500
    assert track.coverUrl and track.coverUrl.startswith("file://")
    assert scanner.guess_mime_type(song) == "audio/webm"


def test_an_untagged_webm_is_named_after_its_file(tmp_path):
    song = tmp_path / "Some Video Soundtrack.webm"
    song.write_bytes(DATA.joinpath("sample.webm").read_bytes())
    track = scanner.read_track(song, tmp_path)
    assert track.title == "Some Video Soundtrack"
    assert track.durationMs > 0
