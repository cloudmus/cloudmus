import asyncio

import pytest

from cloudmus_backend_yandex import client as client_module, playback, server
from rpc_common.generated.models import StreamDescriptor
from rpc_common.server import BackendError


@pytest.mark.asyncio
async def test_resolve_stream_returns_descriptor_without_starting_playback(monkeypatch):
    async def resolve(client, track_id, cancel_event, quality_level=None):
        return StreamDescriptor(kind="url", url=f"https://cdn/{track_id}", mimeType="audio/mpeg")

    monkeypatch.setattr(client_module, "get_client", lambda: object())
    monkeypatch.setattr(playback, "resolve_stream_with_retry", resolve)
    backend = server.build_server()

    result = await backend._handlers["playback.resolveStream"]({"trackId": "42"}, 7)

    assert result["stream"]["url"] == "https://cdn/42"


@pytest.mark.asyncio
async def test_cancel_stops_an_in_flight_resolution(monkeypatch):
    started = asyncio.Event()

    async def resolve(client, track_id, cancel_event, quality_level=None):
        started.set()
        await cancel_event.wait()
        raise asyncio.CancelledError

    monkeypatch.setattr(client_module, "get_client", lambda: object())
    monkeypatch.setattr(playback, "resolve_stream_with_retry", resolve)
    backend = server.build_server()

    pending = asyncio.create_task(backend._handlers["playback.resolveStream"]({"trackId": "42"}, 7))
    await started.wait()
    backend._handlers["playback.cancel"]({"requestId": 7}, 8)

    with pytest.raises(BackendError):
        await asyncio.wait_for(pending, 1)
