import logging

from yandex_music.utils.request import Request

from cloudmus_backend_yandex import http_logging


def _reinstall_over(monkeypatch, fake_original):
    """install() is a one-time global patch (see its docstring) — reset the
    guard so each test re-wraps whatever fake it just monkeypatched in,
    instead of reusing a stale `original` captured by an earlier test."""
    monkeypatch.setattr(Request, "_request_wrapper", fake_original, raising=True)
    monkeypatch.setattr(http_logging, "_installed", False)
    http_logging.install()


def test_wraps_and_logs_request_and_response(monkeypatch, caplog):
    calls = []

    def fake_original(self, *args, **kwargs):
        calls.append((args, kwargs))
        return b'{"ok": true}'

    _reinstall_over(monkeypatch, fake_original)

    with caplog.at_level(logging.INFO, logger="cloudmus_backend_yandex.http_logging"):
        result = Request()._request_wrapper(
            "GET", "https://api.music.yandex.net/some/path", params={"trackId": "123"}
        )

    assert result == b'{"ok": true}'
    assert calls == [(("GET", "https://api.music.yandex.net/some/path"), {"params": {"trackId": "123"}})]
    messages = [r.message for r in caplog.records]
    assert any("YM -> GET https://api.music.yandex.net/some/path" in m and "trackId" in m for m in messages)
    assert any("YM <- GET https://api.music.yandex.net/some/path" in m and "bytes" in m for m in messages)


def test_logs_and_reraises_on_failure(monkeypatch, caplog):
    def fake_original(self, *args, **kwargs):
        raise RuntimeError("boom")

    _reinstall_over(monkeypatch, fake_original)

    with caplog.at_level(logging.INFO, logger="cloudmus_backend_yandex.http_logging"):
        try:
            Request()._request_wrapper("POST", "https://api.music.yandex.net/other")
        except RuntimeError as e:
            assert str(e) == "boom"
        else:
            raise AssertionError("expected RuntimeError to propagate")

    assert any("failed" in r.message and "boom" in r.message for r in caplog.records)


def test_never_logs_headers_or_auth_token(monkeypatch, caplog):
    def fake_original(self, *args, **kwargs):
        return b"{}"

    _reinstall_over(monkeypatch, fake_original)

    with caplog.at_level(logging.INFO, logger="cloudmus_backend_yandex.http_logging"):
        Request()._request_wrapper(
            "GET",
            "https://api.music.yandex.net/some/path",
            headers={"Authorization": "OAuth super-secret-token"},
            params={"trackId": "123"},
        )

    for record in caplog.records:
        assert "super-secret-token" not in record.message
        assert "Authorization" not in record.message


def test_install_is_idempotent(monkeypatch):
    monkeypatch.setattr(http_logging, "_installed", False)
    wrapper_calls = {"count": 0}

    def fake_original(self, *args, **kwargs):
        return b"{}"

    monkeypatch.setattr(Request, "_request_wrapper", fake_original, raising=True)
    http_logging.install()
    first = Request._request_wrapper
    http_logging.install()  # second call must be a no-op
    second = Request._request_wrapper
    assert first is second
