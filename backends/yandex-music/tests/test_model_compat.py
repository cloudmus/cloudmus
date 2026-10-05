import pytest
from yandex_music import Playlist, User

from cloudmus_backend_yandex import model_compat


@pytest.fixture
def installed(monkeypatch):
    """install() is a one-time global patch — restore the library's own
    de_json afterwards and re-run install() fresh for each test."""
    monkeypatch.setattr(User, "de_json", User.de_json, raising=True)
    monkeypatch.setattr(model_compat, "_installed", False)
    model_compat.install()


def test_playlist_of_the_day_without_made_for_login_parses(installed):
    # Trimmed from a real /users/{uid}/playlists/{kind} response.
    data = {
        "uid": 503646255,
        "kind": 26292563,
        "title": "Плейлист дня",
        "owner": {"uid": 503646255, "login": "yamusic-daily", "name": "yamusic-daily"},
        "madeFor": {"userInfo": {"uid": 42, "name": "Vlad", "sex": "male", "verified": False}},
    }
    playlist = Playlist.de_json(data, None)
    assert playlist.made_for.user_info.uid == 42
    assert playlist.made_for.user_info.login == ""
    assert playlist.owner.login == "yamusic-daily"


def test_install_is_idempotent(installed):
    patched = User.de_json
    model_compat.install()
    assert User.de_json == patched
