"""Config layout for this backend, split across two files:

- config.json: this backend's settings, under "settings" (settings_store()).
- browser_headers.json: the signed-in session (see auth.py). Written by
  ytmusicapi.auth.browser.setup_browser() and read by YTMusic(auth=...), so
  this module never parses its contents.
"""
from pathlib import Path

from rpc_common.platform_paths import config_home
from rpc_common.settings import Field, Group, SettingsStore

from . import quality

CONFIG_DIR = config_home() / "cloudmus" / "backends" / "youtube-music"
SETTINGS_FILE = CONFIG_DIR / "config.json"
BROWSER_HEADERS_FILE = CONFIG_DIR / "browser_headers.json"


def browser_headers_path() -> Path:
    """Ensures the config directory exists and returns browser_headers.json's path."""
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    return BROWSER_HEADERS_FILE


def has_browser_auth() -> bool:
    return BROWSER_HEADERS_FILE.exists()


def clear_browser_auth() -> None:
    BROWSER_HEADERS_FILE.unlink(missing_ok=True)


def settings_store() -> SettingsStore:
    """This backend's settings (docs/protocol.md §7.7)."""
    return SettingsStore(
        SETTINGS_FILE,
        groups=[Group("playback", "Playback")],
        fields=[
            # The front saves the stream it plays (docs/protocol.md §7.5),
            # so one setting covers listening and saving.
            Field.enum(
                "streamQuality",
                "Quality",
                default=quality.BEST,
                options=quality.OPTIONS,
                group="playback",
                description="Applies from the next track, and to the tracks you save.",
            ),
        ],
    )
