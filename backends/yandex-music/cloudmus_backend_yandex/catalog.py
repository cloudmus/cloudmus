"""catalog.listPlaylists / listTracks / listLiked, built on the yandex_music
client. Ported from the fetching logic in ym_player/resolver.py and
ym_player/downloader.py's playlist_tracks()."""
from __future__ import annotations

import asyncio

from yandex_music import Client, Playlist as YPlaylist, Track as YTrack

from rpc_common.models import Album, Artist, Playlist, Track

COVER_SIZE = "400x400"


def _cover_url(cover_uri: str | None) -> str | None:
    if not cover_uri:
        return None
    return f"https://{cover_uri.replace('%%', COVER_SIZE)}"


def to_track(t: YTrack) -> Track:
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
    )


def to_playlist(p: YPlaylist) -> Playlist:
    return Playlist(id=p.playlist_id, title=p.title or "(untitled)", trackCount=p.track_count or 0, kind="playlist")


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
    playlists = await asyncio.to_thread(client.users_playlists_list) or []
    return {"playlists": [to_playlist(p).to_dict() for p in playlists]}


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
    return {"tracks": [to_track(t).to_dict() for t in tracks]}
