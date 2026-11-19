import asyncio

import pytest
from yt_dlp.utils import DownloadError

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


@pytest.mark.asyncio
async def test_youtube_bot_check_is_a_retryable_upstream_error(monkeypatch):
    def extract(video_id):
        raise DownloadError(f"ERROR: [youtube] {video_id}: Sign in to confirm you’re not a bot.")

    monkeypatch.setattr(playback, "_extract_info", extract)
    monkeypatch.setattr(playback, "RETRY_INITIAL_DELAY", 0.01)
    monkeypatch.setattr(playback, "RETRY_MAX_DELAY", 0.01)
    backend = server.build_server()

    with pytest.raises(BackendError, match="not a bot") as info:
        await backend._handlers["playback.resolveStream"]({"trackId": "42"}, 7)

    assert info.value.code == 1200
    assert info.value.data == {"retryable": True, "trackId": "42"}
