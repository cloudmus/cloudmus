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
    def __init__(self, responses: dict[str, dict]):
        # keyed by whichever id (videoId or playlistId) was passed -> result
        self._responses = responses
        self.calls: list[tuple[str, str]] = []  # (kwarg_name, id) per call

    def get_watch_playlist(self, videoId=None, playlistId=None, radio=False):
        if videoId is not None:
            self.calls.append(("videoId", videoId))
            return self._responses[videoId]
        self.calls.append(("playlistId", playlistId))
        return self._responses[playlistId]


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
    client = _FakeClient({seed: {"tracks": [_watch_track(seed), _watch_track(next1, length="4:00")]}})
    session = RadioSession(client, _noop_notify)

    result = await session.start(seed)

    assert result["stationId"] == seed
    tracks = result["initialTracks"]
    assert [t["id"] for t in tracks] == [seed, next1]
    assert tracks[0]["durationMs"] == 187000
    assert tracks[0]["album"]["title"] == "Album"
    assert tracks[0]["artists"][0]["name"] == "Artist"
    assert client.calls == [("videoId", seed)]


@pytest.mark.asyncio
async def test_start_requires_a_seed():
    client = _FakeClient({})
    session = RadioSession(client, _noop_notify)
    with pytest.raises(ValueError):
        await session.start(None)


@pytest.mark.asyncio
async def test_start_with_a_playlist_seed_uses_playlistId_and_does_not_prime_last_track_id():
    # A Supermix-style seed (catalog._find_supermix_id()'s result) — not a
    # track, so there's nothing valid to reseed _top_up() from yet.
    supermix_id = "RDTMAK5uy_kset8DisdE7LSD4TNjEVvrKRTmG7a56sY"
    track1 = _vid("track1")
    client = _FakeClient({supermix_id: {"tracks": [_watch_track(track1)]}})
    session = RadioSession(client, _noop_notify)

    result = await session.start(supermix_id)

    assert result["stationId"] == supermix_id
    assert client.calls == [("playlistId", supermix_id)]
    assert session.last_track_id is None


@pytest.mark.asyncio
async def test_top_up_is_a_noop_until_a_track_actually_starts():
    supermix_id = "RDTMAK5uy_kset8DisdE7LSD4TNjEVvrKRTmG7a56sY"
    client = _FakeClient({supermix_id: {"tracks": [_watch_track(_vid("track1"))]}})
    session = RadioSession(client, _noop_notify)
    await session.start(supermix_id)

    # feedback.trackFinished/.skip could in principle arrive before
    # track_started() if the front's bookkeeping raced — must not crash or
    # call get_watch_playlist with a missing/None id.
    await session.track_finished(_vid("track1"), played_ms=1000)
    await session.skip(_vid("track1"), played_ms=1000)

    assert client.calls == [("playlistId", supermix_id)]  # only the initial start() call


@pytest.mark.asyncio
async def test_top_up_reseeds_from_last_started_track_not_original_seed():
    seed, next1, next2 = _vid("seed1"), _vid("next1"), _vid("next2")
    client = _FakeClient({
        seed: {"tracks": [_watch_track(seed), _watch_track(next1)]},
        next1: {"tracks": [_watch_track(next1), _watch_track(next2)]},
    })
    session = RadioSession(client, _noop_notify)
    await session.start(seed)

    # The front started playing "next1" (the second track of the initial
    # batch) — track_started() records that as the new reseed point.
    await session.track_started(next1)
    await session.track_finished(next1, played_ms=180000)

    assert client.calls == [("videoId", seed), ("videoId", next1)]


@pytest.mark.asyncio
async def test_top_up_drops_the_re_included_seed_track():
    emitted = []

    async def capture_notify(method, params):
        emitted.append((method, params))

    seed, seed_next, brand_new = _vid("seed1"), _vid("seednext"), _vid("brandnew")
    client = _FakeClient({
        seed: {"tracks": [_watch_track(seed)]},
        seed_next: {"tracks": [_watch_track(seed_next), _watch_track(brand_new)]},
    })
    session = RadioSession(client, capture_notify)
    await session.start(seed)
    await session.track_started(seed_next)
    await session.skip(seed_next, played_ms=5000)

    assert len(emitted) == 1
    method, params = emitted[0]
    assert method == "radio/tracksAdded"
    assert [t["id"] for t in params["tracks"]] == [brand_new]


@pytest.mark.asyncio
async def test_station_id_stays_fixed_across_top_ups():
    emitted = []

    async def capture_notify(method, params):
        emitted.append(params)

    seed, track_a, track_b, track_c = _vid("seed1"), _vid("trackA"), _vid("trackB"), _vid("trackC")
    client = _FakeClient({
        seed: {"tracks": [_watch_track(seed)]},
        track_a: {"tracks": [_watch_track(track_a), _watch_track(track_b)]},
        track_b: {"tracks": [_watch_track(track_b), _watch_track(track_c)]},
    })
    session = RadioSession(client, capture_notify)
    await session.start(seed)

    await session.track_started(track_a)
    await session.track_finished(track_a, played_ms=180000)
    await session.track_started(track_b)
    await session.skip(track_b, played_ms=3000)

    assert len(emitted) == 2
    assert all(p["stationId"] == seed for p in emitted)
