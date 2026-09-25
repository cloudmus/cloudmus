import json
import os
from pathlib import Path

from rpc_common.settings import Field, Group, SettingsStore

CONFIG_DIR = Path.home() / ".config" / "cloudmus" / "backends" / "local-folder"
CONFIG_FILE = CONFIG_DIR / "config.json"

MUSIC_DIR_ENV = "CLOUDMUS_LOCAL_FOLDER_MUSIC_DIR"


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


def default_music_dir() -> Path:
    """The XDG music folder (e.g. ~/Музыка on a Russian-locale desktop),
    else ~/Music."""
    return _xdg_music_dir() or Path.home() / "Music"


def settings_store() -> SettingsStore:
    """This backend's settings (docs/protocol.md §7.7), kept in
    config.json's "settings" section. Built per call rather than once, so
    the default follows the XDG music folder if it changes."""
    return SettingsStore(
        CONFIG_FILE,
        groups=[Group("library", "Library")],
        fields=[
            Field.path(
                "musicDir",
                "Music folder",
                default=str(default_music_dir()),
                kind="directory",
                group="library",
                description=(
                    "Scanned for music: each folder in it becomes a playlist. "
                    f"The {MUSIC_DIR_ENV} environment variable, if set, overrides it."
                ),
            ),
        ],
    )


def _migrate_legacy_music_dir() -> None:
    """Before settings.*, "musicDir" sat at config.json's top level, edited
    by hand. Moved into the "settings" section as is — not through
    SettingsStore.update(), which would refuse a folder that happens to be
    missing right now."""
    try:
        data = json.loads(CONFIG_FILE.read_text())
    except (OSError, ValueError):
        return
    legacy = data.pop("musicDir", None)
    if not legacy:
        return
    settings = data.setdefault("settings", {})
    settings.setdefault("musicDir", legacy)
    CONFIG_FILE.write_text(json.dumps(data, indent=2, ensure_ascii=False))


def get_music_dir() -> Path:
    """Resolves the root directory to scan for music.

    Priority: CLOUDMUS_LOCAL_FOLDER_MUSIC_DIR env var (dev/testing
    convenience) > the "musicDir" setting > the XDG music folder > ~/Music.
    """
    env_dir = os.environ.get(MUSIC_DIR_ENV)
    if env_dir:
        return Path(env_dir).expanduser()

    _migrate_legacy_music_dir()
    return Path(settings_store().get("musicDir")).expanduser()
