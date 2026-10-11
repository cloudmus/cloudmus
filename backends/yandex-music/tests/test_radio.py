from types import SimpleNamespace

import pytest

from cloudmus_backend_yandex import radio
from cloudmus_backend_yandex.radio import RadioSession, _resolve_station


@pytest.fixture
def one_batch_per_top_up(monkeypatch):
    # A top-up asks on until it has a few new tracks; one is enough for the
    # tests that look at what a single batch does.
    monkeypatch.setattr(radio, "TOP_UP_TRACKS", 1)


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
async def test_top_up_passes_played_track_as_queue_on_session_endpoint(one_batch_per_top_up):
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
async def test_feedback_events_go_through_session_endpoint_with_batch_id(one_batch_per_top_up):
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
async def test_top_up_replaces_upcoming_with_unplayed_tracks(one_batch_per_top_up):
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
async def test_top_up_never_serves_an_already_played_track_as_upcoming(one_batch_per_top_up):
    client = _FakeClient()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)
    await session.track_started("1")

    await session._top_up("1", replace=True)
    # Alternating call: the server re-serves the queued id, which already
    # played. That is not served again — the session is asked once more,
    # from the end of what it sent, and what it has then is.
    await session._top_up("1", replace=True)
    added = [p for m, p in notifier.events if m == "radio/tracksAdded"]
    assert len(added) == 2
    assert all(t["id"] != "1" for batch in added for t in batch["tracks"])
    assert client.calls[-1][1] == {"queue": ["1"]}


@pytest.mark.asyncio
async def test_top_up_gives_up_on_a_session_that_only_repeats_itself():
    class _Repeating(_FakeClient):
        def _respond(self, url, json):
            if url.endswith("/tracks"):
                self.calls.append((url, json))
                return {"batchId": "b", "sequence": [{"type": "track", "track": _TrackDict.make(1)}]}
            return super()._respond(url, json)

    client = _Repeating()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)
    await session.track_started("1")

    await session._top_up("1", replace=False)
    assert len([url for url, _ in client.calls if url.endswith("/tracks")]) == 5
    assert not [p for m, p in notifier.events if m == "radio/tracksAdded"]


@pytest.mark.asyncio
async def test_top_up_may_reserve_a_served_but_unplayed_track(one_batch_per_top_up):
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
async def test_finished_track_appends_when_running_low(one_batch_per_top_up):
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
    session._upcoming = ["a", "b", "c", "d", "e", "f", "g"]
    await session.track_started("c")  # jumped past a, b
    assert session._upcoming == ["d", "e", "f", "g"]


@pytest.mark.asyncio
async def test_a_jump_near_the_end_of_the_queue_appends_more_at_once():
    # The user picked a track further down the queue: not a skip, so nothing
    # is replaced — but with little left after it, more comes now rather
    # than when it ends.
    client = _FakeClient()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)
    session._upcoming = ["a", "b", "c", "d"]

    await session.track_started("c")  # past a and b; only d is left
    added = [p for m, p in notifier.events if m == "radio/tracksAdded"]
    assert len(added) == 1
    assert "replaceUpcoming" not in added[0]
    assert session._upcoming == ["d"] + [t["id"] for t in added[0]["tracks"]]


@pytest.mark.asyncio
async def test_a_jump_that_leaves_plenty_queued_fetches_nothing():
    client = _FakeClient()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)
    session._upcoming = ["a", "b", "c", "d", "e", "f"]

    await session.track_started("b")
    await session.track_started("x")  # back to one played before: no jump ahead
    assert not any(url.endswith("/tracks") for url, _ in client.calls)
    assert not [p for m, p in notifier.events if m == "radio/tracksAdded"]


@pytest.mark.asyncio
async def test_a_track_reached_in_order_is_topped_up_only_once_played():
    client = _FakeClient()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)  # serves "1", "2"

    await session.track_started("1")  # the head of the queue: no jump
    assert not [p for m, p in notifier.events if m == "radio/tracksAdded"]


@pytest.mark.asyncio
async def test_an_append_overtaken_by_a_skip_adds_nothing():
    # Started, then skipped before the append it set off came back: the skip
    # replaced the queue, and the late batch has nothing to follow.
    client = _FakeClient()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)
    session._upcoming = ["a", "b"]
    real_post = session._post

    def post_after_a_skip(path, payload):
        result = real_post(path, payload)
        session._replacements += 1  # what a skip's replacement does meanwhile
        return result

    session._post = post_after_a_skip
    await session._top_up("a", replace=False)
    assert session._upcoming == ["a", "b"]
    assert not [p for m, p in notifier.events if m == "radio/tracksAdded"]


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


@pytest.mark.asyncio
async def test_a_track_the_session_has_not_queued_is_topped_up_by_the_fronts_count():
    # The front's queue came apart from what was served (downloaded tracks
    # kept through a replacement, a resumed radio): the track picked near
    # its end isn't one of the served ones, and the ones jumped past are
    # still counted as waiting — the queue used to run out with no more sent.
    client = _FakeClient()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)
    session._upcoming = ["a", "b", "c", "d"]

    await session.track_started("x", queued_after=1)
    added = [p for m, p in notifier.events if m == "radio/tracksAdded"]
    assert len(added) == 1
    assert "replaceUpcoming" not in added[0]
    assert session._upcoming == ["d"] + [t["id"] for t in added[0]["tracks"]]


