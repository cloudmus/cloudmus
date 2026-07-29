import logging
import os
from pathlib import Path

LOG_FILE = Path.cwd() / "ym-player-debug.log"


def debug_requested() -> bool:
    return os.environ.get("YM_PLAYER_DEBUG", "").lower() in ("1", "true", "yes")


def setup_debug_logging() -> None:
    logging.basicConfig(
        filename=LOG_FILE,
        filemode="a",
        level=logging.DEBUG,
        format="%(asctime)s %(threadName)s %(name)s: %(message)s",
    )
