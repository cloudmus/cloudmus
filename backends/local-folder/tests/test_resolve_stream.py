from pathlib import Path

import pytest

from cloudmus_backend_local import config, server
from rpc_common.server import BackendError


def test_resolve_stream_returns_descriptor_without_starting_playback(tmp_path: Path, monkeypatch):
    track = tmp_path / "song.mp3"
    track.write_bytes(b"audio")
    monkeypatch.setattr(config, "get_music_dir", lambda: tmp_path)
    backend = server.build_server()

    result = backend._handlers["playback.resolveStream"]({"trackId": "song.mp3"}, 42)

    assert result["stream"]["url"] == track.as_uri()
    assert result["stream"]["mimeType"] == "audio/mpeg"
    with pytest.raises(BackendError):
        backend._handlers["playback.resolveStream"]({"trackId": "missing.mp3"}, 43)