@pytest.mark.asyncio
async def test_a_finished_last_track_is_topped_up_whatever_was_served():
    client = _FakeClient()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)
    session._upcoming = ["a", "b", "c", "d"]

    await session.track_finished("x", 1000, queued_after=0)
    added = [p for m, p in notifier.events if m == "radio/tracksAdded"]
    assert len(added) == 1
    assert session._upcoming == [t["id"] for t in added[0]["tracks"]]


@pytest.mark.asyncio
async def test_a_front_with_plenty_queued_is_not_topped_up():
    # The other way round: little is left of what was served, but the front
    # has more than that (tracks it kept).
    client = _FakeClient()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)
    session._upcoming = ["a", "b", "c"]

    await session.track_started("c", queued_after=5)
    await session.track_finished("c", 1000, queued_after=5)
    assert not any(url.endswith("/tracks") for url, _ in client.calls)
    assert not [p for m, p in notifier.events if m == "radio/tracksAdded"]


@pytest.mark.asyncio
async def test_tracks_picked_one_after_another_are_topped_up_as_they_start():
    # Each is the head of what's queued — no jump — and none plays to its
    # end: the front's count alone says the queue is running out.
    client = _FakeClient()
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)  # serves "1", "2"

    await session.track_started("1", queued_after=1)
    assert len([p for m, p in notifier.events if m == "radio/tracksAdded"]) == 1


@pytest.mark.asyncio
async def test_a_start_reported_along_with_an_end_asks_for_nothing_more():
    # The front reports a track's end and the next one's start together:
    # the start's count is from before the end's top-up came.
    client = _FakeClient(batch_size=3)
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None)  # serves "1", "2", "3"
    await session.track_started("1", queued_after=2)
    session._topped_up_at = float("-inf")  # that was a while ago

    await session.track_finished("1", 1000, queued_after=2)
    await session.track_started("2", queued_after=1)
    assert len([url for url, _ in client.calls if url.endswith("/tracks")]) == 2


@pytest.mark.asyncio
async def test_a_track_played_lately_is_not_served_while_there_are_others():
    # What the front played before this session — the station starts every
    # session with much the same tracks.
    client = _FakeClient(with_albums=True, batch_size=3)
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)

    result = await session.start(seed=None, recent_track_ids=["2:102", "5:105"])
    assert [t["id"] for t in result["initialTracks"]] == ["1:101", "3:103"]

    await session._top_up("1:101", replace=False)  # serves 4, 5, 6, then 7, 8, 9
    added = [p for m, p in notifier.events if m == "radio/tracksAdded"]
    assert [t["id"] for t in added[0]["tracks"]] == ["4:104", "6:106", "7:107", "8:108", "9:109"]


@pytest.mark.asyncio
async def test_with_nothing_else_the_tracks_played_longest_ago_are_served():
    client = _FakeClient(batch_size=4)
    session = RadioSession(client, _NotifyRecorder())

    # The latest first: 4 was played longest ago.
    result = await session.start(seed=None, recent_track_ids=["2", "1", "3", "4"])
    assert [t["id"] for t in result["initialTracks"]] == ["4", "3", "1"]


@pytest.mark.asyncio
async def test_a_top_up_of_lately_played_tracks_only_asks_on_then_serves_the_oldest():
    client = _FakeClient(batch_size=1)
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    recent = [str(i) for i in range(2, 30)]
    await session.start(seed=None, recent_track_ids=recent)  # serves "1"
    await session.track_started("1")

    await session._top_up("1", replace=False)  # 2, 3, 4, 5, 6: all played lately
    assert len([url for url, _ in client.calls if url.endswith("/tracks")]) == 5
    added = [p for m, p in notifier.events if m == "radio/tracksAdded"]
    assert [t["id"] for t in added[0]["tracks"]] == ["6", "5", "4"]
    assert session._upcoming == ["6", "5", "4"]


@pytest.mark.asyncio
async def test_a_track_played_in_this_session_is_never_served_again():
    client = _FakeClient(batch_size=2)
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None, recent_track_ids=["3", "4"])  # serves 1, 2
    await session.track_started("1")
    session._played.add("3")

    await session._top_up("1", replace=True)  # 3 and 4, both held back
    added = [p for m, p in notifier.events if m == "radio/tracksAdded"]
    assert [t["id"] for t in added[0]["tracks"]] != []
    assert all(t["id"] != "3" for batch in added for t in batch["tracks"])


@pytest.mark.asyncio
async def test_the_track_a_station_is_started_from_is_served_however_lately_it_played():
    client = _FakeClient(batch_size=2)
    client.tracks = lambda ids: []
    session = RadioSession(client, _NotifyRecorder())

    result = await session.start(seed="1", recent_track_ids=["1", "2"])
    assert [t["id"] for t in result["initialTracks"]] == ["1"]


@pytest.mark.asyncio
async def test_a_top_up_asks_on_until_it_has_a_few_new_tracks():
    # A batch is a few tracks, most of them played lately: one new track
    # at a time would leave the queue one track long.
    client = _FakeClient(batch_size=2)
    notifier = _NotifyRecorder()
    session = RadioSession(client, notifier)
    await session.start(seed=None, recent_track_ids=["4", "6", "8"])  # serves 1, 2
    await session.track_started("1")

    await session._top_up("1", replace=False)  # 3 4, 5 6, 7 8
    assert len([url for url, _ in client.calls if url.endswith("/tracks")]) == 3
    added = [p for m, p in notifier.events if m == "radio/tracksAdded"]
    assert len(added) == 1
    assert [t["id"] for t in added[0]["tracks"]] == ["3", "5", "7"]
    assert session._upcoming == ["2", "3", "5", "7"]
