import json
import os
from pathlib import Path

from rpc_common.platform_paths import config_home
from rpc_common.settings import Field, Group, SettingsStore

CONFIG_DIR = config_home() / "cloudmus" / "backends" / "local-folder"
CONFIG_FILE = CONFIG_DIR / "config.json"

MUSIC_DIR_ENV = "CLOUDMUS_LOCAL_FOLDER_MUSIC_DIR"


def _xdg_music_dir() -> Path | None:
    """XDG_MUSIC_DIR from user-dirs.dirs (what `xdg-user-dir MUSIC` prints),
    parsed here rather than by running that tool, which isn't installed
    everywhere. The same folder Qt's QStandardPaths::MusicLocation gives the
    Qt front as its default download folder, so downloads land where this
    backend looks. None when unset, or set to $HOME itself — the spec's way
    of disabling a user dir."""
    xdg_config_home = Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config")
    try:
        lines = (xdg_config_home / "user-dirs.dirs").read_text(encoding="utf-8").splitlines()
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
    if os.name == "nt":
        return _windows_music_dir()
    return _xdg_music_dir() or Path.home() / "Music"


def _windows_music_dir() -> Path:
    """Ask Windows for the redirected Music known folder, if configured."""
    import ctypes
    import uuid
    folder_id = uuid.UUID("4bd8d571-6d19-48d3-be97-422220080e43")
    guid = (ctypes.c_byte * 16).from_buffer_copy(folder_id.bytes_le)
    path = ctypes.c_wchar_p()
    shell32 = ctypes.windll.shell32
    shell32.SHGetKnownFolderPath.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p,
                                             ctypes.POINTER(ctypes.c_wchar_p)]
    shell32.SHGetKnownFolderPath.restype = ctypes.c_long
    ctypes.windll.ole32.CoTaskMemFree.argtypes = [ctypes.c_void_p]
    if shell32.SHGetKnownFolderPath(ctypes.byref(guid), 0, None, ctypes.byref(path)) == 0:
        try:
            return Path(path.value)
        finally:
            ctypes.windll.ole32.CoTaskMemFree(path)
    return Path.home() / "Music"


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
        data = json.loads(CONFIG_FILE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return
    legacy = data.pop("musicDir", None)
    if not legacy:
        return
    settings = data.setdefault("settings", {})
    settings.setdefault("musicDir", legacy)
    CONFIG_FILE.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")


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
