from types import SimpleNamespace

import pytest

from cloudmus_backend_yandex import download
from rpc_common.downloads import Cancelled, Tracker


class _Response:
    def __init__(self, chunks, total):
        self._chunks = chunks
        self.headers = {"Content-Length": str(total)}

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False

    def raise_for_status(self):
        pass

    def iter_content(self, size):
        yield from self._chunks


def _client():
    return SimpleNamespace(request=SimpleNamespace(headers={}, proxies=None))


def test_fetch_streams_the_file_and_reports_progress(tmp_path, monkeypatch):
    monkeypatch.setattr(download.http_session.session(), "get", lambda *a, **k: _Response([b"ab", b"cd"], 4))
    reported = []
    tracker = Tracker(None, None, None)
    tracker.progress = lambda received, total, final=False: reported.append((received, total, final))
    path = tmp_path / "t.mp3"
    download._fetch(_client(), "https://x", path, tracker)
    assert path.read_bytes() == b"abcd"
    assert reported == [(2, 4, False), (4, 4, False), (4, 4, True)]


def test_a_cancel_mid_way_leaves_no_file(tmp_path, monkeypatch):
    tracker = Tracker(None, None, None)

    def chunks():
        yield b"ab"
        tracker.cancel()
        yield b"cd"

    monkeypatch.setattr(download.http_session.session(), "get", lambda *a, **k: _Response(chunks(), 4))
    path = tmp_path / "t.mp3"
    with pytest.raises(Cancelled):
        download._fetch(_client(), "https://x", path, tracker)
    assert not path.exists()


def _yandex_client(track):
    client = _client()
    client.tracks = lambda ids: [track]
    return client


def _track():
    def no_links():
        raise AssertionError("the download links must not be asked for")

    return SimpleNamespace(
        title="Song",
        albums=[],
        artists_name=lambda: ["Artist"],
        get_download_info=no_links,
        download_cover_bytes=lambda size: None,
    )


def test_a_file_the_front_already_has_is_saved_without_downloading(tmp_path, monkeypatch):
    # What the front downloaded to play the track: no name to go by.
    cached = tmp_path / "cache" / "3f2a"
    cached.parent.mkdir()
    cached.write_bytes(b"ID3" + b"\x00" * 64)
    monkeypatch.setattr(download, "_fetch", lambda *a: pytest.fail("downloaded again"))
    tagged = []
    monkeypatch.setattr(download, "_tag_file", lambda path, track: tagged.append(path))

    dest = tmp_path / "music"
    path = download._download_track_sync(
        _yandex_client(_track()), "1", dest, "best", Tracker(None, None, None), str(cached)
    )
    assert path == dest / "Artist - Song.mp3"
    assert path.read_bytes() == cached.read_bytes()
    assert cached.exists()  # copied: the file stays the front's
    assert tagged == [path]


def test_a_source_file_that_is_no_use_is_downloaded_instead(tmp_path, monkeypatch):
    track = _track()
    track.get_download_info = lambda: [SimpleNamespace(codec="mp3", get_direct_link=lambda: "https://x")]
    monkeypatch.setattr(download.quality, "pick", lambda infos, level: infos[0])
    fetched = []
    monkeypatch.setattr(download, "_fetch", lambda client, url, path, tracker: fetched.append(path))
    monkeypatch.setattr(download, "_tag_file", lambda path, track: None)

    garbage = tmp_path / "garbage"
    garbage.write_bytes(b"<html>not audio</html>")
    for source_file in (str(tmp_path / "gone"), str(garbage)):
        path = download._download_track_sync(
            _yandex_client(track), "1", tmp_path, "best", Tracker(None, None, None), source_file
        )
        assert path == tmp_path / "Artist - Song.mp3"
    assert len(fetched) == 2
