import os
import shutil
import subprocess

import pytest

from cloudmus_tui.app import PlayerApp
from cloudmus_tui.discovery import BackendManifest


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
    app = PlayerApp(manifests=[manifest])
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
