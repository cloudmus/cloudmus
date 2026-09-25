import json
from pathlib import Path

from rpc_common.settings import Field, Group, SettingsStore

from . import quality

CONFIG_DIR = Path.home() / ".config" / "cloudmus" / "backends" / "yandex-music"
CONFIG_FILE = CONFIG_DIR / "config.json"


def load() -> dict:
    if not CONFIG_FILE.exists():
        return {}
    return json.loads(CONFIG_FILE.read_text())


def save(data: dict) -> None:
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    CONFIG_FILE.write_text(json.dumps(data, indent=2, ensure_ascii=False))
    CONFIG_FILE.chmod(0o600)


def get_token() -> str | None:
    return load().get("token")


def set_token(token: str) -> None:
    data = load()
    data["token"] = token
    save(data)


def clear_token() -> None:
    data = load()
    data.pop("token", None)
    save(data)


def settings_store() -> SettingsStore:
    """This backend's settings (docs/protocol.md §7.7), in the same
    config.json as the token — load()/save() above keep the "settings"
    section intact, and SettingsStore keeps "token"."""
    return SettingsStore(
        CONFIG_FILE,
        groups=[Group("playback", "Playback"), Group("downloads", "Downloads")],
        fields=[
            Field.enum(
                "streamQuality",
                "Stream quality",
                default=quality.BEST,
                options=quality.OPTIONS,
                group="playback",
                description="Applies from the next track.",
            ),
            Field.enum(
                "downloadQuality",
                "Download quality",
                default=quality.BEST,
                options=quality.OPTIONS,
                group="downloads",
            ),
        ],
    )
