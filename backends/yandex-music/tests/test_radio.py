import pytest

from cloudmus_backend_yandex.radio import RadioSession, _resolve_station


class _TrackDict:
    """Minimal but valid yandex_music track dicts for Track.de_json."""

    @staticmethod
    def make(id_, title=None):
        return {"id": str(id_), "title": title or f"Track {id_}", "durationMs": 1000}


class _FakeRequest:
    def __init__(self, client):
        self.client = client

    def post(self, url, json=None, **kwargs):
        return self.client._respond(url, json)


class _FakeClient:
    def __init__(self):
        self.base_url = "https://api.music.yandex.net"
        self.report_unknown_fields = False
        self._request = _FakeRequest(self)
        self.calls = []  # (url, json) per post call
        self._next_id = 1
        self._tracks_calls = 0

    def _track_dict(self):
        id_ = self._next_id
        self._next_id += 1
        return _TrackDict.make(id_)

    def _respond(self, url, json):
        self.calls.append((url, json))
        if url.endswith("/rotor/session/new"):
            return {
            "radioSessionId": "sess-1",
            "batchId": "batch-0",
            "sequence": [
                {"type": "track", "track": self._track_dict()},
                {"type": "track", "track": self._track_dict()},
            ],
        }
        if url.endswith("/rotor/session/sess-1/tracks"):
            self._tracks_calls += 1
            fresh = [{"type": "track", "track": self._track_dict()}]
            # When the dedup-under-test passes a queue for an id the server
            # has already served, the server may (incorrectly) re-serve it —
            # simulate the id that is queued being re-served right back.
            queued_id = json["queue"][0] if json and json.get("queue") else None
            served = fresh
            if queued_id and self._tracks_calls % 2 == 0:
                served = [{"type": "track", "track": {"id": str(queued_id), "title": "Repeat", "durationMs": 1000}}]
            return {
                "batchId": f"batch-{self._tracks_calls}",
                "sequence": served,
            }
        if url.endswith("/rotor/session/sess-1/feedback"):
            return {}
        raise AssertionError(f"unexpected url: {url}")


class _NotifyRecorder:
    def __init__(self):
        self.events = []

    async def __call__(self, method, params):
        self.events.append((method, params))


def test_resolve_station_defaults_to_my_wave_when_no_seed():
    assert _resolve_station(None) == "user:onyourwave"
    assert _resolve_station("") == "user:onyourwave"


def test_resolve_station_prefixes_a_bare_track_id():
    # Regression: the front used to send "track:<id>" itself; now it sends
    # the bare id (seed is source-defined and must not be pre-formatted by
    # the front, since a second radio-capable backend — YouTube — needs a
    # different shape for the same bare id) and this backend prefixes it.
    assert _resolve_station("12345") == "track:12345"


def test_resolve_station_passes_through_an_already_complete_station_address():
    # A full "<type>:<id>" address (e.g. My Wave's own Playlist.id, echoed
    # verbatim by the front) is used as-is, not double-prefixed.
    assert _resolve_station("user:onyourwave") == "user:onyourwave"
    assert _resolve_station("genre:pop") == "genre:pop"


@pytest.mark.asyncio
async def test_start_creates_session_and_sends_radio_started_feedback():
    # The legacy flow called the dead /rotor/station/.../feedback endpoint;
    # the session flow must POST /rotor/session/new to bootstrap the session
    # and then ack with a radioStarted event on the *session* endpoint.
    client = _FakeClient()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)

    result = await session.start(seed=None)

    session_new = next(c for c in client.calls if c[0].endswith("/rotor/session/new"))
    assert session_new[1]["seeds"] == ["user:onyourwave"]
    assert session.radio_session_id == "sess-1"
    assert session.batch_id == "batch-0"
    assert len(result["initialTracks"]) == 2

    feedback = next(c for c in client.calls if c[0].endswith("/feedback"))
    assert feedback[1]["batch_id"] == "batch-0"
    assert feedback[1]["event"]["type"] == "radioStarted"
    assert feedback[1]["event"]["from"] == "cloudmus"


@pytest.mark.asyncio
async def test_top_up_passes_played_track_as_queue_on_session_endpoint():
    # Regression: the session API's /rotor/session/{id}/tracks needs
    # queue=[<just-played track id>] to advance the chain (see radio.py's
    # _top_up() doc comment) — omitting it made the wave re-serve the same
    # batch forever instead of progressing.
    client = _FakeClient()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)

    await session.track_finished("2", played_ms=30000)
    tracks_call = next(c for c in client.calls if c[0].endswith("/tracks"))
    assert tracks_call[1] == {"queue": ["2"]}
    assert session.batch_id == "batch-1"


@pytest.mark.asyncio
async def test_feedback_events_go_through_session_endpoint_with_batch_id():
    # All feedback (not just top-up) must use /rotor/session/{id}/feedback
    # — the legacy /rotor/station/.../feedback endpoint returns
    # 400 "condition is not met" for every event type.
    client = _FakeClient()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)

    await session.track_started("1")
    await session.track_finished("2", played_ms=12000)
    await session.skip("3", played_ms=2500)

    feedbacks = [c for c in client.calls if c[0].endswith("/feedback")]
    assert len(feedbacks) == 4  # radioStarted + the three events above

    by_type = {c[1]["event"]["type"]: c[1] for c in feedbacks}
    assert by_type["trackStarted"]["event"]["trackId"] == "1"
    assert by_type["trackFinished"]["event"]["totalPlayedSeconds"] == 12.0
    assert by_type["skip"]["event"]["totalPlayedSeconds"] == 2.5
    # trackFinished advances the session (updates batch_id), so skip goes
    # out against the *new* batch.
    assert by_type["trackStarted"]["batch_id"] == "batch-0"
    assert by_type["trackFinished"]["batch_id"] == "batch-0"
    assert by_type["skip"]["batch_id"] == "batch-1"


@pytest.mark.asyncio
async def test_top_up_emits_only_tracks_not_seen_this_session():
    # The front must never receive a track it was already shown in the
    # initial batch. _FakeClient re-serves the queued id on alternating
    # tracks calls; the session must filter those out and only push fresh.
    client = _FakeClient()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)

    await session._top_up("1")  # advances, server returns a fresh id
    added = [p for m, p in notifier.events if m == "radio/tracksAdded"]
    assert len(added) == 1
    fresh_ids = [t["id"] for t in added[0]["tracks"]]
    assert len(fresh_ids) == 1

    # Alternating call: server re-serves the queued id (already seen) — must
    # not be pushed again.
    await session._top_up("1")
    added = [p for m, p in notifier.events if m == "radio/tracksAdded"]
    assert len(added) == 1