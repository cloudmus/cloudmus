"""catalog.listPlaylists / listTracks / listLiked, built on ytmusicapi's
YTMusic client. Track ids are YouTube videoIds throughout, so playback.py can
resolve a stream from a trackId with no catalog lookup step."""
from __future__ import annotations

import asyncio

from ytmusicapi import YTMusic

from rpc_common.generated.models import Album, Artist, Playlist, Track

LIKED_PLAYLIST_ID = "__liked__"
LIKED_PLAYLIST_TITLE = "Liked Songs"

# "My Supermix" (radio.py's My-Wave-equivalent) has no dedicated ytmusicapi
# lookup — it's discovered by scanning get_home()'s shelves for an item
# with this exact title, confirmed live against a real account: it shows
# up (with a stable "RDTM..."-prefixed playlistId) under both a "Listen
# again" and a "Mixed for you" shelf. Matching on the literal title string
# rather than the shelf title (which varies) or the id prefix (shared by
# several other auto-mixes YouTube also surfaces the same way — "My Mix 1"
# .."My Mix 7", "Discover Mix", "New Release Mix", ...).
SUPERMIX_TITLE = "My Supermix"


def _cover_url(thumbnails: list[dict] | None) -> str | None:
    if not thumbnails:
        return None
    return thumbnails[-1].get("url")  # thumbnails are ordered smallest-first


def _as_int(value: object) -> int:
    # ytmusicapi is inconsistent about numeric-looking fields' actual JSON
    # type — confirmed directly against a live account: get_library_playlists()
    # returns a real playlist's "count" as a numeric-looking *string* (e.g.
    # "28"), while special entries (liked-music/podcast-queue placeholders)
    # carry a real int 0. The protocol's Playlist.trackCount/Track.durationMs
    # are strict integers (fronts/qt's generated ...::fromJson() throws
    # ProtocolParseError on a type mismatch), so passing either through
    # unconverted breaks the *entire* listPlaylists/listTracks response, not
    # just that one field — used for both below since the same inconsistency
    # is plausible for "duration_seconds" too (never actually observed there,
    # but the same library, same failure mode, cheap enough to guard either
    # way).
    if isinstance(value, bool):
        return 0
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value)
        except ValueError:
            return 0
    return 0


def to_track(song: dict) -> Track:
    artists = [Artist(id=a.get("id") or "", name=a.get("name") or "") for a in (song.get("artists") or [])]
    album = None
    if song.get("album"):
        album = Album(id=song["album"].get("id") or "", title=song["album"].get("name") or "")
    video_id = song["videoId"]
    return Track(
        id=video_id,
        title=song.get("title") or "",
        artists=artists,
        durationMs=_as_int(song.get("duration_seconds")) * 1000,
        album=album,
        coverUrl=_cover_url(song.get("thumbnails")),
        liked=song.get("likeStatus") == "LIKE" if "likeStatus" in song else None,
        disliked=song.get("likeStatus") == "DISLIKE" if "likeStatus" in song else None,
        explicit=song.get("isExplicit", False),
        webUrl=f"https://music.youtube.com/watch?v={video_id}",
    )


def to_playlist(playlist: dict) -> Playlist:
    return Playlist(
        id=playlist["playlistId"],
        title=playlist.get("title") or "(untitled)",
        trackCount=_as_int(playlist.get("count")),
        kind="playlist",
        coverUrl=_cover_url(playlist.get("thumbnails")),
        # Only the user's own playlists — not ones saved from others.
        editable=bool(playlist.get("owned")),
    )


def _liked_playlist(track_count: int) -> Playlist:
    return Playlist(id=LIKED_PLAYLIST_ID, title=LIKED_PLAYLIST_TITLE, trackCount=track_count, kind="liked")


def _find_supermix_id(home: list[dict]) -> str | None:
    for shelf in home:
        for item in shelf.get("contents") or []:
            if item.get("title") == SUPERMIX_TITLE and item.get("playlistId"):
                return item["playlistId"]
    return None


def _supermix_playlist(playlist_id: str) -> Playlist:
    # trackCount=0 — same "not applicable" convention as Yandex's My Wave
    # (a continuous radio, not a fixed-length list; see docs/protocol.md's
    # kind: radioStation note).
    return Playlist(id=playlist_id, title=SUPERMIX_TITLE, trackCount=0, kind="radioStation")


def _available_tracks(songs: list[dict]) -> list[dict]:
    # isAvailable=False tracks (region-blocked, taken down, ...) would just
    # fail in playback.py's yt-dlp resolution — filter them out here instead
    # of surfacing a dead entry the front can select but never play.
    return [s for s in songs if s.get("isAvailable", True) and s.get("videoId")]


