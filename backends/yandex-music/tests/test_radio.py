from types import SimpleNamespace

import pytest

from cloudmus_backend_yandex.radio import RadioSession, _resolve_station


class _TrackDict:
    """Minimal but valid yandex_music track dicts for Track.de_json."""

    @staticmethod
    def make(id_, title=None, album_id=None):
        track = {"id": str(id_), "title": title or f"Track {id_}", "durationMs": 1000}
        if album_id is not None:
            track["albums"] = [{"id": album_id}]
        return track


class _FakeRequest:
    def __init__(self, client):
        self.client = client

    def post(self, url, json=None, **kwargs):
        return self.client._respond(url, json)


class _FakeClient:
    def __init__(self, with_albums=False, batch_size=None):
        # with_albums: served tracks carry an album, so their track_id (and
        # the protocol Track.id the front reports back) is "<id>:<albumId>",
        # as for real Yandex tracks. batch_size: serve that many fresh
        # tracks per call, never re-serving the queued one (a well-behaved
        # server) — by default 2 on start and 1 per top-up, with re-serves.
        self.with_albums = with_albums
        self.batch_size = batch_size
        self.base_url = "https://api.music.yandex.net"
        self.report_unknown_fields = False
        self._request = _FakeRequest(self)
        self.calls = []  # (url, json) per post call
        self._next_id = 1
        self._tracks_calls = 0

    def _track_dict(self):
        id_ = self._next_id
        self._next_id += 1
        return _TrackDict.make(id_, album_id=100 + id_ if self.with_albums else None)

    def _respond(self, url, json):
        self.calls.append((url, json))
        if url.endswith("/rotor/session/new"):
            return {
            "radioSessionId": "sess-1",
            "batchId": "batch-0",
            "sequence": [
                {"type": "track", "track": self._track_dict()} for _ in range(self.batch_size or 2)
            ],
        }
        if url.endswith("/rotor/session/sess-1/tracks"):
            self._tracks_calls += 1
            fresh = [{"type": "track", "track": self._track_dict()} for _ in range(self.batch_size or 1)]
            # When the dedup-under-test passes a queue for an id the server
            # has already served, the server may (incorrectly) re-serve it —
            # simulate the id that is queued being re-served right back.
            queued_id = json["queue"][0] if json and json.get("queue") else None
            served = fresh
            if queued_id and self._tracks_calls % 2 == 0 and not self.batch_size:
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


def test_resolve_station_turns_a_track_album_id_into_a_track_station():
    # Regression: this backend's own Track.id is "<trackId>:<albumId>" —
    # its colon must not make it pass through as if it were already a
    # station address (Yandex then silently plays the generic wave).
    assert _resolve_station("12345:6789") == "track:12345"


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
async def test_top_up_replaces_upcoming_with_unplayed_tracks():
    # Every top-up is the wave's recomputed upcoming sequence: it's sent
    # with replaceUpcoming so the front swaps its unplayed tail instead of
    # growing the queue by a whole batch per track.
    client = _FakeClient()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)
    await session.track_started("1")

    await session._top_up("1", replace=True)  # advances, server returns a fresh id
    added = [p for m, p in notifier.events if m == "radio/tracksAdded"]
    assert len(added) == 1
    assert added[0]["replaceUpcoming"] is True
    assert [t["id"] for t in added[0]["tracks"]] == ["3"]


@pytest.mark.asyncio
async def test_top_up_never_serves_an_already_played_track_as_upcoming():
    client = _FakeClient()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)
    await session.track_started("1")

    await session._top_up("1", replace=True)
    # Alternating call: the server re-serves the queued id, which already
    # played — nothing upcoming is left, so nothing is pushed.
    await session._top_up("1", replace=True)
    added = [p for m, p in notifier.events if m == "radio/tracksAdded"]
    assert len(added) == 1


@pytest.mark.asyncio
async def test_top_up_may_reserve_a_served_but_unplayed_track():
    # Track "2" was served in the initial batch but never started — a later
    # sequence recommending it again is legitimate (it replaces the tail).
    client = _FakeClient()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)

    await session._top_up("2", replace=True)
    await session._top_up("2", replace=True)  # re-serves "2", unplayed
    added = [p for m, p in notifier.events if m == "radio/tracksAdded"]
    assert [t["id"] for t in added[-1]["tracks"]] == ["2"]


