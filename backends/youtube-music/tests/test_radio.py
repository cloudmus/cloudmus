import asyncio

import pytest

from cloudmus_backend_ytmusic.radio import RadioSession, _is_video_id, _parse_length_to_seconds


def _vid(name: str) -> str:
    """Pad/truncate to a realistic 11-character YouTube video id — real
    videoIds are always exactly 11 chars, which _is_video_id() relies on
    to tell a track seed apart from a playlist/mix seed (e.g. Supermix's
    "RDTM..."-prefixed id, always much longer)."""
    return (name + "00000000000")[:11]


def _watch_track(video_id, title="Song", length="3:07", artist_name="Artist"):
    return {
        "videoId": video_id,
        "title": title,
        "length": length,
        "thumbnail": [{"url": f"https://x/{video_id}.jpg", "width": 60, "height": 60}],
        "likeStatus": "INDIFFERENT",
        "artists": [{"name": artist_name, "id": "artist-id"}],
        "album": {"name": "Album", "id": "album-id"},
    }


class _FakeClient:
    def __init__(self, responses: dict):
        # keyed by (videoId, playlistId, radio) -> result; tuples may use
        # None for whichever id wasn't passed
        self._responses = responses
        self.calls: list[tuple] = []
        self.reported: list[str] = []

    def get_watch_playlist(self, videoId=None, playlistId=None, radio=False):
        key = (videoId, playlistId, radio)
        self.calls.append(key)
        return self._responses[key]

    def get_song(self, videoId):
        return {"videoId": videoId}

    def add_history_item(self, song):
        self.reported.append(song["videoId"])


def _batch(*ids):
    return {"tracks": [_watch_track(v) for v in ids]}


class _Capture:
    def __init__(self):
        self.emitted: list[dict] = []

    async def __call__(self, method, params):
        assert method == "radio/tracksAdded"
        self.emitted.append(params)


async def _settle():
    # Lets the background history reports finish.
    for _ in range(20):
        await asyncio.sleep(0.01)


async def _noop_notify(method: str, params: dict) -> None:
    return None


def test_parse_length_to_seconds():
    assert _parse_length_to_seconds("3:07") == 187
    assert _parse_length_to_seconds("1:02:15") == 3735
    assert _parse_length_to_seconds("garbage") == 0
    assert _parse_length_to_seconds(None) == 0


def test_is_video_id():
    assert _is_video_id(_vid("x")) is True
    assert _is_video_id("dQw4w9WgXcQ") is True  # real-shaped example, 11 chars
    assert _is_video_id("RDTMAK5uy_kset8DisdE7LSD4TNjEVvrKRTmG7a56sY") is False  # Supermix-style
    assert _is_video_id("PLz7-xrYmULdSLRZGk-6GKUtaBZcgQNwel") is False


@pytest.mark.asyncio
async def test_start_maps_watch_track_shape_and_returns_seed_as_station_id():
    seed = _vid("seed1")
    next1 = _vid("next1")
    client = _FakeClient({(seed, None, True): {"tracks": [_watch_track(seed), _watch_track(next1, length="4:00")]}})
    session = RadioSession(client, _noop_notify)

    result = await session.start(seed)

    assert result["stationId"] == seed
    tracks = result["initialTracks"]
    assert [t["id"] for t in tracks] == [seed, next1]
    assert tracks[0]["durationMs"] == 187000
    assert tracks[0]["album"]["title"] == "Album"
    assert tracks[0]["artists"][0]["name"] == "Artist"
    assert client.calls == [(seed, None, True)]


@pytest.mark.asyncio
async def test_start_requires_a_seed():
    client = _FakeClient({})
    session = RadioSession(client, _noop_notify)
    with pytest.raises(ValueError):
        await session.start(None)


MIX = "RDTMAK5uy_kset8DisdE7LSD4TNjEVvrKRTmG7a56sY"


@pytest.mark.asyncio
async def test_start_with_a_mix_plays_the_mix_itself():
    t1, t2 = _vid("t1"), _vid("t2")
    client = _FakeClient({(None, MIX, False): _batch(t1, t2)})
    session = RadioSession(client, _noop_notify)

    result = await session.start(MIX)

    assert result["stationId"] == MIX
    assert [t["id"] for t in result["initialTracks"]] == [t1, t2]
    assert client.calls == [(None, MIX, False)]


@pytest.mark.asyncio
async def test_finished_track_tops_up_from_the_mix_anchored_on_it_once_low():
    t1, t2, n1, n2 = _vid("t1"), _vid("t2"), _vid("n1"), _vid("n2")
    client = _FakeClient({
        (None, MIX, False): _batch(t1, t2),
        (t1, MIX, True): _batch(t1, t2, n1, n2),
    })
    capture = _Capture()
    session = RadioSession(client, capture)
    await session.start(MIX)

    await session.track_started(t1)
    await session.track_finished(t1, played_ms=180000)

    assert client.calls[-1] == (t1, MIX, True)
    # Appended, minus the played track and what's already queued.
    assert len(capture.emitted) == 1
    assert [t["id"] for t in capture.emitted[0]["tracks"]] == [n1, n2]
    assert "replaceUpcoming" not in capture.emitted[0]


