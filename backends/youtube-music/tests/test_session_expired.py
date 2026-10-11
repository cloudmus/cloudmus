"""A session YouTube has signed out is told as that, not as whatever the
failed call happened to raise."""
import pytest

from cloudmus_backend_ytmusic import auth as auth_module
from cloudmus_backend_ytmusic import catalog, client, config, server
from rpc_common import errors
from rpc_common.server import BackendError


class _FakeClient:
    def __init__(self, response=None, error=None):
        self._response, self._error = response, error

    def _send_request(self, endpoint, body):
        if self._error:
            raise self._error
        return self._response


def _answer(logged_in):
    return {"responseContext": {"serviceTrackingParams": [{"params": [{"key": "logged_in", "value": logged_in}]}]}}


def test_only_youtubes_own_word_counts_as_signed_out(monkeypatch):
    for fake, expected in (
        (_FakeClient(_answer("0")), True),
        (_FakeClient(_answer("1")), False),
        (_FakeClient({}), False),  # an answer that doesn't say
        (_FakeClient(error=OSError("no network")), False),
    ):
        monkeypatch.setattr(client, "get_client", lambda fake=fake: fake)
        assert client.signed_out() is expected


@pytest.fixture
def backend(monkeypatch):
    monkeypatch.setattr(config, "has_browser_auth", lambda: True)
    monkeypatch.setattr(client, "get_client", lambda: object())

    async def broken(*args, **kwargs):
        raise KeyError("twoColumnBrowseResultsRenderer")

    monkeypatch.setattr(catalog, "list_playlists", broken)
    built = server.build_server()
    events = []

    async def notify(method, params):
        events.append((method, params))

    monkeypatch.setattr(built, "notify", notify)
    return built, events


@pytest.mark.asyncio
async def test_a_signed_out_session_asks_for_a_new_sign_in(backend, monkeypatch):
    built, events = backend
    monkeypatch.setattr(client, "signed_out", lambda: True)
    assert built._handlers["auth.getStatus"]({}, 1) == {"status": "authenticated"}

    for request_id in (2, 3):
        with pytest.raises(BackendError) as info:
            await built._handlers["catalog.listPlaylists"]({}, request_id)
        assert info.value.code == errors.AUTH_EXPIRED

    # Told once, and the status no longer claims a sign-in the saved file
    # alone would suggest.
    assert [method for method, _ in events] == ["auth/statusChanged"]
    assert events[0][1]["status"] == "error"
    assert built._handlers["auth.getStatus"]({}, 4)["status"] == "error"


@pytest.mark.asyncio
async def test_another_failure_stays_what_it_is(backend, monkeypatch):
    built, events = backend
    monkeypatch.setattr(client, "signed_out", lambda: False)
    with pytest.raises(KeyError):
        await built._handlers["catalog.listPlaylists"]({}, 2)
    assert events == []
    assert built._handlers["auth.getStatus"]({}, 3) == {"status": "authenticated"}


@pytest.mark.asyncio
async def test_signing_in_again_clears_it(monkeypatch, tmp_path):
    monkeypatch.setattr(auth_module, "_verify_login", lambda path: None)
    monkeypatch.setattr(config, "browser_headers_path", lambda: tmp_path / "browser_headers.json")
    monkeypatch.setattr(config, "has_browser_auth", lambda: True)
    monkeypatch.setattr(auth_module.client_module, "reset_client", lambda: None)
    session = auth_module.BrowserAuthSession()

    async def notify(method, params):
        pass

    await session.mark_expired(notify)
    assert session.get_status()["status"] == "error"
    await session.submit({"headers": "cookie: __Secure-3PAPISID=abcdef123456\nx-goog-authuser: 0\n"}, notify)
    assert session.get_status() == {"status": "authenticated"}
