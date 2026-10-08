import asyncio

import pytest

from rpc_common import jsonrpc, models
from rpc_common.testing import (
    ScriptedBackend,
    make_duplex_pair,
    self_playback_fixture,
    stream_ready_race_fixture,
)


def test_jsonrpc_envelopes():
    req = jsonrpc.make_request(1, "catalog.listPlaylists", {})
    assert req == {"jsonrpc": "2.0", "id": 1, "method": "catalog.listPlaylists", "params": {}}
    assert jsonrpc.is_request(req)

    notif = jsonrpc.make_notification("state/changed", {"state": "playing"})
    assert "id" not in notif
    assert jsonrpc.is_notification(notif)

    result = jsonrpc.make_result(1, {"ok": True})
    assert jsonrpc.is_response(result)

    error = jsonrpc.make_error(1, 1300, "not found", {"retryable": False})
    assert jsonrpc.is_response(error)
    assert error["error"]["code"] == 1300


def test_models_round_trip():
    track = models.Track(
        id="1",
        title="Song",
        artists=[models.Artist(id="a1", name="Artist")],
        durationMs=1000,
    )
    d = track.to_dict()
    assert "album" not in d  # unset optional fields are dropped
    back = models.Track.from_dict(d)
    assert back.id == "1"
    assert back.artists[0].name == "Artist"


@pytest.mark.asyncio
async def test_scripted_backend_request_response():
    front_writer, front_reader, backend_writer, backend_reader = make_duplex_pair()
    backend = ScriptedBackend(handlers={
        "catalog.listPlaylists": lambda p: {"playlists": []},
        "shutdown": lambda p: {},
    })
    serve_task = asyncio.create_task(backend.serve(backend_writer, backend_reader))

    await front_writer.send(jsonrpc.make_request(1, "catalog.listPlaylists", {}))
    response = await front_reader.__anext__()
    assert response == {"jsonrpc": "2.0", "id": 1, "result": {"playlists": []}}

    await front_writer.send(jsonrpc.make_request(2, "shutdown", {}))
    await front_reader.__anext__()
    await asyncio.wait_for(serve_task, timeout=1)


@pytest.mark.asyncio
async def test_stream_ready_race_fixture_discards_stale_requestid():
    front_writer, front_reader, backend_writer, backend_reader = make_duplex_pair()
    backend = stream_ready_race_fixture()
    serve_task = asyncio.create_task(backend.serve(backend_writer, backend_reader))

    await front_writer.send(jsonrpc.make_request(1, "playback.play", {"trackId": "first"}))
    await front_writer.send(jsonrpc.make_request(2, "playback.play", {"trackId": "second"}))

    latest_request_id = 2
    accepted_stream = None
    for _ in range(4):
        message = await front_reader.__anext__()
        if message.get("method") == "track/streamReady":
            if message["params"]["requestId"] == latest_request_id:
                accepted_stream = message["params"]["trackId"]

    assert accepted_stream == "second"

    await front_writer.send(jsonrpc.make_request(3, "shutdown", {}))
    await front_reader.__anext__()
    await asyncio.wait_for(serve_task, timeout=1)


@pytest.mark.asyncio
async def test_self_playback_fixture():
    front_writer, front_reader, backend_writer, backend_reader = make_duplex_pair()
    backend = self_playback_fixture()
    serve_task = asyncio.create_task(backend.serve(backend_writer, backend_reader))

    await front_writer.send(jsonrpc.make_request(1, "initialize", {}))
    response = await front_reader.__anext__()
    assert response["result"]["capabilities"]["playback"]["selfPlayback"] is True

    await front_writer.send(jsonrpc.make_request(2, "playback.play", {"trackId": "x"}))
    response = await front_reader.__anext__()
    assert response["result"] == {"accepted": True}

    await front_writer.send(jsonrpc.make_request(3, "shutdown", {}))
    await front_reader.__anext__()
    await asyncio.wait_for(serve_task, timeout=1)


def test_stderr_is_utf8_whatever_the_system_code_page():
    # The front reads a backend's stderr as UTF-8; on Windows Python would
    # write the code page's encoding (here forced to cp1251) instead.
    import os
    import subprocess
    import sys

    script = (
        "import logging\n"
        "from rpc_common.transport import install_stdout_purity_guard\n"
        "install_stdout_purity_guard()\n"
        "logging.getLogger('x').info('Моя волна ✓')\n"
        "print('Плейлист')\n"
    )
    env = {**os.environ, "PYTHONIOENCODING": "cp1251", "PYTHONUTF8": "0"}
    result = subprocess.run(
        [sys.executable, "-c", script], env=env, capture_output=True, check=True
    )
    text = result.stderr.decode("utf-8")  # raises on a code page's bytes
    assert "Моя волна ✓" in text
    assert "Плейлист" in text
    assert result.stdout == b""
