"""catalog.listPlaylists / listTracks / listLiked, built on the yandex_music
client. Ported from the fetching logic in ym_player/resolver.py and
ym_player/downloader.py's playlist_tracks()."""
from __future__ import annotations

import asyncio

from yandex_music import Client, Playlist as YPlaylist, Track as YTrack

from rpc_common.generated.models import Album, Artist, Playlist, Track

COVER_SIZE = "400x400"

# The station id yandex_music's rotor API uses for the personal "wave"
# station (see radio.py, which imports this rather than keeping its own
# copy). Also doubles as this synthesized playlist's id (§ list_playlists).
WAVE_STATION_ID = "user:onyourwave"

# yandex_music's rotor_stations_list() enumerates genre/mood stations but
# never includes the personal wave station, so there is no per-account
# icon/description available for it via the API — use a static description
# and leave coverUrl unset, so the front generates its own cover from the
# title instead (see fronts/qt/src/Ui/GeneratedCoverArt.h).
WAVE_DESCRIPTION = "Персональная станция на основе ваших вкусов и истории прослушиваний"
LIKED_PLAYLIST_ID = "__liked__"


def _cover_url(cover_uri: str | None) -> str | None:
    if not cover_uri:
        return None
    return f"https://{cover_uri.replace('%%', COVER_SIZE)}"


def _web_url(t: YTrack) -> str | None:
    # Yandex only has a browsable track page in the context of an album.
    # t.track_id is "trackNum:albumNum" when an album is present (or just
    # the bare track number otherwise) — split it rather than relying on a
    # separate t.id attribute, since track_id is what's already guaranteed
    # to carry the numeric track id in both shapes.
    if not t.albums:
        return None
    track_num = t.track_id.split(":", 1)[0]
    return f"https://music.yandex.ru/album/{t.albums[0].id}/track/{track_num}"


def to_track(t: YTrack, *, liked: bool | None = None) -> Track:
    # yandex_music's Track object has no liked/is_liked attribute — likes
    # are tracked upstream only as a separate id list (users_likes_tracks()),
    # so there's nothing to read off `t` itself. Callers that already know a
    # track is liked (list_liked() — every track it returns came from that
    # id list) pass it in explicitly; anywhere else it stays unset (the
    # front just treats it as "unknown", not "not liked").
    artists = [Artist(id=str(a.id), name=a.name or "") for a in (t.artists or [])]
    album = None
    if t.albums:
        a = t.albums[0]
        album = Album(id=str(a.id), title=a.title or "", coverUrl=_cover_url(a.cover_uri))
    return Track(
        id=t.track_id,
        title=t.title or "",
        artists=artists,
        durationMs=t.duration_ms or 0,
        album=album,
        coverUrl=_cover_url(t.cover_uri),
        explicit=t.explicit,
        webUrl=_web_url(t),
        liked=liked,
    )


def to_playlist(p: YPlaylist) -> Playlist:
    return Playlist(id=p.playlist_id, title=p.title or "(untitled)", trackCount=p.track_count or 0, kind="playlist")


def _wave_playlist() -> Playlist:
    return Playlist(
        id=WAVE_STATION_ID,
        title="Моя волна",
        description=WAVE_DESCRIPTION,
        # Continuous, not a fixed-length list — see docs/protocol.md's
        # kind: radioStation note; 0 signals "not applicable" here.
        trackCount=0,
        kind="radioStation",
    )


def _liked_playlist(track_count: int) -> Playlist:
    return Playlist(id=LIKED_PLAYLIST_ID, title="Мне нравится", trackCount=track_count, kind="liked")


def playlist_tracks(playlist: YPlaylist) -> list[YTrack]:
    shorts = playlist.tracks or playlist.fetch_tracks() or []
    tracks = []
    for short in shorts:
        track = short.track or short.fetch_track()
        if track is not None:
            tracks.append(track)
    return tracks


def _find_playlist(client: Client, playlist_id: str) -> YPlaylist | None:
    uid_str, sep, kind_str = playlist_id.partition(":")
    if sep:
        return client.users_playlists(int(kind_str), user_id=uid_str)
    return client.users_playlists(int(uid_str))


async def list_playlists(client: Client) -> dict:
    # My Wave and Liked Tracks are surfaced here as regular Playlist entries
    # (kind: radioStation / liked) rather than the front synthesizing them
    # from capability flags — see docs/protocol.md's Playlist.kind note.
    # This backend's capabilities always have browse.radio/likedTracks true,
    # so both are unconditional.
    def fetch() -> tuple[list[YPlaylist], int]:
        real_playlists = client.users_playlists_list() or []
        liked = client.users_likes_tracks()
        liked_count = len(liked.tracks_ids) if liked else 0
        return real_playlists, liked_count

    real_playlists, liked_count = await asyncio.to_thread(fetch)
    playlists = [_wave_playlist(), _liked_playlist(liked_count)]
    playlists += [to_playlist(p) for p in real_playlists]
    return {"playlists": [p.to_dict() for p in playlists]}


async def list_tracks(client: Client, playlist_id: str) -> dict:
    playlist = await asyncio.to_thread(_find_playlist, client, playlist_id)
    if playlist is None:
        raise LookupError(playlist_id)
    tracks = await asyncio.to_thread(playlist_tracks, playlist)
    return {"tracks": [to_track(t).to_dict() for t in tracks]}


async def list_liked(client: Client) -> dict:
    def fetch() -> list[YTrack]:
        liked = client.users_likes_tracks()
        ids = liked.tracks_ids if liked else []
        return client.tracks(ids) if ids else []

    tracks = await asyncio.to_thread(fetch)
    # Every track here came from users_likes_tracks() itself — liked=True
    # unconditionally, rather than leaving it unset like to_track()'s
    # default (see its docstring).
    return {"tracks": [to_track(t, liked=True).to_dict() for t in tracks]}
