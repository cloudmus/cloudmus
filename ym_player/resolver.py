import re

from yandex_music import Client, Playlist, Track

WAVE_STATION = "user:onyourwave"

_TRACK_URL_RE = re.compile(r"music\.yandex\.\w+/album/\d+/track/(?P<track_id>\d+)")
_PLAYLIST_UUID_URL_RE = re.compile(
    r"music\.yandex\.\w+/(?:users/[^/]+/)?playlists/(?P<uuid>[0-9a-fA-F-]{8,})"
)
_PLAYLIST_KIND_URL_RE = re.compile(
    r"music\.yandex\.\w+/users/(?P<login>[^/]+)/playlists/(?P<kind>\d+)"
)


def is_wave(s: str) -> bool:
    return s.strip().lower() in {"wave", "волна", "моя волна", "onyourwave"}


def is_likes(s: str) -> bool:
    return s.strip().lower() in {"likes", "лайки", "мне нравится"}


def resolve_track(client: Client, s: str) -> Track:
    s = s.strip()
    m = _TRACK_URL_RE.search(s)
    track_id = m.group("track_id") if m else s
    tracks = client.tracks([track_id])
    if not tracks:
        raise ValueError(f"Track not found: {s}")
    return tracks[0]


def resolve_playlist(client: Client, s: str) -> Playlist:
    s = s.strip()

    m = _PLAYLIST_UUID_URL_RE.search(s)
    if m:
        playlist = client.playlist(m.group("uuid"))
        if playlist is None:
            raise ValueError(f"Playlist not found: {s}")
        return playlist

    m = _PLAYLIST_KIND_URL_RE.search(s)
    if m:
        playlist = client.users_playlists(int(m.group("kind")), user_id=m.group("login"))
        if playlist is None:
            raise ValueError(f"Playlist not found: {s}")
        return playlist

    if re.fullmatch(r"[0-9a-fA-F-]{8,}", s) and "-" in s:
        playlist = client.playlist(s)
        if playlist is None:
            raise ValueError(f"Playlist not found: {s}")
        return playlist

    if s.isdigit():
        playlist = client.users_playlists(int(s))
        if playlist is None:
            raise ValueError(f"Playlist not found: {s}")
        return playlist

    raise ValueError(f"Could not recognize playlist: {s}")
