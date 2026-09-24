import pytest

from cloudmus_backend_ytmusic import catalog


def _song(
    videoId="abc",
    title="Song",
    artists=None,
    album=None,
    duration_seconds=200,
    thumbnails=None,
    isExplicit=False,
    likeStatus=None,
    isAvailable=True,
):
    song = {
        "videoId": videoId,
        "title": title,
        "artists": artists if artists is not None else [{"id": "a1", "name": "Artist"}],
        "album": album if album is not None else {"id": "al1", "name": "Album"},
        "duration_seconds": duration_seconds,
        "thumbnails": thumbnails if thumbnails is not None else [{"url": "https://img/low.jpg"}, {"url": "https://img/high.jpg"}],
        "isExplicit": isExplicit,
        "isAvailable": isAvailable,
    }
    if likeStatus is not None:
        song["likeStatus"] = likeStatus
    return song


def test_to_track_maps_fields():
    t = catalog.to_track(_song())
    assert t.id == "abc"
    assert t.title == "Song"
    assert t.artists[0].name == "Artist"
    assert t.album.title == "Album"
    assert t.coverUrl == "https://img/high.jpg"
    assert t.durationMs == 200000
    assert t.webUrl == "https://music.youtube.com/watch?v=abc"


def test_to_track_handles_missing_album_and_cover():
    t = catalog.to_track(_song(album={}, thumbnails=[]))
    assert t.album is None or t.album.title == ""
    assert t.coverUrl is None


def test_to_track_maps_like_status():
    liked = catalog.to_track(_song(likeStatus="LIKE"))
    assert liked.liked is True
    disliked = catalog.to_track(_song(likeStatus="DISLIKE"))
    assert disliked.liked is False
    unknown = catalog.to_track(_song())
    assert unknown.liked is None


def test_to_playlist_maps_fields():
    p = catalog.to_playlist({"playlistId": "PL1", "title": "My Playlist", "count": 42})
    d = p.to_dict()
    # No "owned" in the entry: not the user's own, so not editable.
    assert d == {"id": "PL1", "title": "My Playlist", "trackCount": 42, "kind": "playlist", "editable": False}


def test_to_playlist_falls_back_to_untitled():
    p = catalog.to_playlist({"playlistId": "PL1", "title": None, "count": 0})
    assert p.title == "(untitled)"


def test_to_playlist_coerces_string_count():
    # get_library_playlists() returns "count" as a numeric-looking string
    # for regular playlists in practice — confirmed against a live account.
    # trackCount must stay a real int (protocol.Playlist.trackCount) or the
    # whole listPlaylists response fails to parse on the Qt side.
    p = catalog.to_playlist({"playlistId": "PL1", "title": "Test", "count": "28"})
    assert p.trackCount == 28
    assert isinstance(p.trackCount, int)


def test_to_playlist_handles_unparseable_count():
    p = catalog.to_playlist({"playlistId": "PL1", "title": "Test", "count": "unknown"})
    assert p.trackCount == 0


def test_to_track_coerces_string_duration():
    t = catalog.to_track(_song(duration_seconds="225"))
    assert t.durationMs == 225000
    assert isinstance(t.durationMs, int)


def test_available_tracks_filters_unavailable_and_missing_video_id():
    songs = [_song(videoId="a"), _song(videoId="b", isAvailable=False), {"title": "no id"}]
    result = catalog._available_tracks(songs)
    assert [s["videoId"] for s in result] == ["a"]


def test_find_supermix_id_matches_exact_title_across_shelves():
    # Confirmed live against a real account: "My Supermix" appears under
    # more than one shelf (e.g. both "Listen again" and "Mixed for you"),
    # always with the same playlistId — matching on the item's own title
    # rather than the (less stable) shelf title.
    home = [
        {"title": "Listen again", "contents": [{"title": "Some Song", "playlistId": "RDAMVMxyz"}]},
        {
            "title": "Mixed for you",
            "contents": [
                {"title": "My Supermix", "playlistId": "RDTMsupermix123"},
                {"title": "My Mix 1", "playlistId": "RDTMmix1"},
            ],
        },
    ]
    assert catalog._find_supermix_id(home) == "RDTMsupermix123"


def test_find_supermix_id_absent_returns_none():
    home = [{"title": "Listen again", "contents": [{"title": "Some Song", "playlistId": "RDAMVMxyz"}]}]
    assert catalog._find_supermix_id(home) is None


def test_find_supermix_id_ignores_items_with_no_playlist_id():
    # e.g. an artist or album result card, which get_home() also returns
    # mixed into the same shelves (has "browseId", not "playlistId").
    home = [{"title": "Mixed for you", "contents": [{"title": "My Supermix", "browseId": "UCxyz"}]}]
    assert catalog._find_supermix_id(home) is None


