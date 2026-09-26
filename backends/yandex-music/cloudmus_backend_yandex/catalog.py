"""catalog.listPlaylists / listTracks / listLiked, built on the yandex_music
client. Ported from the fetching logic in ym_player/resolver.py and
ym_player/downloader.py's playlist_tracks()."""
from __future__ import annotations

import asyncio
import logging
from typing import Callable, TypeVar

from yandex_music import Client, Playlist as YPlaylist, Track as YTrack

from rpc_common.generated.models import Album, Artist, Playlist, Track

COVER_SIZE = "400x400"

T = TypeVar("T")

logger = logging.getLogger(__name__)


def _bare_id(track_id: object) -> str:
    # Likes/dislikes lists carry "trackId:albumId" while a Track's own
    # track_id may or may not include the album part — compare bare ids.
    return str(track_id).split(":", 1)[0]


class LikeCache:
    """The account's liked/disliked track ids.

    yandex_music's Track has no liked/disliked attribute — both are only
    available upstream as separate id lists — so every Track this backend
    returns is annotated from this cache (see to_track()), not just the
    ones listLiked returns. Reloaded with every catalog.listPlaylists (the
    front's refresh point) and patched in place by the feedback.* handlers,
    so a like made from this app shows up immediately everywhere.
    """

    def __init__(self) -> None:
        self.liked: set[str] = set()
        self.disliked: set[str] = set()
        self.loaded = False

    def load(self, client: Client) -> None:
        liked = client.users_likes_tracks()
        self.liked = {_bare_id(i) for i in (liked.tracks_ids if liked else [])}
        try:
            disliked = client.users_dislikes_tracks()
            self.disliked = {_bare_id(i) for i in (disliked.tracks_ids if disliked else [])}
        except Exception as e:  # dislikes are a nice-to-have; likes still load
            logger.debug("loading dislikes failed: %s", e)
            self.disliked = set()
        self.loaded = True

    def ensure_loaded(self, client: Client) -> None:
        if not self.loaded:
            try:
                self.load(client)
            except Exception as e:
                logger.debug("loading likes failed: %s", e)

    def set_liked(self, track_id: str, liked: bool) -> None:
        tid = _bare_id(track_id)
        if liked:
            self.liked.add(tid)
            self.disliked.discard(tid)  # the service cross-clears — docs/protocol.md §7.4
        else:
            self.liked.discard(tid)

    def set_disliked(self, track_id: str, disliked: bool) -> None:
        tid = _bare_id(track_id)
        if disliked:
            self.disliked.add(tid)
            self.liked.discard(tid)
        else:
            self.disliked.discard(tid)


likes = LikeCache()

# The station id yandex_music's rotor API uses for the personal "wave"
# station (see radio.py, which imports this rather than keeping its own
# copy). Also doubles as this synthesized playlist's id (§ list_playlists).
WAVE_STATION_ID = "user:onyourwave"

# rotor_stations_list() never includes the personal wave station, so there
# is no description for it via the API — a static one. Its picture comes
# from rotor_stations_dashboard(), which does list it (see _stations()).
WAVE_DESCRIPTION = "Персональная станция на основе ваших вкусов и истории прослушиваний"
LIKED_PLAYLIST_ID = "__liked__"
# The account's "Мне нравится" as a regular playlist — only its cover is
# used; its tracks come from users_likes_tracks() (see list_liked()).
LIKES_PLAYLIST_KIND = 3


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
    # liked/disliked come from the account-wide LikeCache (see its doc);
    # `liked` overrides it for callers that know better (list_liked()).
    # Left unset (unknown to the front, not "not liked") until the cache
    # has loaded at least once.
    disliked = None
    if likes.loaded:
        tid = _bare_id(t.track_id)
        if liked is None:
            liked = tid in likes.liked
        disliked = tid in likes.disliked
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
        disliked=disliked,
    )


def _playlist_cover(p: YPlaylist) -> str | None:
    # A playlist's own picture (a personal playlist's generated art, an
    # uploaded image) when it has one, else the first album of its mosaic
    # — og_image is that same first album.
    cover = getattr(p, "cover", None)
    uri = getattr(cover, "uri", None) or getattr(p, "og_image", None)
    if not uri:
        items = getattr(cover, "items_uri", None) or []
        uri = items[0] if items else None
    return _cover_url(uri)


def to_playlist(p: YPlaylist, *, editable: bool = True, featured: bool | None = None) -> Playlist:
    # users_playlists_list() only lists the account's own playlists, so
    # every one of them can be edited (docs/protocol.md §7.6) — unlike a
    # personal playlist, which belongs to a Yandex system user.
    return Playlist(
        id=p.playlist_id,
        title=p.title or "(untitled)",
        description=getattr(p, "description", None) or None,
        coverUrl=_playlist_cover(p),
        trackCount=p.track_count or 0,
        kind="playlist",
        editable=editable,
        featured=featured,
    )


