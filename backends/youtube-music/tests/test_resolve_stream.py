import asyncio

import pytest

from cloudmus_backend_ytmusic import playback, server
from rpc_common.generated.models import StreamDescriptor
from rpc_common.server import BackendError


@pytest.mark.asyncio
async def test_resolve_stream_returns_descriptor_without_starting_playback(monkeypatch):
    async def resolve(track_id, cancel_event, quality_level=None):
        return StreamDescriptor(kind="url", url=f"https://cdn/{track_id}", mimeType="audio/webm")

    monkeypatch.setattr(playback, "resolve_stream_with_retry", resolve)
    backend = server.build_server()

    result = await backend._handlers["playback.resolveStream"]({"trackId": "42"}, 7)

    assert result["stream"]["url"] == "https://cdn/42"


@pytest.mark.asyncio
async def test_cancel_stops_an_in_flight_resolution(monkeypatch):
    started = asyncio.Event()

    async def resolve(track_id, cancel_event, quality_level=None):
        started.set()
        await cancel_event.wait()
        raise asyncio.CancelledError

    monkeypatch.setattr(playback, "resolve_stream_with_retry", resolve)
    backend = server.build_server()

    pending = asyncio.create_task(backend._handlers["playback.resolveStream"]({"trackId": "42"}, 7))
    await started.wait()
    backend._handlers["playback.cancel"]({"requestId": 7}, 8)

    with pytest.raises(BackendError):
        await asyncio.wait_for(pending, 1)


@pytest.mark.asyncio
async def test_unavailable_and_offline_errors_carry_the_track_and_reason(monkeypatch):
    from rpc_common.errors import OfflineError

    backend = server.build_server()

    async def unavailable(track_id, cancel_event, quality_level=None):
        raise LookupError("video unavailable")

    monkeypatch.setattr(playback, "resolve_stream_with_retry", unavailable)
    with pytest.raises(BackendError) as info:
        await backend._handlers["playback.resolveStream"]({"trackId": "42"}, 7)
    assert info.value.code == 1300
    assert info.value.data == {"retryable": False, "trackId": "42", "reason": "unavailable"}

    async def offline(track_id, cancel_event, quality_level=None):
        raise OfflineError("no network")

    monkeypatch.setattr(playback, "resolve_stream_with_retry", offline)
    with pytest.raises(BackendError) as info:
        await backend._handlers["playback.resolveStream"]({"trackId": "42"}, 8)
    assert info.value.code == 1200
    assert info.value.data == {"retryable": True, "trackId": "42", "reason": "offline"}
