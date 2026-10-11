import asyncio

import pytest
import pytest_asyncio

from cloudmus_tui.playback_engine import PlaybackEngine


class _FakeClient:
    def __init__(self):
        self.calls: list[tuple[str, dict]] = []
        self.cancelled: list[dict] = []
        self._next_id = 1

    async def start_call(self, method, params=None, timeout=5.0):
        self.calls.append((method, params or {}))
        request_id = self._next_id
        self._next_id += 1

        async def _pending():
            return {"accepted": True}

        return request_id, asyncio.ensure_future(_pending())

    async def call(self, method, params=None, timeout=5.0):
        request_id, pending = await self.start_call(method, params, timeout)
        result = await pending
        return request_id, result

    async def request(self, method, params=None, timeout=5.0):
        _, result = await self.call(method, params, timeout)
        return result

    async def notify_fire_and_forget(self, method, params=None):
        self.cancelled.append(params or {})


class _FakeSourceManager:
    def __init__(self, client):
        self.clients = {"local-folder": client}


def _track(track_id: str, title: str = "Song") -> dict:
    return {"id": track_id, "title": title, "artists": [{"id": "a", "name": "Artist"}], "durationMs": 1000}


@pytest_asyncio.fixture
async def engine():
    client = _FakeClient()
    sm = _FakeSourceManager(client)
    eng = PlaybackEngine(sm, on_track_change=None, on_error=None)
    yield eng, client
    eng.shutdown()


@pytest.mark.asyncio
async def test_load_queue_issues_play_for_first_track(engine):
    eng, client = engine
    await eng.load_queue("local-folder", [_track("1"), _track("2")], start_index=0)
    assert client.calls == [("playback.play", {"trackId": "1"})]
    assert eng.current().track["id"] == "1"


@pytest.mark.asyncio
async def test_next_cancels_previous_and_advances(engine):
    eng, client = engine
    await eng.load_queue("local-folder", [_track("1"), _track("2")], start_index=0)
    first_request_id = eng._latest_request_id

    await eng.next()

    assert eng.current().track["id"] == "2"
    assert client.cancelled == [{"requestId": first_request_id}]
    assert client.calls[-1] == ("playback.play", {"trackId": "2"})


@pytest.mark.asyncio
async def test_stream_ready_discarded_if_requestid_stale(engine):
    eng, client = engine
    await eng.load_queue("local-folder", [_track("1"), _track("2")], start_index=0)
    stale_request_id = eng._latest_request_id

    await eng.next()  # supersedes the first play

    # Stale notification for the first (superseded) request must be ignored.
    eng.handle_stream_ready("local-folder", stale_request_id, {"url": "file:///stale.mp3", "mimeType": "audio/mpeg"})
    assert eng._resolved_request_id != stale_request_id

    current_request_id = eng._latest_request_id
    eng.handle_stream_ready("local-folder", current_request_id, {"url": "file:///current.mp3", "mimeType": "audio/mpeg"})
    assert eng._resolved_request_id == current_request_id


@pytest.mark.asyncio
async def test_queue_exhausted_reports_none(engine):
    eng, client = engine
    calls = []
    eng.on_track_change = lambda entry: calls.append(entry)
    await eng.load_queue("local-folder", [_track("1")], start_index=0)
    await eng.next()  # advances past the only track
    assert eng.current() is None
    assert calls[-1] is None


@pytest.mark.asyncio
async def test_unavailable_track_error_skips_to_the_next_at_once(engine):
    eng, client = engine
    errors = []
    eng.on_error = errors.append
    await eng.load_queue("local-folder", [_track("1", "One"), _track("2")], start_index=0)

    handled = eng.handle_track_error("local-folder", {"trackId": "1", "reason": "unavailable"})
    await asyncio.sleep(0.05)  # the advance runs as a task

    assert handled
    assert eng.current().track["id"] == "2"
    assert client.calls[-1] == ("playback.play", {"trackId": "2"})
    assert errors and "One" in errors[0]


@pytest.mark.asyncio
async def test_offline_error_stops_instead_of_skipping_through_the_queue(engine):
    eng, client = engine
    await eng.load_queue("local-folder", [_track("1"), _track("2")], start_index=0)

    assert eng.handle_track_error("local-folder", {"trackId": "1", "reason": "offline"})
    await asyncio.sleep(0.05)

    assert eng.current().track["id"] == "1"
    assert len([c for c in client.calls if c[0] == "playback.play"]) == 1


@pytest.mark.asyncio
async def test_error_about_another_track_is_not_handled(engine):
    eng, _ = engine
    await eng.load_queue("local-folder", [_track("1"), _track("2")], start_index=0)

    assert not eng.handle_track_error("local-folder", {"trackId": "2", "reason": "unavailable"})
    assert not eng.handle_track_error("local-folder", {"trackId": "1"})
