from pathlib import Path

import pytest

from cloudmus_tui.discovery import BackendManifest


@pytest.fixture(scope="session")
def local_folder_argv() -> list[str]:
    root = Path(__file__).resolve().parents[3]
    return BackendManifest.from_json(root / "backends/local-folder/manifest.json").argv
