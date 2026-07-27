import json
from pathlib import Path

CONFIG_DIR = Path.home() / ".config" / "ym-player"
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
