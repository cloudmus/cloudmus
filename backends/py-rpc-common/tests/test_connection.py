import asyncio
import socket

import pytest
import requests

from rpc_common.connection import OFFLINE, ONLINE, ConnectionMonitor


class Recorder:
    def __init__(self):
        self.sent: list[tuple[str, dict]] = []

    async def __call__(self, method, params):
        self.sent.append((method, params))


def run(coro):
    return asyncio.run(coro)


def test_a_probe_that_answers_is_online_and_says_nothing_new():
    async def probe():
        return None

    notify = Recorder()
    monitor = ConnectionMonitor(probe, notify)
    assert run(monitor.check()) == {"state": ONLINE}
    assert notify.sent == []


def test_a_probe_that_fails_is_offline_and_tells_once():
    async def probe():
        raise requests.ConnectionError("Name or service not known")

    notify = Recorder()
    monitor = ConnectionMonitor(probe, notify)

    async def twice():
        first = await monitor.check()
        second = await monitor.check()
        return first, second

    first, second = run(twice())
    assert first["state"] == OFFLINE and "not known" in first["detail"]
    assert second["state"] == OFFLINE
    assert [m for m, _ in notify.sent] == ["connection/changed"]
    assert notify.sent[0][1]["state"] == OFFLINE


def test_a_probe_that_hangs_is_offline(monkeypatch):
    import rpc_common.connection as connection

    monkeypatch.setattr(connection, "PROBE_TIMEOUT_SEC", 0.05)

    async def probe():
        await asyncio.sleep(5)

    assert run(ConnectionMonitor(probe).check())["state"] == OFFLINE


def test_only_a_network_failure_makes_it_offline():
    async def probe():
        return None

    notify = Recorder()
    monitor = ConnectionMonitor(probe, notify)

    async def scenario():
        assert not await monitor.report_failure(ValueError("bad data"))
        assert monitor.state == ONLINE
        assert await monitor.report_failure(socket.gaierror("no DNS"))
        assert monitor.state == OFFLINE

    run(scenario())
    assert notify.sent == [("connection/changed", {"state": OFFLINE, "detail": "no DNS"})]


def test_something_working_while_offline_gets_it_probed_back_online():
    calls = []

    async def probe():
        calls.append(1)

    notify = Recorder()
    monitor = ConnectionMonitor(probe, notify)

    async def scenario():
        await monitor.report_failure(ConnectionError("down"))
        monitor.reached_maybe()
        await asyncio.sleep(0.01)

    run(scenario())
    assert monitor.state == ONLINE
    assert calls == [1]
    assert [p["state"] for _, p in notify.sent] == [OFFLINE, ONLINE]


def test_the_server_answers_connection_check_and_declares_it():
    from rpc_common.server import BackendServer

    async def probe():
        return None

    server = BackendServer("t", "T", "0", "test", {})
    server.set_connection_probe(probe)
    assert server.capabilities["connectionCheck"] is True
    handler = server._handlers["connection.check"]
    assert run(handler({}, 1)) == {"state": ONLINE}
