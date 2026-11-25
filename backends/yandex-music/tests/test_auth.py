"""DeviceAuthSession: a device code survives the backend being killed while
the user confirms it in a browser, and the next start goes on with it."""
from __future__ import annotations

import asyncio
import time
from types import SimpleNamespace

import pytest
from yandex_music.exceptions import DeviceAuthError, NetworkError

from cloudmus_backend_yandex import auth, config


class FakeClient:
    codes_requested = 0
    # What each poll answers, in turn: None (not yet), a token, or an error.
    polls: list = []
    polled: list[str] = []

    def request_device_code(self):
        FakeClient.codes_requested += 1
        n = FakeClient.codes_requested
        return SimpleNamespace(
            device_code=f"device-{n}",
            user_code=f"USER{n}",
            verification_url="https://ya.ru/device",
            interval=0,
            expires_in=300,
        )

    def poll_device_token(self, device_code):
        FakeClient.polled.append(device_code)
        answer = FakeClient.polls.pop(0) if FakeClient.polls else None
        if isinstance(answer, Exception):
            raise answer
        return None if answer is None else SimpleNamespace(access_token=answer)


@pytest.fixture(autouse=True)
def isolated(tmp_path, monkeypatch):
    monkeypatch.setattr(config, "CONFIG_DIR", tmp_path)
    monkeypatch.setattr(config, "CONFIG_FILE", tmp_path / "config.json")
    monkeypatch.setattr(auth, "Client", FakeClient)
    monkeypatch.setattr(auth.client_module, "reset_client", lambda: None)
    FakeClient.codes_requested = 0
    FakeClient.polls = []
    FakeClient.polled = []


def run_session(polls: list) -> tuple[auth.DeviceAuthSession, list]:
    FakeClient.polls = list(polls)
    sent: list = []

    async def notify(method, params):
        sent.append((method, params))

    async def main():
        session = auth.DeviceAuthSession()
        session.start(notify)
        await session._task
        await asyncio.sleep(0)  # the prompt, sent from the worker thread
        return session

    return asyncio.run(main()), sent


def prompts(sent: list) -> list[dict]:
    return [params for method, params in sent if method == "auth/prompt"]


def test_signed_in_clears_the_pending_code():
    session, sent = run_session([None, "token-1"])
    assert session.status == "authenticated"
    assert config.get_token() == "token-1"
    assert config.get_pending_auth() is None
    assert prompts(sent)[0]["code"] == "USER1"


def test_a_killed_run_leaves_the_code_for_the_next_start():
    # The first run gets its code and is "killed": it never sees a token.
    config.set_pending_auth({
        "deviceCode": "device-old",
        "userCode": "OLD",
        "url": "https://ya.ru/device",
        "interval": 0,
        "expiresAt": time.time() + 200,
    })
    session, sent = run_session(["token-2"])
    assert FakeClient.codes_requested == 0
    assert FakeClient.polled == ["device-old"]
    assert prompts(sent)[0]["code"] == "OLD"
    assert 0 < prompts(sent)[0]["expiresInSec"] <= 200
    assert session.status == "authenticated"
    assert config.get_pending_auth() is None


def test_the_code_is_saved_while_waiting(monkeypatch):
    seen: list = []

    def poll(self, device_code):
        seen.append(config.get_pending_auth())
        return SimpleNamespace(access_token="t")

    monkeypatch.setattr(FakeClient, "poll_device_token", poll)
    run_session([])
    assert seen[0]["deviceCode"] == "device-1"
    assert seen[0]["userCode"] == "USER1"


def test_an_expired_code_is_replaced():
    config.set_pending_auth({
        "deviceCode": "device-old",
        "userCode": "OLD",
        "url": "https://ya.ru/device",
        "interval": 0,
        "expiresAt": time.time() - 1,
    })
    _, sent = run_session(["token"])
    assert FakeClient.codes_requested == 1
    assert prompts(sent)[0]["code"] == "USER1"


def test_a_refused_code_is_forgotten():
    session, _ = run_session([DeviceAuthError("access_denied")])
    assert session.status == "error"
    assert config.get_pending_auth() is None


def test_no_network_keeps_the_code_for_a_retry():
    session, _ = run_session([NetworkError("offline")])
    assert session.status == "error"
    assert config.get_pending_auth()["deviceCode"] == "device-1"


def test_logout_forgets_the_code():
    config.set_pending_auth({"deviceCode": "d", "userCode": "U", "url": "u", "interval": 0,
                             "expiresAt": time.time() + 200})
    auth.DeviceAuthSession().logout()
    assert config.get_pending_auth() is None
