import pytest

from cloudmus_backend_yandex.radio import RadioSession


class _FakeSequenceEntry:
    def __init__(self, track):
        self.track = track


class _FakeStationTracksResult:
    def __init__(self, batch_id, tracks):
        self.batch_id = batch_id
        self.sequence = [_FakeSequenceEntry(t) for t in tracks]


class _FakeTrack:
    def __init__(self, track_id):
        self.track_id = track_id
        self.title = f"Track {track_id}"
        self.artists = []
        self.albums = []
        self.duration_ms = 1000
        self.cover_uri = None
        self.explicit = False


class _FakeClient:
    def __init__(self):
        self.tracks_calls: list[tuple[str, object]] = []  # (station, queue) per rotor_station_tracks call

    def rotor_station_feedback_radio_started(self, station, from_=None):
        return True

    def rotor_station_feedback_track_started(self, station, track_id, batch_id=None):
        return True

    def rotor_station_feedback_track_finished(self, station, track_id, total_played_seconds, batch_id=None):
        return True

    def rotor_station_feedback_skip(self, station, track_id, total_played_seconds, batch_id=None):
        return True

    def rotor_station_tracks(self, station, queue=None):
        self.tracks_calls.append((station, queue))
        # A real station always advances past `queue` — simulate that here
        # so the test would fail if _top_up() ever stopped passing it.
        next_id = str(len(self.tracks_calls))
        return _FakeStationTracksResult(batch_id=f"batch-{next_id}", tracks=[_FakeTrack(next_id)])


async def _noop_notify(method: str, params: dict) -> None:
    return None


@pytest.mark.asyncio
async def test_top_up_passes_played_track_as_queue_to_advance_the_chain():
    # Regression test: rotor_station_tracks() needs queue=<just-played
    # track id> to advance the station's chain (see radio.py's _top_up()
    # doc comment) — omitting it made the wave re-serve the same batch
    # forever instead of progressing.
    client = _FakeClient()
    session = RadioSession(client, _noop_notify)
    await session.start(seed=None)

    await session.track_finished("1", played_ms=30000)
    assert client.tracks_calls[-1] == (session.station, "1")

    await session.skip("2", played_ms=5000)
    assert client.tracks_calls[-1] == (session.station, "2")
