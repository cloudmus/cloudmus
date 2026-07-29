import json
import os
from pathlib import Path

CONFIG_DIR = Path.home() / ".config" / "cloudmus" / "backends" / "local-folder"
CONFIG_FILE = CONFIG_DIR / "config.json"


def get_music_dir() -> Path:
    """Resolves the root directory to scan for music.

    Priority: CLOUDMUS_LOCAL_FOLDER_MUSIC_DIR env var (dev/testing
    convenience) > "musicDir" in config.json > ~/Music as a fallback default.
    """
    env_dir = os.environ.get("CLOUDMUS_LOCAL_FOLDER_MUSIC_DIR")
    if env_dir:
        return Path(env_dir).expanduser()

    if CONFIG_FILE.exists():
        data = json.loads(CONFIG_FILE.read_text())
        music_dir = data.get("musicDir")
        if music_dir:
            return Path(music_dir).expanduser()

    return Path.home() / "Music"


def set_music_dir(path: str) -> None:
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    CONFIG_FILE.write_text(json.dumps({"musicDir": path}, indent=2, ensure_ascii=False))