@pytest.mark.asyncio
async def test_finished_track_leaves_a_long_queue_alone():
    ids = [_vid(f"t{i}") for i in range(6)]
    client = _FakeClient({(None, MIX, False): _batch(*ids)})
    capture = _Capture()
    session = RadioSession(client, capture)
    await session.start(MIX)

    await session.track_started(ids[0])
    await session.track_finished(ids[0], played_ms=180000)

    assert client.calls == [(None, MIX, False)]
    assert capture.emitted == []


@pytest.mark.asyncio
async def test_skip_replaces_the_upcoming_tail_without_played_or_skipped_tracks():
    ids = [_vid(f"t{i}") for i in range(6)]
    n1 = _vid("n1")
    client = _FakeClient({
        (None, MIX, False): _batch(*ids),
        (None, MIX, True): _batch(ids[0], n1, ids[1]),
    })
    capture = _Capture()
    session = RadioSession(client, capture)
    await session.start(MIX)

    await session.track_started(ids[0])
    await session.track_started(ids[1])
    await session.skip(ids[1], played_ms=5000)

    # Nothing played to the end yet — the fresh batch comes from the mix.
    assert client.calls[-1] == (None, MIX, True)
    assert [t["id"] for t in capture.emitted[0]["tracks"]] == [n1]
    assert capture.emitted[0]["replaceUpcoming"] is True


@pytest.mark.asyncio
async def test_skip_follows_the_last_track_played_to_the_end():
    ids = [_vid(f"t{i}") for i in range(6)]
    n1 = _vid("n1")
    client = _FakeClient({
        (None, MIX, False): _batch(*ids),
        (ids[0], MIX, True): _batch(n1),
    })
    capture = _Capture()
    session = RadioSession(client, capture)
    await session.start(MIX)

    await session.track_started(ids[0])
    await session.track_finished(ids[0], played_ms=180000)
    await session.track_started(ids[1])
    await session.skip(ids[1], played_ms=5000)

    assert client.calls[-1] == (ids[0], MIX, True)


@pytest.mark.asyncio
async def test_listens_are_reported_but_quick_skips_are_not():
    ids = [_vid(f"t{i}") for i in range(6)]
    client = _FakeClient({
        (None, MIX, False): _batch(*ids),
        (ids[0], MIX, True): _batch(),
    })
    session = RadioSession(client, _noop_notify)
    await session.start(MIX)

    await session.track_started(ids[0])
    await session.track_finished(ids[0], played_ms=180000)
    await session.track_started(ids[1])
    await session.skip(ids[1], played_ms=5000)
    await session.track_started(ids[2])
    await session.skip(ids[2], played_ms=45000)
    await _settle()

    assert client.reported == [ids[0], ids[2]]


@pytest.mark.asyncio
async def test_a_failed_history_report_is_harmless():
    class _FailingClient(_FakeClient):
        def add_history_item(self, song):
            raise RuntimeError("nope")

    ids = [_vid(f"t{i}") for i in range(6)]
    client = _FailingClient({(None, MIX, False): _batch(*ids)})
    session = RadioSession(client, _noop_notify)
    await session.start(MIX)
    await session.track_started(ids[0])
    await session.track_finished(ids[0], played_ms=180000)  # must not raise
    await _settle()


@pytest.mark.asyncio
async def test_track_radio_tops_up_from_the_finished_track_and_drops_it():
    seed, next1, next2 = _vid("seed1"), _vid("next1"), _vid("next2")
    client = _FakeClient({
        (seed, None, True): _batch(seed, next1),
        (next1, None, True): _batch(next1, next2),
    })
    capture = _Capture()
    session = RadioSession(client, capture)
    await session.start(seed)

    await session.track_started(seed)
    await session.track_started(next1)
    await session.track_finished(next1, played_ms=180000)

    assert client.calls == [(seed, None, True), (next1, None, True)]
    assert [t["id"] for t in capture.emitted[0]["tracks"]] == [next2]


@pytest.mark.asyncio
async def test_station_id_stays_fixed_across_top_ups():
    seed, a, b, c = _vid("seed1"), _vid("trackA"), _vid("trackB"), _vid("trackC")
    client = _FakeClient({
        (seed, None, True): _batch(seed, a),
        (a, None, True): _batch(a, b),
    })
    capture = _Capture()
    session = RadioSession(client, capture)
    await session.start(seed)

    await session.track_started(a)
    await session.track_finished(a, played_ms=180000)
    await session.track_started(b)
    client._responses[(a, None, True)] = _batch(c)
    await session.skip(b, played_ms=3000)

    assert len(capture.emitted) == 2
    assert all(p["stationId"] == seed for p in capture.emitted)


