import asyncio
import threading
import time
from types import SimpleNamespace

import pytest

from cloudmus_backend_yandex import auth as auth_module


class _FakeClient:
    """Client().device_auth as the library runs it: shows a code, then polls
    until the user signs in or should_cancel() says stop."""

    signed_in = threading.Event()
    issued = 0

    def device_auth(self, on_code, should_cancel):
        _FakeClient.issued += 1
        code = f"CODE{_FakeClient.issued}"
        on_code(SimpleNamespace(verification_url="https://ya.ru/device", user_code=code, expires_in=300))
        while not should_cancel():
            if _FakeClient.signed_in.is_set():
                return SimpleNamespace(access_token="token")
            time.sleep(0.01)
        raise RuntimeError("cancelled")


@pytest.fixture(autouse=True)
def _fresh_fake():
    _FakeClient.signed_in.clear()
    _FakeClient.issued = 0


@pytest.mark.asyncio
async def test_start_right_after_cancel_begins_a_new_sign_in(monkeypatch):
    monkeypatch.setattr(auth_module, "Client", _FakeClient)
    saved = []
    monkeypatch.setattr(auth_module.config, "get_token", lambda: None)
    monkeypatch.setattr(auth_module.config, "set_token", saved.append)
    monkeypatch.setattr(auth_module.client_module, "reset_client", lambda: None)
    events = []

    async def notify(method, params):
        events.append((method, params))

    session = auth_module.DeviceAuthSession()
    session.start(notify)
    first = session._task
    await asyncio.sleep(0.1)
    session.cancel()
    session.start(notify)  # the front's Start over, before the first run noticed
    assert session._task is not first
    await asyncio.sleep(0.1)

    prompts = [p["code"] for m, p in events if m == "auth/prompt"]
    assert prompts == ["CODE1", "CODE2"]
    # The cancelled run ended without a word: no error for the front to show.
    await first
    assert [m for m, _ in events if m == "auth/statusChanged"] == []

    _FakeClient.signed_in.set()
    await session._task
    assert saved == ["token"]
    assert events[-1] == ("auth/statusChanged", {"status": "authenticated"})
    assert session.status == "authenticated"


@pytest.mark.asyncio
async def test_start_while_running_is_a_no_op(monkeypatch):
    monkeypatch.setattr(auth_module, "Client", _FakeClient)
    events = []

    async def notify(method, params):
        events.append((method, params))

    session = auth_module.DeviceAuthSession()
    session.start(notify)
    task = session._task
    session.start(notify)
    assert session._task is task
    session.cancel()
    await task
