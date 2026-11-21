from cloudmus_backend_yandex.server import _iso_millis


def test_the_time_is_sent_the_way_play_audio_wants_it():
    # Without the milliseconds the service answers invalid-timestamp.
    assert _iso_millis("2026-11-14T22:10:31Z") == "2026-11-14T22:10:31.000Z"
    assert _iso_millis("2026-11-14T22:10:31.250Z") == "2026-11-14T22:10:31.250Z"
    assert _iso_millis("2026-11-15T01:10:31+03:00") == "2026-11-14T22:10:31.000Z"


def test_a_time_that_cant_be_read_becomes_now():
    assert _iso_millis("garbage").endswith("Z")
