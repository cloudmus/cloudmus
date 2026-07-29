import asyncio

import pytest

from cloudmus_backend_yandex import playback


class _FakeInfo:
    def __init__(self, codec, bitrate, link):
        self.codec = codec
        self.bitrate_in_kbps = bitrate
        self.direct_link = link


class _FakeTrack:
    def __init__(self, infos):
        self._infos = infos

    def get_download_info(self, get_direct_links=False):
        return self._infos


class _FakeClient:
    def __init__(self, tracks_by_call):
        self._tracks_by_call = tracks_by_call
        self.calls = 0

    def tracks(self, ids):
        result = self._tracks_by_call[min(self.calls, len(self._tracks_by_call) - 1)]
        self.calls += 1
        return result


@pytest.mark.asyncio
async def test_resolves_best_mp3_by_bitrate(monkeypatch):
    infos = [
        _FakeInfo("mp3", 128, "https://low"),
        _FakeInfo("mp3", 320, "https://high"),
        _FakeInfo("aac", 500, "https://aac"),
    ]
    client = _FakeClient([[_FakeTrack(infos)]])
    stream = await playback.resolve_stream_with_retry(client, "123", asyncio.Event())
    assert stream.url == "https://high"
    assert stream.mimeType == "audio/mpeg"


@pytest.mark.asyncio
async def test_retries_then_succeeds(monkeypatch):
    monkeypatch.setattr(playback, "RETRY_INITIAL_DELAY", 0.01)
    monkeypatch.setattr(playback, "RETRY_MAX_DELAY", 0.01)

    good_infos = [_FakeInfo("mp3", 320, "https://ok")]
    client = _FakeClient([[], [], [_FakeTrack(good_infos)]])  # first two calls: no tracks found

    stream = await playback.resolve_stream_with_retry(client, "123", asyncio.Event())
    assert stream.url == "https://ok"
    assert client.calls == 3


@pytest.mark.asyncio
async def test_gives_up_after_max_attempts(monkeypatch):
    monkeypatch.setattr(playback, "RETRY_INITIAL_DELAY", 0.01)
    monkeypatch.setattr(playback, "RETRY_MAX_DELAY", 0.01)
    monkeypatch.setattr(playback, "MAX_ATTEMPTS", 2)

    client = _FakeClient([[]])  # always empty -> always fails
    with pytest.raises(RuntimeError):
        await playback.resolve_stream_with_retry(client, "123", asyncio.Event())
    assert client.calls == 2


@pytest.mark.asyncio
async def test_cancel_event_aborts_retry(monkeypatch):
    monkeypatch.setattr(playback, "RETRY_INITIAL_DELAY", 5.0)
    monkeypatch.setattr(playback, "RETRY_MAX_DELAY", 5.0)

    client = _FakeClient([[]])
    cancel_event = asyncio.Event()

    async def cancel_soon():
        await asyncio.sleep(0.05)
        cancel_event.set()

    asyncio.create_task(cancel_soon())
    with pytest.raises(asyncio.CancelledError):
        await playback.resolve_stream_with_retry(client, "123", cancel_event)
