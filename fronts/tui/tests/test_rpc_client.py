import sys

import pytest

from cloudmus_tui.discovery import BackendManifest
from cloudmus_tui.rpc_client import BackendClient


def _local_folder_manifest(music_dir) -> BackendManifest:
    import os

    os.environ["CLOUDMUS_LOCAL_FOLDER_MUSIC_DIR"] = str(music_dir)
    return BackendManifest(
        id="local-folder",
        name="Local Folder",
        argv=[sys.executable, "-m", "cloudmus_backend_local"],
        protocol_version="1.0",
        manifest_path=None,
    )


@pytest.mark.asyncio
async def test_backend_client_initialize_and_shutdown(tmp_path):
    (tmp_path / "track.mp3").write_bytes(b"")
    manifest = _local_folder_manifest(tmp_path)
    notifications = []
    client = BackendClient(manifest, lambda method, params: notifications.append((method, params)))

    await client.start()
    assert client.available
    assert client.capabilities["playback"]["providesStream"] is True
    assert client.source_info["id"] == "local-folder"

    result = await client.request("catalog.listPlaylists", {})
    assert len(result["playlists"]) == 1

    await client.shutdown()
    assert client.available is False


@pytest.mark.asyncio
async def test_backend_client_play_delivers_stream_ready_notification(tmp_path):
    (tmp_path / "track.mp3").write_bytes(b"")
    manifest = _local_folder_manifest(tmp_path)
    notifications = []
    client = BackendClient(manifest, lambda method, params: notifications.append((method, params)))
    await client.start()

    playlists = await client.request("catalog.listPlaylists", {})
    playlist_id = playlists["playlists"][0]["id"]
    tracks = await client.request("catalog.listTracks", {"playlistId": playlist_id})
    track_id = tracks["tracks"][0]["id"]

    request_id, ack = await client.call("playback.play", {"trackId": track_id})
    assert ack == {"accepted": True}

    for _ in range(20):
        if any(m == "track/streamReady" for m, _ in notifications):
            break
        import asyncio

        await asyncio.sleep(0.05)

    stream_ready = [p for m, p in notifications if m == "track/streamReady"]
    assert len(stream_ready) == 1
    assert stream_ready[0]["requestId"] == request_id
    assert stream_ready[0]["stream"]["url"].startswith("file://")

    await client.shutdown()
