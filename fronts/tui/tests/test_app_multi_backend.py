import os
import signal

import pytest


def _pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    return True

from cloudmus_tui.app import PlayerApp
from cloudmus_tui.discovery import BackendManifest
from cloudmus_tui.source_manager import BACKEND_UNAVAILABLE


def _manifests(music_dir):
    os.environ["CLOUDMUS_LOCAL_FOLDER_MUSIC_DIR"] = str(music_dir)
    return [
        BackendManifest(
            id="local-folder", name="Local Folder",
            argv=["python", "-m", "cloudmus_backend_local"], protocol_version="1.0", manifest_path=None,
        ),
        BackendManifest(
            id="yandex-music", name="Yandex Music",
            argv=["python", "-m", "cloudmus_backend_yandex"], protocol_version="1.0", manifest_path=None,
        ),
    ]


@pytest.mark.asyncio
async def test_both_backends_spawn_and_sidebar_shows_both(tmp_path):
    (tmp_path / "AlbumA").mkdir()
    (tmp_path / "AlbumA" / "track1.mp3").write_bytes(b"")

    app = PlayerApp(manifests=_manifests(tmp_path))
    async with app.run_test() as pilot:
        for _ in range(30):
            if len(app.source_manager.clients) == 2:
                break
            await pilot.pause(0.1)

        assert set(app.source_manager.clients) == {"local-folder", "yandex-music"}

        # local-folder needs no auth, so its playlist should already be populated;
        # yandex-music requires auth and stays pending (no live account here).
        await pilot.pause(0.3)
        sidebar = app.query_one("#sidebar")
        labels = [str(item.children[0].render()) for item in sidebar.children]
        assert any("AlbumA" in label for label in labels)

    await app.source_manager.shutdown_all()
    if app.playback_engine is not None:
        app.playback_engine.shutdown()


@pytest.mark.asyncio
async def test_killed_backend_auto_restarts_other_keeps_working(tmp_path):
    """A single crash is transient: source_manager should detect the
    disconnect, restart the backend (it comes back up fine, since nothing
    is actually wrong with it), and the *other* backend must never notice —
    it keeps answering requests throughout."""
    (tmp_path / "AlbumA").mkdir()
    (tmp_path / "AlbumA" / "track1.mp3").write_bytes(b"")

    app = PlayerApp(manifests=_manifests(tmp_path))
    async with app.run_test() as pilot:
        for _ in range(30):
            if len(app.source_manager.clients) == 2:
                break
            await pilot.pause(0.1)

        yandex_client = app.source_manager.clients["yandex-music"]
        yandex_client._proc.send_signal(signal.SIGKILL)

        # local-folder must keep answering requests the whole time, unaffected.
        for _ in range(20):
            result = await app.source_manager.clients["local-folder"].request("catalog.listPlaylists", {})
            assert len(result["playlists"]) == 1
            await pilot.pause(0.1)

        # source_manager's restart backoff (docs/architecture-plan.md §5: 2s
        # first attempt) should bring yandex-music back up on its own.
        for _ in range(50):
            client = app.source_manager.clients.get("yandex-music")
            if client is not None and client.available:
                break
            await pilot.pause(0.2)

        client = app.source_manager.clients.get("yandex-music")
        assert client is not None and client.available
        assert client._proc.pid != yandex_client._proc.pid  # a genuinely new process

    await app.source_manager.shutdown_all()
    if app.playback_engine is not None:
        app.playback_engine.shutdown()


@pytest.mark.asyncio
async def test_repeatedly_crashing_backend_marked_unavailable(tmp_path, monkeypatch):
    """A backend that crashes immediately every time it's (re)started should
    exhaust source_manager's restart budget (docs/architecture-plan.md §5:
    2s/5s/10s, max 3 attempts) and end up reported as unavailable, without
    ever taking down the front."""
    from cloudmus_tui import source_manager as sm_module

    monkeypatch.setattr(sm_module, "RESTART_DELAYS", (0.05, 0.05, 0.05))

    (tmp_path / "AlbumA").mkdir()
    (tmp_path / "AlbumA" / "track1.mp3").write_bytes(b"")
    os.environ["CLOUDMUS_LOCAL_FOLDER_MUSIC_DIR"] = str(tmp_path)

    manifests = [
        BackendManifest(
            id="local-folder", name="Local Folder",
            argv=["python", "-m", "cloudmus_backend_local"], protocol_version="1.0", manifest_path=None,
        ),
        BackendManifest(
            id="flaky", name="Flaky",
            argv=["python", "-c", "import sys; sys.exit(1)"], protocol_version="1.0", manifest_path=None,
        ),
    ]

    app = PlayerApp(manifests=manifests)
    notifications = []
    orig_notify = app._on_backend_notification

    def spy(source_id, method, params):
        notifications.append((source_id, method))
        return orig_notify(source_id, method, params)

    app._on_backend_notification = spy
    app.source_manager._on_notification = spy

    async with app.run_test() as pilot:
        for _ in range(50):
            if any(sid == "flaky" and m == BACKEND_UNAVAILABLE for sid, m in notifications):
                break
            await pilot.pause(0.1)

        assert any(sid == "flaky" and m == BACKEND_UNAVAILABLE for sid, m in notifications)
        assert "flaky" not in app.source_manager.clients
        # local-folder must be entirely unaffected by the other backend's crash-loop.
        result = await app.source_manager.clients["local-folder"].request("catalog.listPlaylists", {})
        assert len(result["playlists"]) == 1

    await app.source_manager.shutdown_all()
    if app.playback_engine is not None:
        app.playback_engine.shutdown()


@pytest.mark.asyncio
async def test_quit_leaves_no_orphaned_backend_processes(tmp_path):
    (tmp_path / "AlbumA").mkdir()
    (tmp_path / "AlbumA" / "track1.mp3").write_bytes(b"")

    app = PlayerApp(manifests=_manifests(tmp_path))
    pids = []
    async with app.run_test() as pilot:
        for _ in range(30):
            if len(app.source_manager.clients) == 2:
                break
            await pilot.pause(0.1)
        pids = [c._proc.pid for c in app.source_manager.clients.values()]

        await app.action_quit()

    for pid in pids:
        assert not _pid_alive(pid), f"backend pid {pid} still alive after quit"
