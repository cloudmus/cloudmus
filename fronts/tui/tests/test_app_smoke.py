import os
import shutil
import subprocess
import sys

import pytest

from cloudmus_tui.app import PlayerApp, SourceItem, TrackItem
from cloudmus_tui.discovery import BackendManifest
from cloudmus_tui.playback_engine import QueueEntry
from cloudmus_tui import session


def _make_silent_mp3(path, duration_sec=2):
    subprocess.run(
        [
            "ffmpeg", "-y", "-f", "lavfi", "-i", f"anullsrc=r=44100:cl=mono",
            "-t", str(duration_sec), "-q:a", "9", str(path),
        ],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )


@pytest.mark.asyncio
async def test_app_boots_with_local_folder_backend(tmp_path):
    (tmp_path / "AlbumA").mkdir()
    if shutil.which("ffmpeg"):
        _make_silent_mp3(tmp_path / "AlbumA" / "track1.mp3")
    else:
        (tmp_path / "AlbumA" / "track1.mp3").write_bytes(b"")
    os.environ["CLOUDMUS_LOCAL_FOLDER_MUSIC_DIR"] = str(tmp_path)

    manifest = BackendManifest(
        id="local-folder",
        name="Local Folder",
        argv=["python", "-m", "cloudmus_backend_local"],
        protocol_version="1.0",
        manifest_path=None,
    )
    app = PlayerApp(manifests=[manifest], session_path=tmp_path / "session.json")
    async with app.run_test() as pilot:
        await pilot.pause()
        for _ in range(20):
            if app.source_manager.clients:
                break
            await pilot.pause(0.1)

        assert "local-folder" in app.source_manager.clients
        assert app.playback_engine is not None

        sidebar = app.query_one("#sidebar")
        await pilot.pause(0.2)
        labels = [str(item.children[0].render()) for item in sidebar.children]
        assert any("AlbumA" in label for label in labels)

        playlist_item = sidebar.children[0]
        await app._load_source(playlist_item)
        await pilot.pause(0.2)

        tracks_view = app.query_one("#tracks")
        assert len(tracks_view.children) == 1

        assert app._current_source_id == "local-folder"
        app.playback_engine.load_queue(app._current_source_id, app._current_tracks, 0)

        for _ in range(20):
            if app.playback_engine._resolved_request_id is not None:
                break
            await pilot.pause(0.1)

        assert app.playback_engine._resolved_request_id is not None
        assert app.playback_engine.current().track["id"] == app._current_tracks[0]["id"]

    await app.source_manager.shutdown_all()
    if app.playback_engine is not None:
        app.playback_engine.shutdown()


@pytest.mark.asyncio
async def test_track_change_scrolls_open_playlist(tmp_path):
    app = PlayerApp(manifests=[], session_path=tmp_path / "session.json")
    async with app.run_test() as pilot:
        tracks_view = app.query_one("#tracks")
        tracks = [{"id": str(i), "title": f"Track {i}"} for i in range(40)]
        app._current_source_id = "test"
        app._current_tracks = tracks
        for i, track in enumerate(tracks):
            await tracks_view.append(TrackItem(i, track))
        await pilot.pause()

        assert app.playback_engine is not None
        app.playback_engine.queue = [QueueEntry("test", track) for track in tracks]
        app.playback_engine.index = 30
        app._on_track_change(app.playback_engine.current())
        await pilot.pause()
        assert tracks_view.scroll_y > 0
        assert tracks_view.children[30].region.overlaps(tracks_view.scrollable_content_region)

        scroll = tracks_view.scroll_y
        app.playback_engine.index = 29
        app._on_track_change(app.playback_engine.current())
        await pilot.pause()
        assert tracks_view.scroll_y == scroll

        app.playback_engine.index = 2
        app._on_track_change(app.playback_engine.current())
        await pilot.pause()
        assert tracks_view.scroll_y == tracks_view.children[2].virtual_region.y

    if app.playback_engine is not None:
        app.playback_engine.shutdown()


