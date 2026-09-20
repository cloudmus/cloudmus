from pathlib import Path

import pytest
import yt_dlp

from cloudmus_backend_ytmusic import download


def _fake_extract_and_download(dest_dir: Path, ext: str, **info_extra):
    def fake(video_id, outtmpl):
        raw_path = dest_dir / f"{video_id}.{ext}"
        raw_path.write_bytes(b"fake audio data")
        info = {"_filepath": str(raw_path)}
        info.update(info_extra)
        return info

    return fake


@pytest.mark.asyncio
async def test_renames_using_clean_music_metadata(tmp_path, monkeypatch):
    monkeypatch.setattr(download, "_tag_file", lambda path, info: None)
    monkeypatch.setattr(
        download,
        "_extract_and_download",
        _fake_extract_and_download(tmp_path, "m4a", track="My Song", artist="My Artist"),
    )
    result = await download.download_track("abc123", str(tmp_path))
    final_path = Path(result["path"])
    assert final_path.name == "My Artist - My Song.m4a"
    assert final_path.exists()
    assert not (tmp_path / "abc123.m4a").exists()  # renamed away, not copied


@pytest.mark.asyncio
async def test_falls_back_to_title_and_uploader_without_music_metadata(tmp_path, monkeypatch):
    # No "track"/"artist" (yt-dlp only populates those for a recognized
    # Music URL) — falls back to the raw video title/uploader.
    monkeypatch.setattr(download, "_tag_file", lambda path, info: None)
    monkeypatch.setattr(
        download,
        "_extract_and_download",
        _fake_extract_and_download(tmp_path, "webm", title="Raw Video Title", uploader="Some Channel"),
    )
    result = await download.download_track("abc123", str(tmp_path))
    assert Path(result["path"]).name == "Some Channel - Raw Video Title.webm"


@pytest.mark.asyncio
async def test_sanitizes_unsafe_characters_in_filename(tmp_path, monkeypatch):
    monkeypatch.setattr(download, "_tag_file", lambda path, info: None)
    monkeypatch.setattr(
        download,
        "_extract_and_download",
        _fake_extract_and_download(tmp_path, "opus", track="Song: Part 2/2", artist="A/C"),
    )
    result = await download.download_track("abc123", str(tmp_path))
    assert Path(result["path"]).name == "A_C - Song_ Part 2_2.opus"


@pytest.mark.asyncio
async def test_non_transient_download_error_becomes_lookup_error(monkeypatch):
    def fake(video_id, outtmpl):
        raise yt_dlp.utils.DownloadError("ERROR: [youtube] abc: Video unavailable")

    monkeypatch.setattr(download, "_extract_and_download", fake)
    with pytest.raises(LookupError):
        await download.download_track("abc123", "/tmp/wherever")


@pytest.mark.asyncio
async def test_transient_download_error_propagates(monkeypatch):
    def fake(video_id, outtmpl):
        raise yt_dlp.utils.DownloadError("ERROR: some transient network blip")

    monkeypatch.setattr(download, "_extract_and_download", fake)
    with pytest.raises(yt_dlp.utils.DownloadError):
        await download.download_track("abc123", "/tmp/wherever")


def test_tag_file_writes_m4a_atoms(tmp_path, monkeypatch):
    written = {}

    class FakeMP4(dict):
        def __init__(self, path):
            super().__init__()

        def save(self):
            written.update(self)

    monkeypatch.setattr(download, "MP4", FakeMP4)
    path = tmp_path / "song.m4a"
    path.write_bytes(b"x")
    download._tag_file(path, {"track": "Song", "artist": "Artist", "album": "Album"})
    assert written == {"\xa9nam": ["Song"], "\xa9ART": ["Artist"], "\xa9alb": ["Album"]}


def test_tag_file_writes_opus_fields(tmp_path, monkeypatch):
    written = {}

    class FakeOggOpus(dict):
        def __init__(self, path):
            super().__init__()

        def save(self):
            written.update(self)

    monkeypatch.setattr(download, "OggOpus", FakeOggOpus)
    path = tmp_path / "song.opus"
    path.write_bytes(b"x")
    download._tag_file(path, {"track": "Song", "artist": "Artist"})
    assert written == {"title": ["Song"], "artist": ["Artist"]}


def test_tag_file_is_best_effort_on_failure(tmp_path, monkeypatch):
    class ExplodingMP4:
        def __init__(self, path):
            raise ValueError("not a real MP4 file")

    monkeypatch.setattr(download, "MP4", ExplodingMP4)
    path = tmp_path / "song.m4a"
    path.write_bytes(b"garbage")
    download._tag_file(path, {"track": "Song"})  # must not raise


def test_tag_file_skips_unsupported_containers(tmp_path, monkeypatch):
    # .webm has no mutagen writer wired up here — should just no-op, not
    # touch MP4/OggOpus/ID3 at all.
    for name in ("MP4", "OggOpus", "ID3"):
        monkeypatch.setattr(
            download, name, lambda *a, **k: (_ for _ in ()).throw(AssertionError(f"{name} should not be used"))
        )
    path = tmp_path / "song.webm"
    path.write_bytes(b"x")
    download._tag_file(path, {"track": "Song"})  # must not raise
