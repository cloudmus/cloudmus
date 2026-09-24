import asyncio

import pytest
import yt_dlp

from cloudmus_backend_ytmusic import playback


def _format(acodec="opus", vcodec="none", abr=0, url="https://x", ext="webm", http_headers=None):
    f = {"acodec": acodec, "vcodec": vcodec, "abr": abr, "url": url, "ext": ext}
    if http_headers is not None:
        f["http_headers"] = http_headers
    return f


@pytest.mark.asyncio
async def test_resolves_best_audio_by_abr(monkeypatch):
    monkeypatch.setattr(
        playback,
        "_extract_info",
        lambda vid: {
            "formats": [
                _format(abr=70, url="https://low"),
                _format(abr=160, url="https://high", http_headers={"User-Agent": "x"}),
                _format(acodec="opus", vcodec="mp4a", abr=999, url="https://video"),  # not audio-only
            ]
        },
    )
    stream = await playback.resolve_stream_with_retry("abc", asyncio.Event())
    assert stream.url == "https://high"
    assert stream.headers == {"User-Agent": "x"}
    assert stream.mimeType == "audio/webm"


@pytest.mark.asyncio
async def test_retries_then_succeeds(monkeypatch):
    monkeypatch.setattr(playback, "RETRY_INITIAL_DELAY", 0.01)
    monkeypatch.setattr(playback, "RETRY_MAX_DELAY", 0.01)

    calls = {"n": 0}

    def fake_extract(vid):
        calls["n"] += 1
        if calls["n"] < 3:
            raise TimeoutError("transient network blip")
        return {"formats": [_format(abr=128, url="https://ok")]}

    monkeypatch.setattr(playback, "_extract_info", fake_extract)
    stream = await playback.resolve_stream_with_retry("abc", asyncio.Event())
    assert stream.url == "https://ok"
    assert calls["n"] == 3


@pytest.mark.asyncio
async def test_gives_up_after_max_attempts(monkeypatch):
    monkeypatch.setattr(playback, "RETRY_INITIAL_DELAY", 0.01)
    monkeypatch.setattr(playback, "RETRY_MAX_DELAY", 0.01)
    monkeypatch.setattr(playback, "MAX_ATTEMPTS", 2)

    calls = {"n": 0}

    def fake_extract(vid):
        calls["n"] += 1
        raise TimeoutError("transient network blip")

    monkeypatch.setattr(playback, "_extract_info", fake_extract)
    with pytest.raises(RuntimeError):
        await playback.resolve_stream_with_retry("abc", asyncio.Event())
    assert calls["n"] == 2


@pytest.mark.asyncio
async def test_cancel_event_aborts_retry(monkeypatch):
    monkeypatch.setattr(playback, "RETRY_INITIAL_DELAY", 5.0)
    monkeypatch.setattr(playback, "RETRY_MAX_DELAY", 5.0)

    def fake_extract(vid):
        raise TimeoutError("transient network blip")

    monkeypatch.setattr(playback, "_extract_info", fake_extract)

    cancel_event = asyncio.Event()

    async def cancel_soon():
        await asyncio.sleep(0.05)
        cancel_event.set()

    asyncio.create_task(cancel_soon())
    with pytest.raises(asyncio.CancelledError):
        await playback.resolve_stream_with_retry("abc", cancel_event)


@pytest.mark.asyncio
async def test_fails_fast_on_non_transient_error(monkeypatch):
    calls = {"n": 0}

    def fake_extract(vid):
        calls["n"] += 1
        raise yt_dlp.utils.DownloadError("ERROR: [youtube] abc: Video unavailable")

    monkeypatch.setattr(playback, "_extract_info", fake_extract)
    with pytest.raises(LookupError):
        await playback.resolve_stream_with_retry("abc", asyncio.Event())
    assert calls["n"] == 1


@pytest.mark.asyncio
async def test_fails_immediately_when_no_audio_only_formats(monkeypatch):
    calls = {"n": 0}

    def fake_extract(vid):
        calls["n"] += 1
        return {"formats": [_format(acodec="none", vcodec="avc1")]}  # video-only

    monkeypatch.setattr(playback, "_extract_info", fake_extract)
    with pytest.raises(LookupError):
        await playback.resolve_stream_with_retry("abc", asyncio.Event())
    assert calls["n"] == 1
