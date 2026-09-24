"""Backend discovery via manifest files (docs/protocol.md, architecture-plan.md §6).

Production: scans ~/.config/cloudmus/backends.d/*.json for manifests dropped
there by installed backends. Dev mode (CLOUDMUS_DEV_BACKENDS=1) additionally
scans repo-relative backends/*/manifest.json, so running straight out of a
checkout works without installing anything into that directory first.
"""
from __future__ import annotations

import json
import os
from dataclasses import dataclass
from pathlib import Path

MANIFEST_DIR = Path.home() / ".config" / "cloudmus" / "backends.d"


@dataclass
class BackendManifest:
    id: str
    name: str
    argv: list[str]
    protocol_version: str
    manifest_path: Path

    @classmethod
    def from_json(cls, path: Path) -> "BackendManifest":
        data = json.loads(path.read_text())
        return cls(
            id=data["id"],
            name=data.get("name", data["id"]),
            argv=data["argv"],
            protocol_version=data.get("protocolVersion", "1.0"),
            manifest_path=path,
        )


def _repo_root() -> Path:
    # fronts/tui/cloudmus_tui/discovery.py -> repo root is 3 parents up
    return Path(__file__).resolve().parents[3]


def discover_manifests() -> list[BackendManifest]:
    manifests: dict[str, BackendManifest] = {}

    if MANIFEST_DIR.is_dir():
        for path in sorted(MANIFEST_DIR.glob("*.json")):
            try:
                manifest = BackendManifest.from_json(path)
            except Exception:
                continue
            manifests[manifest.id] = manifest

    if os.environ.get("CLOUDMUS_DEV_BACKENDS", "").lower() in ("1", "true", "yes"):
        backends_dir = _repo_root() / "backends"
        if backends_dir.is_dir():
            for path in sorted(backends_dir.glob("*/manifest.json")):
                try:
                    manifest = BackendManifest.from_json(path)
                except Exception:
                    continue
                manifests.setdefault(manifest.id, manifest)

    return list(manifests.values())
