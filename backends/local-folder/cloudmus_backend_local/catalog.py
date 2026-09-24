from __future__ import annotations

from pathlib import Path

from rpc_common.generated.models import Playlist

from . import scanner


def list_playlists(root: Path) -> dict:
    playlists = [
        Playlist(id=info.id, title=info.title, trackCount=len(info.files), kind="playlist").to_dict()
        for info in scanner.scan(root)
    ]
    return {"playlists": playlists}


def list_tracks(root: Path, playlist_id: str) -> dict:
    playlist = scanner.find_playlist(root, playlist_id)
    if playlist is None:
        raise KeyError(playlist_id)
    tracks = [scanner.read_track(path, root).to_dict() for path in playlist.files]
    return {"tracks": tracks}