async def list_playlists(client: YTMusic) -> dict:
    def fetch() -> tuple[list[dict], int, str | None]:
        real_playlists = client.get_library_playlists() or []
        liked = client.get_liked_songs()
        liked_count = len(_available_tracks(liked.get("tracks") or []))
        try:
            supermix_id = _find_supermix_id(client.get_home(limit=10))
        except Exception:
            # Best-effort — Supermix is a nice-to-have extra entry, not
            # worth failing the whole playlists list over a get_home()
            # hiccup the way a failed get_liked_songs()/
            # get_library_playlists() legitimately would.
            supermix_id = None
        return real_playlists, liked_count, supermix_id

    real_playlists, liked_count, supermix_id = await asyncio.to_thread(fetch)
    membership.invalidate()  # the front's refresh point — contents may have changed elsewhere
    playlists = [_liked_playlist(liked_count)]
    if supermix_id:
        playlists.append(_supermix_playlist(supermix_id))
    playlists += [to_playlist(p) for p in real_playlists]
    return {"playlists": [p.to_dict() for p in playlists]}


async def list_tracks(client: YTMusic, playlist_id: str) -> dict:
    if playlist_id == LIKED_PLAYLIST_ID:
        return await list_liked(client)

    def fetch() -> dict:
        return client.get_playlist(playlist_id)

    try:
        playlist = await asyncio.to_thread(fetch)
    except Exception:
        raise LookupError(playlist_id)
    songs = _available_tracks(playlist.get("tracks") or [])
    return {"tracks": [to_track(s).to_dict() for s in songs]}


async def list_liked(client: YTMusic) -> dict:
    def fetch() -> dict:
        return client.get_liked_songs()

    liked = await asyncio.to_thread(fetch)
    songs = _available_tracks(liked.get("tracks") or [])
    return {"tracks": [to_track(s).to_dict() for s in songs]}


# --- editing playlists (docs/protocol.md §7.6) ---


class PlaylistMembership:
    """Which of the user's own playlists contain which videos.

    Answers catalog.getTrackPlaylists without re-fetching every playlist
    per question: each owned library playlist is fetched once, on first
    use; dropped on catalog.listPlaylists (the front's refresh point) and
    kept in step with this backend's own add/remove calls.
    """

    def __init__(self) -> None:
        self._videos: dict[str, list[str]] | None = None  # playlist id -> videoIds, in order

    def invalidate(self) -> None:
        self._videos = None

    def _ensure_loaded(self, client: YTMusic) -> dict[str, list[str]]:
        if self._videos is None:
            owned = [p for p in (client.get_library_playlists(limit=None) or []) if p.get("owned")]
            self._videos = {}
            for p in owned:
                full = client.get_playlist(p["playlistId"], limit=None)
                self._videos[p["playlistId"]] = [t["videoId"] for t in full.get("tracks") or [] if t.get("videoId")]
        return self._videos

    def playlists_with(self, client: YTMusic, video_id: str) -> list[str]:
        return [pid for pid, ids in self._ensure_loaded(client).items() if video_id in ids]

    def added(self, playlist_id: str, video_id: str) -> None:
        if self._videos is not None and playlist_id in self._videos:
            self._videos[playlist_id].append(video_id)

    def removed(self, playlist_id: str, video_id: str) -> None:
        ids = (self._videos or {}).get(playlist_id)
        if ids is not None and video_id in ids:
            ids.remove(video_id)


membership = PlaylistMembership()


async def get_track_playlists(client: YTMusic, video_id: str) -> dict:
    ids = await asyncio.to_thread(membership.playlists_with, client, video_id)
    return {"playlistIds": ids}


async def add_to_playlist(client: YTMusic, playlist_id: str, video_id: str) -> dict:
    def add() -> int:
        # duplicates=True: appending is what was asked, even if it's there.
        client.add_playlist_items(playlist_id, [video_id], duplicates=True)
        return _as_int(client.get_playlist(playlist_id, limit=1).get("trackCount"))

    count = await asyncio.to_thread(add)
    membership.added(playlist_id, video_id)
    return {"trackCount": count}


async def remove_from_playlist(client: YTMusic, playlist_id: str, video_id: str) -> dict:
    def remove() -> int:
        tracks = client.get_playlist(playlist_id, limit=None).get("tracks") or []
        # Removal is by the entry's setVideoId (its slot in this playlist),
        # not the videoId — the first occurrence's.
        entry = next((t for t in tracks if t.get("videoId") == video_id and t.get("setVideoId")), None)
        if entry is None:
            raise LookupError(video_id)
        client.remove_playlist_items(playlist_id, [{"videoId": video_id, "setVideoId": entry["setVideoId"]}])
        return max(0, len(tracks) - 1)

    count = await asyncio.to_thread(remove)
    membership.removed(playlist_id, video_id)
    return {"trackCount": count}

