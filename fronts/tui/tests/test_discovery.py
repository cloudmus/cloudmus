import json
from pathlib import Path

from cloudmus_tui import discovery


def test_discover_manifests_dev_mode(monkeypatch, tmp_path):
    monkeypatch.setenv("CLOUDMUS_DEV_BACKENDS", "1")
    monkeypatch.setattr(discovery, "MANIFEST_DIR", tmp_path / "nonexistent")
    manifests = discovery.discover_manifests()
    ids = {m.id for m in manifests}
    assert "local-folder" in ids
    assert "yandex-music" in ids
    # A program path in a manifest is relative to its own directory.
    spotify = next(m for m in manifests if m.id == "spotify")
    assert spotify.argv[0] == str(spotify.manifest_path.parent / "target/release/cloudmus-backend-spotify")
    local = next(m for m in manifests if m.id == "local-folder")
    assert local.argv[0] == "python3"


def test_discover_manifests_reads_production_dir(monkeypatch, tmp_path):
    monkeypatch.delenv("CLOUDMUS_DEV_BACKENDS", raising=False)
    manifest_dir = tmp_path / "backends.d"
    manifest_dir.mkdir()
    (manifest_dir / "custom.json").write_text(
        json.dumps({"id": "custom", "name": "Custom", "argv": ["custom-backend"], "protocolVersion": "1.0"})
    )
    monkeypatch.setattr(discovery, "MANIFEST_DIR", manifest_dir)
    manifests = discovery.discover_manifests()
    assert len(manifests) == 1
    assert manifests[0].id == "custom"
    assert manifests[0].argv == ["custom-backend"]


def test_malformed_manifest_is_skipped(monkeypatch, tmp_path):
    monkeypatch.delenv("CLOUDMUS_DEV_BACKENDS", raising=False)
    manifest_dir = tmp_path / "backends.d"
    manifest_dir.mkdir()
    (manifest_dir / "broken.json").write_text("not json")
    monkeypatch.setattr(discovery, "MANIFEST_DIR", manifest_dir)
    assert discovery.discover_manifests() == []