def test_supermix_playlist_shape():
    p = catalog._supermix_playlist("RDTMsupermix123")
    d = p.to_dict()
    assert d == {"id": "RDTMsupermix123", "title": "My Supermix", "trackCount": 0, "kind": "radioStation"}


class _FakeClient:
    def __init__(self, home, library_playlists, liked_tracks):
        self._home = home
        self._library_playlists = library_playlists
        self._liked_tracks = liked_tracks

    def get_home(self, limit=3):
        return self._home

    def get_library_playlists(self):
        return self._library_playlists

    def get_liked_songs(self):
        return {"tracks": self._liked_tracks}


@pytest.mark.asyncio
async def test_list_playlists_includes_supermix_when_present():
    client = _FakeClient(
        home=[{"title": "Mixed for you", "contents": [{"title": "My Supermix", "playlistId": "RDTMsupermix123"}]}],
        library_playlists=[{"playlistId": "PL1", "title": "My Playlist", "count": 1}],
        liked_tracks=[],
    )
    result = await catalog.list_playlists(client)
    kinds_by_id = {p["id"]: p["kind"] for p in result["playlists"]}
    assert kinds_by_id["RDTMsupermix123"] == "radioStation"
    assert kinds_by_id[catalog.LIKED_PLAYLIST_ID] == "liked"
    assert kinds_by_id["PL1"] == "playlist"


@pytest.mark.asyncio
async def test_list_playlists_omits_supermix_when_get_home_fails():
    class _FailingHomeClient(_FakeClient):
        def get_home(self, limit=3):
            raise RuntimeError("network blip")

    client = _FailingHomeClient(home=None, library_playlists=[], liked_tracks=[])
    result = await catalog.list_playlists(client)  # must not raise
    assert all(p["kind"] != "radioStation" for p in result["playlists"])


# --- editing playlists ---


class _EditClient:
    """Fake YTMusic: one owned and one saved playlist; edits recorded."""

    def __init__(self):
        self.contents = {
            "PLmine": [{"videoId": "a", "setVideoId": "s-a"}, {"videoId": "b", "setVideoId": "s-b"}],
            "PLsaved": [{"videoId": "b"}],
        }
        self.added = []
        self.removed = []

    def get_library_playlists(self, limit=25):
        return [{"playlistId": "PLmine", "owned": True}, {"playlistId": "PLsaved", "owned": False}]

    def get_playlist(self, playlist_id, limit=100):
        tracks = self.contents[playlist_id]
        return {"tracks": list(tracks), "trackCount": len(tracks)}

    def add_playlist_items(self, playlist_id, video_ids, duplicates=False):
        self.added.append((playlist_id, video_ids))
        self.contents[playlist_id] = self.contents[playlist_id] + [{"videoId": v, "setVideoId": "s-" + v} for v in video_ids]
        return {"status": "STATUS_SUCCEEDED"}

    def remove_playlist_items(self, playlist_id, videos):
        self.removed.append((playlist_id, videos))
        return "STATUS_SUCCEEDED"


@pytest.fixture(autouse=True)
def _fresh_membership(monkeypatch):
    monkeypatch.setattr(catalog, "membership", catalog.PlaylistMembership())


def test_only_owned_playlists_are_editable():
    assert catalog.to_playlist({"playlistId": "p", "title": "t", "owned": True}).editable is True
    assert catalog.to_playlist({"playlistId": "p", "title": "t", "owned": False}).editable is False


@pytest.mark.asyncio
async def test_get_track_playlists_only_considers_owned_playlists():
    client = _EditClient()
    assert (await catalog.get_track_playlists(client, "b"))["playlistIds"] == ["PLmine"]
    assert (await catalog.get_track_playlists(client, "z"))["playlistIds"] == []


@pytest.mark.asyncio
async def test_add_then_membership_includes_it():
    client = _EditClient()
    await catalog.get_track_playlists(client, "c")  # loads the cache
    result = await catalog.add_to_playlist(client, "PLmine", "c")
    assert client.added == [("PLmine", ["c"])]
    assert result == {"trackCount": 3}
    assert (await catalog.get_track_playlists(client, "c"))["playlistIds"] == ["PLmine"]


@pytest.mark.asyncio
async def test_remove_uses_the_entrys_set_video_id():
    client = _EditClient()
    result = await catalog.remove_from_playlist(client, "PLmine", "b")
    assert client.removed == [("PLmine", [{"videoId": "b", "setVideoId": "s-b"}])]
    assert result == {"trackCount": 1}


@pytest.mark.asyncio
async def test_remove_raises_for_a_video_not_in_the_playlist():
    with pytest.raises(LookupError):
        await catalog.remove_from_playlist(_EditClient(), "PLmine", "z")
