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
    assert d == {"id": "PL1", "title": "My Playlist", "trackCount": 42, "kind": "playlist"}


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
