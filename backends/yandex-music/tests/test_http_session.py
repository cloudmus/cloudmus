from types import SimpleNamespace

import pytest
from yandex_music.exceptions import NetworkError, NotFoundError
from yandex_music.utils.request import Request

from cloudmus_backend_yandex import http_session


@pytest.fixture
def installed(monkeypatch):
    """install() is a one-time global patch — restore the library's own
    wrapper afterwards and re-run install() fresh for each test."""
    monkeypatch.setattr(Request, "_request_wrapper", Request._request_wrapper, raising=True)
    monkeypatch.setattr(http_session, "_installed", False)
    http_session.install()


def test_every_request_goes_through_the_one_session(installed, monkeypatch):
    sessions = []

    def fake_request(self, method, url, **kwargs):
        sessions.append(self)
        return SimpleNamespace(status_code=200, content=b'{"ok": true}')

    monkeypatch.setattr(type(http_session.session()), "request", fake_request)

    assert Request().retrieve("https://api.music.yandex.net/a") == b'{"ok": true}'
    assert Request().retrieve("https://api.music.yandex.net/b") == b'{"ok": true}'
    assert sessions == [http_session.session(), http_session.session()]


def test_error_status_still_raises_the_library_error(installed, monkeypatch):
    monkeypatch.setattr(
        type(http_session.session()),
        "request",
        lambda self, *a, **k: SimpleNamespace(status_code=404, content=b""),
    )
    with pytest.raises(NotFoundError):
        Request().retrieve("https://api.music.yandex.net/missing")


def test_connection_failure_becomes_network_error(installed, monkeypatch):
    import requests

    def fail(self, *a, **k):
        raise requests.ConnectionError("down")

    monkeypatch.setattr(type(http_session.session()), "request", fail)
    with pytest.raises(NetworkError):
        Request().retrieve("https://api.music.yandex.net/a")
