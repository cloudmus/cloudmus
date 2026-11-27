import json

import pytest

from cloudmus_backend_ytmusic import auth as auth_module
from cloudmus_backend_ytmusic import config
from rpc_common.generated.models import AuthBrowserResult, PromptParams, SubmitParams


_verify_login = auth_module._verify_login


@pytest.fixture(autouse=True)
def _no_network(monkeypatch):
    monkeypatch.setattr(auth_module, "_verify_login", lambda path: None)


def _notify_recorder():
    events = []

    async def notify(method, params):
        events.append((method, params))

    return notify, events


def _pasted(raw):
    return SubmitParams(form={"headers": raw})


def _from_browser(cookie):
    return SubmitParams(
        browser=AuthBrowserResult(
            finalUrl="https://music.youtube.com/",
            userAgent="Mozilla/5.0 (Linux; Android 14)",
            cookies={auth_module.MUSIC_URL: cookie},
        )
    )


@pytest.mark.asyncio
async def test_submit_success_writes_file_and_reports_authenticated(monkeypatch, tmp_path):
    headers_file = tmp_path / "browser_headers.json"
    monkeypatch.setattr(config, "browser_headers_path", lambda: headers_file)
    monkeypatch.setattr(auth_module.client_module, "reset_client", lambda: None)

    session = auth_module.BrowserAuthSession()
    notify, events = _notify_recorder()

    raw = "cookie: __Secure-3PAPISID=abcdef123456\nx-goog-authuser: 0\n"
    await session.submit(_pasted(raw), notify)

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
    await session.submit(_pasted(raw), notify)

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


def test_curl_to_headers_extracts_headers_and_cookie_flag():
    cmd = (
        "curl 'https://music.youtube.com/youtubei/v1/browse?prettyPrint=false' \\\n"
        "  -H 'accept: */*' \\\n"
        "  -H 'x-goog-authuser: 0' \\\n"
        "  -b '__Secure-3PAPISID=abc; SID=def' \\\n"
        "  --data-raw '{\"a\":1}'"
    )
    assert auth_module.curl_to_headers(cmd).splitlines() == [
        "accept: */*",
        "x-goog-authuser: 0",
        "cookie: __Secure-3PAPISID=abc; SID=def",
    ]


def test_curl_to_headers_passes_raw_headers_through():
    raw = "cookie: a=b\nx-goog-authuser: 0\n"
    assert auth_module.curl_to_headers(raw) == raw


def test_submit_without_authorization_header_still_loads_as_browser_auth(monkeypatch, tmp_path):
    import asyncio

    from ytmusicapi import YTMusic

    headers_file = tmp_path / "browser_headers.json"
    monkeypatch.setattr(config, "browser_headers_path", lambda: headers_file)
    monkeypatch.setattr(auth_module.client_module, "reset_client", lambda: None)

    notify, _ = _notify_recorder()
    raw = "cookie: __Secure-3PAPISID=abcdef123456\nx-goog-authuser: 0\n"
    asyncio.run(auth_module.BrowserAuthSession().submit(_pasted(raw), notify))

    # Raised "oauth JSON provided via auth argument..." before the marker.
    YTMusic(auth=str(headers_file))


@pytest.mark.asyncio
async def test_submit_rejected_by_youtube_reports_error_and_removes_file(monkeypatch, tmp_path):
    headers_file = tmp_path / "browser_headers.json"
    monkeypatch.setattr(config, "browser_headers_path", lambda: headers_file)
    monkeypatch.setattr(auth_module.client_module, "reset_client", lambda: None)

    def reject(path):
        raise RuntimeError("HTTP 401: login required")

    monkeypatch.setattr(auth_module, "_verify_login", reject)
    session = auth_module.BrowserAuthSession()
    notify, events = _notify_recorder()

    await session.submit(_pasted("cookie: __Secure-3PAPISID=abc\nx-goog-authuser: 0\n"), notify)

    assert session.status == "error"
    assert not headers_file.exists()
    assert events[-1] == ("auth/statusChanged", {"status": "error", "message": "HTTP 401: login required"})


@pytest.mark.asyncio
async def test_start_offers_the_browser_first_then_pasting():
    session = auth_module.BrowserAuthSession()
    notify, events = _notify_recorder()

    await session.start(notify)

    method, params = events[-1]
    assert method == "auth/prompt"
    browser, form = PromptParams.from_dict(params).methods
    assert browser.kind == "browser"
    assert browser.cookieUrls == [auth_module.MUSIC_URL]
    assert browser.doneCookies == [auth_module.SESSION_COOKIE]
    assert browser.freshSession
    assert form.kind == "form"
    assert form.message
    assert [(f.name, f.multiline) for f in form.fields] == [("headers", True)]


@pytest.mark.asyncio
async def test_submit_from_browser_saves_its_cookies(monkeypatch, tmp_path):
    headers_file = tmp_path / "browser_headers.json"
    monkeypatch.setattr(config, "browser_headers_path", lambda: headers_file)
    monkeypatch.setattr(auth_module.client_module, "reset_client", lambda: None)
    session = auth_module.BrowserAuthSession()
    notify, events = _notify_recorder()
    cookie = "SID=a; __Secure-3PAPISID=abcdef123456"

    await session.submit(_from_browser(cookie), notify)

    assert events[-1] == ("auth/statusChanged", {"status": "authenticated"})
    saved = json.loads(headers_file.read_text())
    assert saved["cookie"] == cookie
    assert saved["x-goog-authuser"] == "0"
    # ytmusicapi takes them as browser auth, not as a broken OAuth token.
    from ytmusicapi import YTMusic

    YTMusic(auth=str(headers_file))


@pytest.mark.asyncio
async def test_submit_from_browser_without_a_session_reports_error(monkeypatch, tmp_path):
    headers_file = tmp_path / "browser_headers.json"
    monkeypatch.setattr(config, "browser_headers_path", lambda: headers_file)
    monkeypatch.setattr(auth_module.client_module, "reset_client", lambda: None)

    def reject(path):
        raise RuntimeError("HTTP 401: login required")

    monkeypatch.setattr(auth_module, "_verify_login", reject)
    session = auth_module.BrowserAuthSession()
    notify, events = _notify_recorder()

    await session.submit(_from_browser(""), notify)

    assert session.status == "error"
    assert not headers_file.exists()
    assert events[-1][1]["status"] == "error"


@pytest.mark.parametrize("signed_out", [False, True])
def test_a_session_youtube_answers_as_nobodys_isnt_taken(monkeypatch, tmp_path, signed_out):
    """Its library call may well succeed, with nothing in it."""

    class Client:
        def __init__(self, auth):
            pass

        def get_library_playlists(self, limit):
            return []

    monkeypatch.setattr(auth_module, "YTMusic", Client)
    monkeypatch.setattr(auth_module.client_module, "says_signed_out", lambda client: signed_out)

    if signed_out:
        with pytest.raises(RuntimeError):
            _verify_login(tmp_path / "headers.json")
    else:
        _verify_login(tmp_path / "headers.json")