# The personal playlist shown at the top level by default (Playlist.featured);
# the others ("Премьера", "Дежавю", "Тайник", ...) wait to be picked as
# favorites. A GeneratedPlaylist.type, stable across UI languages.
FEATURED_PERSONAL_PLAYLIST = "playlistOfTheDay"


def _personal_playlists(feed: object) -> list[Playlist]:
    # Their ids are the usual "<owner uid>:<kind>", so listTracks finds
    # them through _find_playlist() like any other playlist.
    playlists = []
    for generated in getattr(feed, "generated_playlists", None) or []:
        data = generated.data
        if data is None or not generated.ready:
            continue
        featured = True if generated.type == FEATURED_PERSONAL_PLAYLIST else None
        playlists.append(to_playlist(data, editable=False, featured=featured))
    return playlists


def _stations(dashboard: object) -> list[Playlist]:
    # The account's recommended stations ("Электроника", "Рок", ...),
    # addressed "<type>:<tag>" like My Wave — a seed radio.py's
    # _resolve_station() already passes through as is. My Wave is among
    # them — list_playlists() takes just its picture for _wave_playlist().
    stations = []
    for result in getattr(dashboard, "stations", None) or []:
        station = result.station
        if station is None or station.id is None:
            continue
        station_id = f"{station.id.type}:{station.id.tag}"
        image = station.full_image_url or (station.icon.image_url if station.icon else None)
        stations.append(
            Playlist(
                id=station_id,
                title=station.name or station_id,
                coverUrl=_cover_url(image),
                trackCount=0,
                kind="radioStation",
            )
        )
    return stations


def _wave_playlist(cover_url: str | None = None) -> Playlist:
    return Playlist(
        id=WAVE_STATION_ID,
        title="Моя волна",
        description=WAVE_DESCRIPTION,
        coverUrl=cover_url,
        # Continuous, not a fixed-length list — see docs/protocol.md's
        # kind: radioStation note; 0 signals "not applicable" here.
        trackCount=0,
        kind="radioStation",
        featured=True,
    )


def _liked_playlist(track_count: int, cover_url: str | None = None) -> Playlist:
    return Playlist(
        id=LIKED_PLAYLIST_ID,
        title="Мне нравится",
        coverUrl=cover_url,
        trackCount=track_count,
        kind="liked",
        featured=True,
    )


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


def _best_effort(what: str, fetch: Callable[[], T], default: T) -> T:
    # The personal playlists, stations and covers are extras — not worth failing
    # the whole list over the way a failed users_playlists_list() is.
    try:
        return fetch()
    except Exception as e:
        logger.debug("fetching %s failed: %s", what, e)
        return default


async def list_playlists(client: Client) -> dict:
    # My Wave and Liked Tracks are surfaced here as regular Playlist entries
    # (kind: radioStation / liked) rather than the front synthesizing them
    # from capability flags — see docs/protocol.md's Playlist.kind note.
    # This backend's capabilities always have browse.radio/likedTracks true,
    # so both are unconditional.
    def fetch() -> tuple[list[YPlaylist], int, str | None, list[Playlist], list[Playlist]]:
        real_playlists = client.users_playlists_list() or []
        likes.load(client)  # also refreshes every later track's liked/disliked
        liked_cover = _best_effort(
            "liked cover", lambda: _playlist_cover(client.users_playlists(LIKES_PLAYLIST_KIND)), None
        )
        stations = _best_effort("stations", lambda: _stations(client.rotor_stations_dashboard()), [])
        personal = _best_effort("personal playlists", lambda: _personal_playlists(client.feed()), [])
        return real_playlists, len(likes.liked), liked_cover, stations, personal

    real_playlists, liked_count, liked_cover, stations, personal = await asyncio.to_thread(fetch)
    membership.invalidate()  # the front's refresh point — contents may have changed elsewhere
    wave_cover = next((s.coverUrl for s in stations if s.id == WAVE_STATION_ID), None)
    stations = [s for s in stations if s.id != WAVE_STATION_ID]
    playlists = [_wave_playlist(wave_cover), _liked_playlist(liked_count, liked_cover), *stations, *personal]
    playlists += [to_playlist(p) for p in real_playlists]
    return {"playlists": [p.to_dict() for p in playlists]}


