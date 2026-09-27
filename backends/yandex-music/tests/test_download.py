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
    monkeypatch.setattr(download.requests, "get", lambda *a, **k: _Response([b"ab", b"cd"], 4))
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

    monkeypatch.setattr(download.requests, "get", lambda *a, **k: _Response(chunks(), 4))
    path = tmp_path / "t.mp3"
    with pytest.raises(Cancelled):
        download._fetch(_client(), "https://x", path, tracker)
    assert not path.exists()