@pytest.mark.asyncio
async def test_restart_restores_playlist_and_last_song_without_playing(tmp_path, monkeypatch):
    music_dir = tmp_path / "music"
    album = music_dir / "AlbumA"
    album.mkdir(parents=True)
    for name in ("track1.mp3", "track2.mp3"):
        (album / name).write_bytes(b"")
    monkeypatch.setenv("CLOUDMUS_LOCAL_FOLDER_MUSIC_DIR", str(music_dir))
    manifest = BackendManifest(
        id="local-folder", name="Local Folder", argv=[sys.executable, "-m", "cloudmus_backend_local"],
        protocol_version="1.0", manifest_path=None,
    )
    path = tmp_path / "session.json"

    first = PlayerApp(manifests=[manifest], session_path=path)
    async with first.run_test() as pilot:
        await pilot.pause()
        await first._load_source(first.query_one("#sidebar").children[0])
        assert len(first._current_tracks) == 2
        assert first.playback_engine is not None
        first.playback_engine.queue = [QueueEntry("local-folder", track) for track in first._current_tracks]
        first.playback_engine.index = 1
        first._on_track_change(first.playback_engine.current())
        assert first._resume_index == 1
    await first.source_manager.shutdown_all()
    first.playback_engine.shutdown()

    second = PlayerApp(manifests=[manifest], session_path=path)
    async with second.run_test() as pilot:
        await pilot.pause()
        assert len(second._current_tracks) == 2
        assert second._resume_index == 1
        assert second.playback_engine is not None
        assert second.playback_engine.queue == []
        starts = []
        second.playback_engine.load_queue = lambda source, tracks, index: starts.append((source, tracks, index))
        second.action_toggle_pause()
        assert starts == [("local-folder", second._current_tracks, 1)]
    await second.source_manager.shutdown_all()
    second.playback_engine.shutdown()


@pytest.mark.asyncio
async def test_cached_playlist_is_visible_before_backend_connects(tmp_path):
    path = tmp_path / "session.json"
    tracks = [{"id": "one", "title": "One"}, {"id": "two", "title": "Two"}]
    session.save(path, session.Session("offline", "favorites", "two", 1, "Favorites", "playlist", tracks))
    app = PlayerApp(manifests=[], session_path=path)
    async with app.run_test() as pilot:
        await pilot.pause()
        assert app._current_tracks == tracks
        assert app._resume_index == 1
        assert len(app.query_one("#tracks").children) == 2
        assert app.playback_engine.queue == []
    app.playback_engine.shutdown()


@pytest.mark.asyncio
async def test_saved_track_is_visible_at_startup(tmp_path):
    path = tmp_path / "session.json"
    tracks = [{"id": str(index), "title": f"Track {index}"} for index in range(40)]
    session.save(path, session.Session("offline", "favorites", "35", 35, "Favorites", "playlist", tracks))
    app = PlayerApp(manifests=[], session_path=path)
    async with app.run_test() as pilot:
        await pilot.pause()
        tracks_view = app.query_one("#tracks")
        assert tracks_view.scroll_y > 0
        assert tracks_view.children[35].region.overlaps(tracks_view.scrollable_content_region)

        class PlaylistClient:
            async def request(self, method, params):
                assert method == "catalog.listTracks"
                return {"tracks": tracks}

        app.source_manager.clients["offline"] = PlaylistClient()
        item = SourceItem("Favorites", "playlist", "offline", {"id": "favorites", "title": "Favorites"})
        assert await app._load_source(item, restoring=session.load(path))
        await pilot.pause()
        assert tracks_view.children[35].region.overlaps(tracks_view.scrollable_content_region)
    app.playback_engine.shutdown()


@pytest.mark.asyncio
async def test_dynamic_playlist_tracks_survive_restart(tmp_path):
    path = tmp_path / "session.json"
    app = PlayerApp(manifests=[], session_path=path)
    async with app.run_test() as pilot:
        app._current_source_id = "fake"
        app._current_playlist_id = "wave"
        app._current_playlist_title = "Wave"
        app._current_kind = "wave"
        app.playback_engine.wave = True
        app.playback_engine.wave_source_id = "fake"
        app.playback_engine.wave_station_id = "station"
        app.playback_engine.queue = [QueueEntry("fake", {"id": "first", "title": "First"})]
        app._resume_index = 1
        app._on_backend_notification("fake", "radio/tracksAdded", {
            "stationId": "station", "tracks": [{"id": "second", "title": "Second"}],
        })
        await pilot.pause()
        assert [track["id"] for track in session.load(path).tracks] == ["first", "second"]
    app.playback_engine.shutdown()

    restored = PlayerApp(manifests=[], session_path=path)
    async with restored.run_test() as pilot:
        await pilot.pause()
        assert [track["id"] for track in restored._current_tracks] == ["first", "second"]
        assert restored._resume_index == 1
        assert len(restored.query_one("#tracks").children) == 2
        assert restored.playback_engine.queue == []

        class RadioClient:
            async def request(self, method, params):
                assert method == "catalog.startRadio"
                assert params == {"seed": "wave"}
                return {"stationId": "new-station", "initialTracks": [{"id": "fresh", "title": "Fresh"}]}

        restored.source_manager.clients["fake"] = RadioClient()
        starts = []
        restored.playback_engine.start_radio = lambda source, station, tracks: starts.append((source, station, tracks))
        restored.action_toggle_pause()
        await pilot.pause()
        assert starts == [("fake", "new-station", [
            {"id": "second", "title": "Second"}, {"id": "fresh", "title": "Fresh"},
        ])]
    restored.playback_engine.shutdown()
