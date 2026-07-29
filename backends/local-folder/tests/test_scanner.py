from pathlib import Path

from cloudmus_backend_local import scanner


def _touch(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"")


def test_subfolders_become_playlists(tmp_path: Path):
    _touch(tmp_path / "Album A" / "track1.mp3")
    _touch(tmp_path / "Album A" / "track2.mp3")
    _touch(tmp_path / "Album B" / "track1.flac")

    playlists = scanner.scan(tmp_path)

    ids = {p.id for p in playlists}
    assert ids == {"Album A", "Album B"}
    by_id = {p.id: p for p in playlists}
    assert len(by_id["Album A"].files) == 2
    assert len(by_id["Album B"].files) == 1


def test_flat_files_become_single_playlist(tmp_path: Path):
    _touch(tmp_path / "track1.mp3")
    _touch(tmp_path / "track2.mp3")

    playlists = scanner.scan(tmp_path)

    assert len(playlists) == 1
    assert playlists[0].id == scanner.ROOT_PLAYLIST_ID
    assert len(playlists[0].files) == 2


def test_mixed_root_files_and_subfolders(tmp_path: Path):
    _touch(tmp_path / "loose.mp3")
    _touch(tmp_path / "Album A" / "track1.mp3")

    playlists = scanner.scan(tmp_path)

    ids = {p.id for p in playlists}
    assert ids == {"Album A", scanner.ROOT_PLAYLIST_ID}


def test_non_audio_files_are_ignored(tmp_path: Path):
    _touch(tmp_path / "readme.txt")
    _touch(tmp_path / "track.mp3")

    playlists = scanner.scan(tmp_path)

    assert len(playlists) == 1
    assert len(playlists[0].files) == 1


def test_empty_directory_has_no_playlists(tmp_path: Path):
    assert scanner.scan(tmp_path) == []


def test_resolve_track_path_rejects_escape(tmp_path: Path):
    _touch(tmp_path / "track.mp3")
    (tmp_path.parent / "outside.mp3").write_bytes(b"")
    try:
        assert scanner.resolve_track_path(tmp_path, "track.mp3") is not None
        assert scanner.resolve_track_path(tmp_path, "../outside.mp3") is None
    finally:
        (tmp_path.parent / "outside.mp3").unlink(missing_ok=True)


def test_read_track_falls_back_to_filename_when_untagged(tmp_path: Path):
    _touch(tmp_path / "My Song.mp3")
    track = scanner.read_track(tmp_path / "My Song.mp3", tmp_path)
    assert track.title == "My Song"
    assert track.id == "My Song.mp3"
