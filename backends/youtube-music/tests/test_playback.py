import asyncio

import pytest
import yt_dlp

from cloudmus_backend_ytmusic import playback


@pytest.fixture(autouse=True)
def _no_network(monkeypatch):
    monkeypatch.setattr(playback, "_stream_accepted", lambda url, headers: True)


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


@pytest.mark.asyncio
async def test_rejected_url_is_re_resolved_with_a_fresh_session(monkeypatch):
    monkeypatch.setattr(playback, "RETRY_INITIAL_DELAY", 0.01)
    monkeypatch.setattr(playback, "RETRY_MAX_DELAY", 0.01)
    monkeypatch.setattr(playback, "_extract_info", lambda vid: {"formats": [_format(abr=128, url="https://u")]})
    verdicts = iter([False, True])
    monkeypatch.setattr(playback, "_stream_accepted", lambda url, headers: next(verdicts))
    dropped = {"n": 0}
    monkeypatch.setattr(playback, "_drop_session", lambda: dropped.__setitem__("n", dropped["n"] + 1))

    stream = await playback.resolve_stream_with_retry("abc", asyncio.Event())
    assert stream.url == "https://u"
    assert dropped["n"] == 1


@pytest.mark.asyncio
async def test_always_rejected_gives_up(monkeypatch):
    monkeypatch.setattr(playback, "RETRY_INITIAL_DELAY", 0.01)
    monkeypatch.setattr(playback, "RETRY_MAX_DELAY", 0.01)
    monkeypatch.setattr(playback, "_extract_info", lambda vid: {"formats": [_format(abr=128)]})
    monkeypatch.setattr(playback, "_stream_accepted", lambda url, headers: False)
    with pytest.raises(RuntimeError):
        await playback.resolve_stream_with_retry("abc", asyncio.Event())


def test_session_is_dropped_on_extraction_error_and_after_max_age(monkeypatch):
    created = []

    class FakeYdl:
        def __init__(self, opts):
            created.append(self)
            self.fail = False

        def extract_info(self, url, download=False):
            if self.fail:
                raise TimeoutError("boom")
            return {"formats": []}

    monkeypatch.setattr(playback.yt_dlp, "YoutubeDL", FakeYdl)
    monkeypatch.setattr(playback, "_ydl", None)
    playback._extract_info("a")
    playback._extract_info("b")
    assert len(created) == 1  # reused

    created[0].fail = True
    with pytest.raises(TimeoutError):
        playback._extract_info("c")
    playback._extract_info("d")
    assert len(created) == 2  # dropped after the failure

    monkeypatch.setattr(playback, "YDL_MAX_AGE", -1)
    playback._extract_info("e")
    assert len(created) == 3  # too old


def _tier(format_id, acodec, abr, **kw):
    return {"format_id": format_id, "acodec": acodec, "vcodec": "none", "abr": abr, "ext": "webm", **kw}


_YOUTUBE_FORMATS = [
    _tier("139", "mp4a.40.5", 48.8, ext="m4a"),
    _tier("249", "opus", 46.2),
    _tier("250", "opus", 61.1),
    _tier("140", "mp4a.40.2", 129.5, ext="m4a"),
    _tier("251", "opus", 128.9),
]


@pytest.mark.parametrize("level, expected", [("best", "251"), ("medium", "250"), ("low", "249")])
def test_quality_caps_the_bitrate(level, expected):
    from cloudmus_backend_ytmusic import quality

    assert quality.pick(_YOUTUBE_FORMATS, level)["format_id"] == expected


def test_quality_prefers_opus_over_aac_at_the_same_tier():
    from cloudmus_backend_ytmusic import quality

    formats = [_tier("140", "mp4a.40.2", 129.9, ext="m4a"), _tier("251", "opus", 128.9)]
    assert quality.pick(formats, "best")["format_id"] == "251"


def test_quality_falls_back_to_the_lowest_above_the_cap():
    from cloudmus_backend_ytmusic import quality

    assert quality.pick([_tier("140", "mp4a.40.2", 129.5), _tier("251", "opus", 128.9)], "low")["format_id"] == "251"


def test_quality_prefers_the_original_language_and_uncompressed_audio():
    from cloudmus_backend_ytmusic import quality

    formats = [
        _tier("251-dub", "opus", 130, language_preference=-1),
        _tier("251-drc", "opus", 130, language_preference=10),
        _tier("251", "opus", 129, language_preference=10),
    ]
    assert quality.pick(formats, "best")["format_id"] == "251"


@pytest.mark.asyncio
async def test_stream_uses_the_quality_level(monkeypatch):
    formats = [_format(abr=128, url="https://high"), _format(abr=62, url="https://mid"), _format(abr=48, url="https://low")]
    monkeypatch.setattr(playback, "_extract_info", lambda vid: {"formats": formats})
    stream = await playback.resolve_stream_with_retry("abc", asyncio.Event(), "medium")
    assert stream.url == "https://mid"
