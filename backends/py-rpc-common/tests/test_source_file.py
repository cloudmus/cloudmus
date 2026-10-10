import pytest

from rpc_common.downloads import Cancelled, Tracker, audio_extension, save_from_source_file


@pytest.mark.parametrize(
    "head, extension",
    [
        (b"ID3\x04\x00", ".mp3"),
        (b"\xff\xfb\x90\x00", ".mp3"),  # no ID3 tag: straight into the frames
        (b"\x00\x00\x00\x18ftypM4A ", ".m4a"),
        (b"\x1a\x45\xdf\xa3\x9f", ".webm"),
        (b"OggS\x00", ".opus"),
        (b"fLaC\x00", ".flac"),
        (b"<html>", None),
        (b"", None),
    ],
)
def test_the_format_is_told_from_the_content(tmp_path, head, extension):
    path = tmp_path / "3f2a"  # as the front names what it caches: no extension
    path.write_bytes(head + b"\x00" * 16 if head else b"")
    assert audio_extension(path) == extension


def test_a_usable_file_is_copied_under_its_proper_name(tmp_path):
    source = tmp_path / "3f2a"
    source.write_bytes(b"ID3" + b"\x01" * 100)
    reported = []
    tracker = Tracker(None, None, None)
    tracker.progress = lambda received, total, final=False: reported.append((received, total, final))
    saved = save_from_source_file(str(source), tmp_path / "Artist - Song v1.5", tracker)
    assert saved == tmp_path / "Artist - Song v1.5.mp3"
    assert saved.read_bytes() == source.read_bytes()
    assert source.exists()
    assert reported == [(103, 103, True)]


def test_nothing_usable_means_download_as_usual(tmp_path):
    tracker = Tracker(None, None, None)
    garbage = tmp_path / "garbage"
    garbage.write_bytes(b"not audio at all")
    assert save_from_source_file(None, tmp_path / "x", tracker) is None
    assert save_from_source_file(str(tmp_path / "gone"), tmp_path / "x", tracker) is None
    assert save_from_source_file(str(garbage), tmp_path / "x", tracker) is None
    assert list(tmp_path.iterdir()) == [garbage]


def test_a_cancelled_download_copies_nothing(tmp_path):
    source = tmp_path / "3f2a"
    source.write_bytes(b"ID3" + b"\x01" * 100)
    tracker = Tracker(None, None, None)
    tracker.cancel()
    with pytest.raises(Cancelled):
        save_from_source_file(str(source), tmp_path / "x", tracker)
    assert not (tmp_path / "x.mp3").exists()
