import json
from pathlib import Path

from rpc_common.platform_paths import config_home
from rpc_common.settings import Field, Group, SettingsStore

from . import quality

CONFIG_DIR = config_home() / "cloudmus" / "backends" / "yandex-music"
CONFIG_FILE = CONFIG_DIR / "config.json"


def load() -> dict:
    if not CONFIG_FILE.exists():
        return {}
    return json.loads(CONFIG_FILE.read_text(encoding="utf-8"))


def save(data: dict) -> None:
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    CONFIG_FILE.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
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
    data.pop("pendingAuth", None)
    save(data)


def get_pending_auth() -> dict | None:
    """The device code being waited on, saved so that a backend killed while
    the user confirms it in a browser (Android unloads the whole app on a
    phone short of memory) picks it up again on its next start instead of
    asking for a new one."""
    pending = load().get("pendingAuth")
    return pending if isinstance(pending, dict) else None


def set_pending_auth(pending: dict) -> None:
    data = load()
    data["pendingAuth"] = pending
    save(data)


def clear_pending_auth() -> None:
    data = load()
    if data.pop("pendingAuth", None) is not None:
        save(data)


def settings_store() -> SettingsStore:
    """This backend's settings (docs/protocol.md §7.7), in the same
    config.json as the token — load()/save() above keep the "settings"
    section intact, and SettingsStore keeps "token"."""
    return SettingsStore(
        CONFIG_FILE,
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
