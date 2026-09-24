from types import SimpleNamespace

import pytest

from cloudmus_backend_yandex import catalog


@pytest.fixture(autouse=True)
def _fresh_like_cache(monkeypatch):
    # catalog.likes is module state — never let one test's cache leak into another.
    monkeypatch.setattr(catalog, "likes", catalog.LikeCache())
    monkeypatch.setattr(catalog, "membership", catalog.PlaylistMembership())


def _artist(id=1, name="Artist"):
    return SimpleNamespace(id=id, name=name)


def _album(id=10, title="Album", cover_uri="avatars.yandex.net/get-music-content/abc/%%"):
    return SimpleNamespace(id=id, title=title, cover_uri=cover_uri)


def _track(track_id="123", title="Song", artists=None, albums=None, duration_ms=200000, cover_uri=None, explicit=False):
    return SimpleNamespace(
        track_id=track_id,
        title=title,
        artists=artists if artists is not None else [_artist()],
        albums=albums if albums is not None else [_album()],
        duration_ms=duration_ms,
        cover_uri=cover_uri,
        explicit=explicit,
    )


def test_to_track_maps_fields():
    t = catalog.to_track(_track())
    assert t.id == "123"
    assert t.title == "Song"
    assert t.artists[0].name == "Artist"
    assert t.album.title == "Album"
    assert t.album.coverUrl == "https://avatars.yandex.net/get-music-content/abc/400x400"
    assert t.durationMs == 200000


def test_to_track_handles_missing_album_and_cover():
    t = catalog.to_track(_track(albums=[], cover_uri=None))
    assert t.album is None
    assert t.coverUrl is None


def test_to_track_liked_defaults_unset_but_can_be_forced_true():
    # yandex_music's Track has no liked attribute of its own — list_liked()
    # passes liked=True explicitly since every track it returns came from
    # users_likes_tracks() (see catalog.py's to_track docstring).
    assert catalog.to_track(_track()).liked is None
    assert catalog.to_track(_track(), liked=True).liked is True


def _likes_client(liked_ids, disliked_ids):
    return SimpleNamespace(
        users_likes_tracks=lambda: SimpleNamespace(tracks_ids=liked_ids),
        users_dislikes_tracks=lambda: SimpleNamespace(tracks_ids=disliked_ids),
    )


def test_to_track_annotates_liked_and_disliked_from_the_account_cache():
    # Lists carry "trackId:albumId"; a Track's own id may be bare.
    catalog.likes.load(_likes_client(["123:10"], ["456:11"]))
    liked = catalog.to_track(_track(track_id="123"))
    disliked = catalog.to_track(_track(track_id="456:11"))
    neither = catalog.to_track(_track(track_id="789"))
    assert (liked.liked, liked.disliked) == (True, False)
    assert (disliked.liked, disliked.disliked) == (False, True)
    assert (neither.liked, neither.disliked) == (False, False)


def test_like_cache_feedback_updates_cross_clear():
    catalog.likes.load(_likes_client([], ["123"]))
    catalog.likes.set_liked("123", True)
    t = catalog.to_track(_track(track_id="123"))
    assert (t.liked, t.disliked) == (True, False)
    catalog.likes.set_disliked("123", True)
    t = catalog.to_track(_track(track_id="123"))
    assert (t.liked, t.disliked) == (False, True)


def test_like_cache_survives_a_failing_dislikes_fetch():
    def broken():
        raise RuntimeError("nope")

    client = SimpleNamespace(users_likes_tracks=lambda: SimpleNamespace(tracks_ids=["1"]), users_dislikes_tracks=broken)
    catalog.likes.load(client)
    assert catalog.likes.liked == {"1"} and catalog.likes.disliked == set()


def test_to_playlist_maps_fields():
    p = catalog.to_playlist(SimpleNamespace(playlist_id="1:3", title="My Playlist", track_count=42))
    d = p.to_dict()
    # The account's own playlists are editable (docs/protocol.md §7.6).
    assert d == {"id": "1:3", "title": "My Playlist", "trackCount": 42, "kind": "playlist", "editable": True}


def test_to_playlist_falls_back_to_untitled():
    p = catalog.to_playlist(SimpleNamespace(playlist_id="1:3", title=None, track_count=0))
    assert p.title == "(untitled)"


# --- editing playlists ---


class _EditClient:
    """Fake yandex_music client: two own playlists, insert/delete recorded."""

    def __init__(self):
        self.playlists = {
            3: SimpleNamespace(playlist_id="1:3", kind=3, revision=7, track_count=2,
                               tracks=[SimpleNamespace(id="10"), SimpleNamespace(id="20")]),
            5: SimpleNamespace(playlist_id="1:5", kind=5, revision=2, track_count=1,
                               tracks=[SimpleNamespace(id="20")]),
        }
        self.inserted = []
        self.deleted = []

    def users_playlists_list(self):
        return list(self.playlists.values())

    def users_playlists(self, kind, user_id=None):
        if isinstance(kind, list):
            return [self.playlists[k] for k in kind]
        return self.playlists[int(kind)]

    def tracks(self, ids):
        return [SimpleNamespace(albums=[SimpleNamespace(id=99)])]

    def users_playlists_insert_track(self, kind, track_id, album_id, at=0, revision=1):
        self.inserted.append((kind, track_id, album_id, at, revision))
        return SimpleNamespace(track_count=self.playlists[kind].track_count + 1)

    def users_playlists_delete_track(self, kind, from_, to, revision=1):
        self.deleted.append((kind, from_, to, revision))
        return SimpleNamespace(track_count=self.playlists[kind].track_count - 1)


@pytest.mark.asyncio
async def test_get_track_playlists_lists_own_playlists_containing_the_track():
    client = _EditClient()
    assert (await catalog.get_track_playlists(client, "20:777"))["playlistIds"] == ["1:3", "1:5"]
    assert (await catalog.get_track_playlists(client, "10"))["playlistIds"] == ["1:3"]
    assert (await catalog.get_track_playlists(client, "30"))["playlistIds"] == []


@pytest.mark.asyncio
async def test_add_to_playlist_appends_at_current_revision_and_updates_membership():
    client = _EditClient()
    await catalog.get_track_playlists(client, "30")  # loads the cache
    result = await catalog.add_to_playlist(client, "1:5", "30:444")
    assert client.inserted == [(5, "30", "444", 1, 2)]  # at=track_count, revision
    assert result == {"trackCount": 2}
    assert (await catalog.get_track_playlists(client, "30"))["playlistIds"] == ["1:5"]


@pytest.mark.asyncio
async def test_add_to_playlist_looks_up_the_album_when_the_id_has_none():
    client = _EditClient()
    await catalog.add_to_playlist(client, "1:3", "30")
    assert client.inserted[0][2] == "99"


@pytest.mark.asyncio
async def test_remove_from_playlist_deletes_its_first_occurrence():
    client = _EditClient()
    await catalog.get_track_playlists(client, "20")
    result = await catalog.remove_from_playlist(client, "1:3", "20:777")
    assert client.deleted == [(3, 1, 2, 7)]  # [index, index + 1) at revision 7
    assert result == {"trackCount": 1}
    assert (await catalog.get_track_playlists(client, "20"))["playlistIds"] == ["1:5"]


@pytest.mark.asyncio
async def test_remove_from_playlist_raises_for_a_track_not_in_it():
    with pytest.raises(LookupError):
        await catalog.remove_from_playlist(_EditClient(), "1:5", "10")

