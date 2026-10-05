"""Tolerates API responses yandex_music's models don't accept yet.

The API dropped `login` from a generated playlist's `madeFor.userInfo`
(Playlist of the Day and the like), but yandex_music's User still requires
it: parsing the playlist fails with "User.__init__() missing 1 required
positional argument: 'login'", and the playlist can't be opened at all.
Nothing here reads the login, so a missing one parses as empty.
"""
from __future__ import annotations

from yandex_music import User

_installed = False


def install() -> None:
    """Idempotent. Must run before the first API call is parsed."""
    global _installed
    if _installed:
        return
    _installed = True

    original = User.de_json.__func__

    def de_json(cls, data, client):
        if isinstance(data, dict) and "login" not in data:
            data = {**data, "login": ""}
        return original(cls, data, client)

    User.de_json = classmethod(de_json)
