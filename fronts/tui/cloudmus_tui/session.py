"""The playlist and track to offer when the terminal front starts again."""
from __future__ import annotations

import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any


def default_path() -> Path:
    return Path.home() / ".config" / "cloudmus" / "fronts" / "tui" / "session.json"


@dataclass(frozen=True)
class Session:
    source_id: str
    playlist_id: str
    track_id: str | None = None
    track_index: int | None = None
    playlist_title: str = "playlist"
    kind: str = "playlist"
    tracks: list[dict[str, Any]] | None = None


def load(path: Path) -> Session | None:
    try:
        data = json.loads(path.read_text())
        source_id = data["sourceId"]
        playlist_id = data["playlistId"]
        track_id = data.get("trackId")
        track_index = data.get("trackIndex")
        playlist_title = data.get("playlistTitle", "playlist")
        kind = data.get("kind", "playlist")
        tracks = data.get("tracks", [])
        if not isinstance(source_id, str) or not source_id:
            return None
        if not isinstance(playlist_id, str) or not playlist_id:
            return None
        if track_id is not None and not isinstance(track_id, str):
            return None
        if track_index is not None and (type(track_index) is not int or track_index < 0):
            return None
        if not isinstance(playlist_title, str) or not isinstance(kind, str):
            return None
        if not isinstance(tracks, list) or any(
            not isinstance(track, dict) or not isinstance(track.get("id"), str) for track in tracks
        ):
            return None
        return Session(source_id, playlist_id, track_id, track_index, playlist_title, kind, tracks)
    except (OSError, ValueError, TypeError, KeyError):
        return None


def save(path: Path, session: Session) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    try:
        with temporary.open("w") as file:
            os.fchmod(file.fileno(), 0o600)
            json.dump(
                {
                    "sourceId": session.source_id,
                    "playlistId": session.playlist_id,
                    "trackId": session.track_id,
                    "trackIndex": session.track_index,
                    "playlistTitle": session.playlist_title,
                    "kind": session.kind,
                    "tracks": session.tracks or [],
                },
                file,
            )
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)
