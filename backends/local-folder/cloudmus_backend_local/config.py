import json
import os
from pathlib import Path

CONFIG_DIR = Path.home() / ".config" / "cloudmus" / "backends" / "local-folder"
CONFIG_FILE = CONFIG_DIR / "config.json"


def _xdg_music_dir() -> Path | None:
    """XDG_MUSIC_DIR from user-dirs.dirs (what `xdg-user-dir MUSIC` prints),
    parsed here rather than by running that tool, which isn't installed
    everywhere. The same folder Qt's QStandardPaths::MusicLocation gives the
    Qt front as its default download folder, so downloads land where this
    backend looks. None when unset, or set to $HOME itself — the spec's way
    of disabling a user dir."""
    config_home = Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config")
    try:
        lines = (config_home / "user-dirs.dirs").read_text().splitlines()
    except OSError:
        return None
    for line in lines:
        key, sep, value = line.strip().partition("=")
        if key != "XDG_MUSIC_DIR" or not sep:
            continue
        value = value.strip().strip('"')
        if value.startswith("$HOME"):
            value = str(Path.home()) + value[len("$HOME"):]
        path = Path(value)
        if path.is_absolute() and path != Path.home():
            return path
    return None


def get_music_dir() -> Path:
    """Resolves the root directory to scan for music.

    Priority: CLOUDMUS_LOCAL_FOLDER_MUSIC_DIR env var (dev/testing
    convenience) > "musicDir" in config.json > the XDG music folder
    (e.g. ~/Музыка on a Russian-locale desktop) > ~/Music.
    """
    env_dir = os.environ.get("CLOUDMUS_LOCAL_FOLDER_MUSIC_DIR")
    if env_dir:
        return Path(env_dir).expanduser()

    if CONFIG_FILE.exists():
        data = json.loads(CONFIG_FILE.read_text())
        music_dir = data.get("musicDir")
        if music_dir:
            return Path(music_dir).expanduser()

    return _xdg_music_dir() or Path.home() / "Music"


def set_music_dir(path: str) -> None:
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    CONFIG_FILE.write_text(json.dumps({"musicDir": path}, indent=2, ensure_ascii=False))