async def list_tracks(client: Client, playlist_id: str) -> dict:
    playlist = await asyncio.to_thread(_find_playlist, client, playlist_id)
    if playlist is None:
        raise LookupError(playlist_id)
    await asyncio.to_thread(likes.ensure_loaded, client)
    tracks = await asyncio.to_thread(playlist_tracks, playlist)
    return {"tracks": [to_track(t).to_dict() for t in tracks]}


async def list_liked(client: Client) -> dict:
    def fetch() -> list[YTrack]:
        liked = client.users_likes_tracks()
        ids = liked.tracks_ids if liked else []
        likes.ensure_loaded(client)
        return client.tracks(ids) if ids else []

    tracks = await asyncio.to_thread(fetch)
    # Every track here came from users_likes_tracks() itself — liked=True
    # unconditionally, rather than leaving it unset like to_track()'s
    # default (see its docstring).
    return {"tracks": [to_track(t, liked=True).to_dict() for t in tracks]}


# --- editing playlists (docs/protocol.md §7.6) ---


class PlaylistMembership:
    """Which of the account's own playlists contain which tracks.

    Answers catalog.getTrackPlaylists without fetching every playlist per
    question: loaded in one users_playlists(kind=[...]) call on first use,
    dropped on catalog.listPlaylists (the front's refresh point) and kept
    in step with this backend's own add/remove calls.
    """

    def __init__(self) -> None:
        self._tracks: dict[str, list[str]] | None = None  # playlist id -> bare track ids, in order

    def invalidate(self) -> None:
        self._tracks = None

    def _ensure_loaded(self, client: Client) -> dict[str, list[str]]:
        if self._tracks is None:
            own = client.users_playlists_list() or []
            kinds = [p.kind for p in own]
            full = client.users_playlists(kinds) if kinds else []
            if full is not None and not isinstance(full, list):
                full = [full]
            self._tracks = {
                p.playlist_id: [_bare_id(short.id) for short in (p.tracks or [])] for p in (full or [])
            }
        return self._tracks

    def playlists_with(self, client: Client, track_id: str) -> list[str]:
        tid = _bare_id(track_id)
        return [pid for pid, ids in self._ensure_loaded(client).items() if tid in ids]

    def added(self, playlist_id: str, track_id: str) -> None:
        if self._tracks is not None and playlist_id in self._tracks:
            self._tracks[playlist_id].append(_bare_id(track_id))

    def removed(self, playlist_id: str, track_id: str) -> None:
        ids = (self._tracks or {}).get(playlist_id)
        tid = _bare_id(track_id)
        if ids is not None and tid in ids:
            ids.remove(tid)


membership = PlaylistMembership()


async def get_track_playlists(client: Client, track_id: str) -> dict:
    ids = await asyncio.to_thread(membership.playlists_with, client, track_id)
    return {"playlistIds": ids}


def _album_id_for(client: Client, track_id: str) -> str:
    # Our Track.id is "<trackId>:<albumId>" when the album is known; the
    # insert call needs the album explicitly.
    bare, _, album = str(track_id).partition(":")
    if album:
        return album
    tracks = client.tracks([bare]) or []
    albums = tracks[0].albums if tracks else None
    if not albums:
        raise LookupError(track_id)
    return str(albums[0].id)


async def add_to_playlist(client: Client, playlist_id: str, track_id: str) -> dict:
    def add() -> int:
        playlist = _find_playlist(client, playlist_id)
        if playlist is None:
            raise LookupError(playlist_id)
        result = client.users_playlists_insert_track(
            playlist.kind,
            _bare_id(track_id),
            _album_id_for(client, track_id),
            at=playlist.track_count or 0,  # append
            revision=playlist.revision or 1,
        )
        return (result.track_count if result else (playlist.track_count or 0) + 1) or 0

    count = await asyncio.to_thread(add)
    membership.added(playlist_id, track_id)
    return {"trackCount": count}


async def remove_from_playlist(client: Client, playlist_id: str, track_id: str) -> dict:
    def remove() -> int:
        playlist = _find_playlist(client, playlist_id)
        if playlist is None:
            raise LookupError(playlist_id)
        shorts = playlist.tracks or playlist.fetch_tracks() or []
        tid = _bare_id(track_id)
        index = next((i for i, short in enumerate(shorts) if _bare_id(short.id) == tid), None)
        if index is None:
            raise LookupError(track_id)
        # Deletes the [index, index + 1) range at the playlist's current
        # revision (the API rejects a stale one).
        result = client.users_playlists_delete_track(playlist.kind, index, index + 1, revision=playlist.revision or 1)
        return (result.track_count if result else (playlist.track_count or 1) - 1) or 0

    count = await asyncio.to_thread(remove)
    membership.removed(playlist_id, track_id)
    return {"trackCount": count}

