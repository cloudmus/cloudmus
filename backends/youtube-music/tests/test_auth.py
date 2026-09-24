import pytest

from cloudmus_backend_ytmusic import auth as auth_module
from cloudmus_backend_ytmusic import config


def _notify_recorder():
    events = []

    async def notify(method, params):
        events.append((method, params))

    return notify, events


@pytest.mark.asyncio
async def test_submit_success_writes_file_and_reports_authenticated(monkeypatch, tmp_path):
    headers_file = tmp_path / "browser_headers.json"
    monkeypatch.setattr(config, "browser_headers_path", lambda: headers_file)
    monkeypatch.setattr(auth_module.client_module, "reset_client", lambda: None)

    session = auth_module.BrowserAuthSession()
    notify, events = _notify_recorder()

    raw = "cookie: __Secure-3PAPISID=abcdef123456\nx-goog-authuser: 0\n"
    await session.submit({"headers": raw}, notify)

    assert session.status == "authenticated"
    assert headers_file.exists()
    assert events[-1] == ("auth/statusChanged", {"status": "authenticated"})


@pytest.mark.asyncio
async def test_submit_missing_cookie_reports_error(monkeypatch, tmp_path):
    headers_file = tmp_path / "browser_headers.json"
    monkeypatch.setattr(config, "browser_headers_path", lambda: headers_file)
    monkeypatch.setattr(auth_module.client_module, "reset_client", lambda: None)

    session = auth_module.BrowserAuthSession()
    notify, events = _notify_recorder()

    raw = "x-goog-authuser: 0\n"  # missing the required cookie header
    await session.submit({"headers": raw}, notify)

    assert session.status == "error"
    assert not headers_file.exists()
    method, params = events[-1]
    assert method == "auth/statusChanged"
    assert params["status"] == "error"
    assert "cookie" in params["message"].lower()


def test_get_status_reflects_saved_file(monkeypatch, tmp_path):
    headers_file = tmp_path / "browser_headers.json"
    monkeypatch.setattr(config, "has_browser_auth", lambda: headers_file.exists())

    session = auth_module.BrowserAuthSession()
    assert session.get_status() == {"status": "unauthenticated"}

    headers_file.write_text("{}")
    assert session.get_status() == {"status": "authenticated"}