@pytest.mark.asyncio
async def test_a_jump_near_the_end_of_the_queue_appends_more_at_once():
    # The user picked a track further down the queue: not a skip, so nothing
    # is replaced — but with little left after it, more comes now, following
    # the track picked.
    ids = [_vid(f"t{i}") for i in range(4)]
    n1, n2 = _vid("n1"), _vid("n2")
    client = _FakeClient({
        (None, MIX, False): _batch(*ids),
        (ids[2], MIX, True): _batch(ids[2], ids[3], n1, n2),
    })
    capture = _Capture()
    session = RadioSession(client, capture)
    await session.start(MIX)

    await session.track_started(ids[2])  # past t0 and t1; only t3 is left

    assert client.calls[-1] == (ids[2], MIX, True)
    assert len(capture.emitted) == 1
    assert "replaceUpcoming" not in capture.emitted[0]
    assert [t["id"] for t in capture.emitted[0]["tracks"]] == [n1, n2]
    assert session._upcoming == [ids[3], n1, n2]


@pytest.mark.asyncio
async def test_a_jump_that_leaves_plenty_queued_fetches_nothing():
    ids = [_vid(f"t{i}") for i in range(8)]
    client = _FakeClient({(None, MIX, False): _batch(*ids)})
    capture = _Capture()
    session = RadioSession(client, capture)
    await session.start(MIX)

    await session.track_started(ids[2])
    await session.track_started(ids[0])  # back to one jumped past: no jump ahead

    assert client.calls == [(None, MIX, False)]
    assert capture.emitted == []


@pytest.mark.asyncio
async def test_a_track_reached_in_order_is_topped_up_only_once_played():
    t1, t2 = _vid("t1"), _vid("t2")
    client = _FakeClient({(None, MIX, False): _batch(t1, t2)})
    capture = _Capture()
    session = RadioSession(client, capture)
    await session.start(MIX)

    await session.track_started(t1)  # the head of the queue: no jump

    assert client.calls == [(None, MIX, False)]
    assert capture.emitted == []


@pytest.mark.asyncio
async def test_an_append_overtaken_by_a_skip_adds_nothing():
    # Started, then skipped before the append it set off came back: the skip
    # replaced the queue, and the late batch has nothing to follow.
    t1, t2, n1 = _vid("t1"), _vid("t2"), _vid("n1")
    client = _FakeClient({(None, MIX, False): _batch(t1, t2)})
    capture = _Capture()
    session = RadioSession(client, capture)
    await session.start(MIX)

    def fetch_after_a_skip(anchor=None):
        session._replacements += 1  # what a skip's replacement does meanwhile
        return _batch(n1)

    session._fetch_batch = fetch_after_a_skip
    await session._top_up(replace=False)

    assert session._upcoming == [t1, t2]
    assert capture.emitted == []


@pytest.mark.asyncio
async def test_a_track_the_session_has_not_queued_is_topped_up_by_the_fronts_count():
    # The front's queue came apart from what was served (downloaded tracks
    # kept through a replacement): the track picked near its end isn't one
    # of the served ones, and the ones jumped past are still counted as
    # waiting — the queue used to run out with no more sent.
    ids = [_vid(f"t{i}") for i in range(4)]
    kept, n1, n2 = _vid("kept"), _vid("n1"), _vid("n2")
    client = _FakeClient({
        (None, MIX, False): _batch(*ids),
        (kept, MIX, True): _batch(kept, ids[3], n1, n2),
    })
    capture = _Capture()
    session = RadioSession(client, capture)
    await session.start(MIX)

    await session.track_started(kept, queued_after=1)

    assert client.calls[-1] == (kept, MIX, True)
    assert [t["id"] for t in capture.emitted[0]["tracks"]] == [n1, n2]
    assert session._upcoming == [ids[3], n1, n2]


@pytest.mark.asyncio
async def test_a_finished_last_track_is_topped_up_whatever_was_served():
    ids = [_vid(f"t{i}") for i in range(4)]
    kept, n1 = _vid("kept"), _vid("n1")
    client = _FakeClient({
        (None, MIX, False): _batch(*ids),
        (kept, MIX, True): _batch(n1),
    })
    capture = _Capture()
    session = RadioSession(client, capture)
    await session.start(MIX)

    await session.track_finished(kept, 1000, queued_after=0)

    assert [t["id"] for t in capture.emitted[0]["tracks"]] == [n1]
    assert session._upcoming == [n1]


@pytest.mark.asyncio
async def test_a_front_with_plenty_queued_is_not_topped_up():
    ids = [_vid(f"t{i}") for i in range(3)]
    client = _FakeClient({(None, MIX, False): _batch(*ids)})
    capture = _Capture()
    session = RadioSession(client, capture)
    await session.start(MIX)

    await session.track_started(ids[2], queued_after=5)
    await session.track_finished(ids[2], 1000, queued_after=5)

    assert client.calls == [(None, MIX, False)]
    assert capture.emitted == []