@pytest.mark.asyncio
async def test_track_station_adds_the_tracks_genre_as_a_second_seed():
    # A track seed alone is the personal wave barely nudged by the track;
    # the track's genre keeps the station in its style.
    client = _FakeClient()
    client.tracks = lambda ids: [SimpleNamespace(albums=[SimpleNamespace(genre="folk")])]
    session = RadioSession(client, _NotifyRecorder())
    await session.start(seed="150804586:41815842")
    new = [json for url, json in client.calls if url.endswith("/rotor/session/new")]
    assert new[0]["seeds"] == ["track:150804586", "genre:folk"]
    assert session.station == "track:150804586"


@pytest.mark.asyncio
async def test_track_station_falls_back_to_the_track_seed_without_a_genre():
    client = _FakeClient()  # no tracks() at all — the lookup fails
    session = RadioSession(client, _NotifyRecorder())
    await session.start(seed="12345")
    new = [json for url, json in client.calls if url.endswith("/rotor/session/new")]
    assert new[0]["seeds"] == ["track:12345"]


@pytest.mark.asyncio
async def test_finished_track_keeps_the_queue_when_enough_is_upcoming():
    # Played to the end with plenty still queued: nothing is fetched or
    # pushed, so the front's planned tracks don't change.
    client = _FakeClient()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)
    session._upcoming = ["a", "b", "c", "d"]

    await session.track_finished("x", played_ms=180000)
    assert not any(url.endswith("/tracks") for url, _ in client.calls)
    assert not [p for m, p in notifier.events if m == "radio/tracksAdded"]


@pytest.mark.asyncio
async def test_finished_track_appends_when_running_low():
    client = _FakeClient()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)  # serves "1", "2"
    await session.track_started("1")  # upcoming: ["2"]

    await session.track_finished("1", played_ms=180000)
    added = [p for m, p in notifier.events if m == "radio/tracksAdded"]
    assert len(added) == 1
    assert "replaceUpcoming" not in added[0]  # appended, not replacing
    assert [t["id"] for t in added[0]["tracks"]] == ["3"]
    assert session._upcoming == ["2", "3"]


@pytest.mark.asyncio
async def test_skip_replaces_the_queued_tracks():
    client = _FakeClient()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)
    await session.track_started("1")

    await session.skip("1", played_ms=5000)
    added = [p for m, p in notifier.events if m == "radio/tracksAdded"]
    assert added[-1]["replaceUpcoming"] is True
    assert session._upcoming == [t["id"] for t in added[-1]["tracks"]]


@pytest.mark.asyncio
async def test_track_started_drops_everything_up_to_it_from_upcoming():
    client = _FakeClient()
    session = RadioSession(client, _NotifyRecorder())
    await session.start(seed=None)
    session._upcoming = ["a", "b", "c", "d"]
    await session.track_started("c")  # jumped past a, b
    assert session._upcoming == ["d"]


@pytest.mark.asyncio
async def test_tracks_played_to_the_end_keep_topping_up_with_album_track_ids():
    # The front reports tracks by their protocol Track.id, "<id>:<albumId>"
    # for real Yandex tracks, while the session tracks bare ids — listening
    # straight through (no skips) must still drain the queue and top it up,
    # or the wave just stops at the end of the first batch.
    client = _FakeClient(with_albums=True, batch_size=5)
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    result = await session.start(seed=None)
    queue = [t["id"] for t in result["initialTracks"]]
    assert queue[0] == "1:101"

    for _ in range(12):
        track_id = queue.pop(0)
        await session.track_started(track_id)
        await session.track_finished(track_id, played_ms=180000)
        for m, p in notifier.events:
            if m == "radio/tracksAdded":
                queue += [t["id"] for t in p["tracks"] if t["id"] not in queue]
        notifier.events.clear()
        assert queue, "the wave ran out of tracks"
