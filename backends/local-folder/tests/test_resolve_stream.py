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


def test_stream_reports_codec_and_bitrate_of_a_webm_file(tmp_path: Path, monkeypatch):
    track = tmp_path / "song.webm"
    track.write_bytes((Path(__file__).parent / "data" / "sample.webm").read_bytes())
    monkeypatch.setattr(config, "get_music_dir", lambda: tmp_path)
    backend = server.build_server()

    stream = backend._handlers["playback.resolveStream"]({"trackId": "song.webm"}, 1)["stream"]

    assert stream["codec"] == "opus"
    assert stream["bitrateKbps"] > 0


def test_stream_without_readable_tags_has_no_codec_or_bitrate(tmp_path: Path, monkeypatch):
    (tmp_path / "broken.flac").write_bytes(b"not audio")
    monkeypatch.setattr(config, "get_music_dir", lambda: tmp_path)
    backend = server.build_server()

    stream = backend._handlers["playback.resolveStream"]({"trackId": "broken.flac"}, 1)["stream"]

    assert "bitrateKbps" not in stream
