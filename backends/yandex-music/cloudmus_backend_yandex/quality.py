"""Stream/download quality: which of a track's download infos to use.

Yandex offers each track in a few codec/bitrate variants (typically mp3 at
320/192/128 kbps, sometimes aac). The "streamQuality"/"downloadQuality"
settings (see config.settings_store()) cap the bitrate; mp3 is preferred as
the most widely playable and taggable.
"""
from __future__ import annotations

from typing import Any, Sequence

BEST = "best"

# (setting value, label, bitrate cap in kbps — None: no cap)
LEVELS: list[tuple[str, str, int | None]] = [
    (BEST, "Best available", None),
    ("high", "Up to 192 kbps", 192),
    ("low", "Up to 128 kbps", 128),
]

OPTIONS = [(value, label) for value, label, _ in LEVELS]
_CAPS = {value: cap for value, _, cap in LEVELS}


def pick(infos: Sequence[Any], quality: str) -> Any:
    """The highest-bitrate variant within the quality's cap — or, if every
    variant is above it, the lowest one there is."""
    candidates = [i for i in infos if i.codec == "mp3"] or list(infos)
    cap = _CAPS.get(quality)
    within = [i for i in candidates if cap is None or i.bitrate_in_kbps <= cap]
    if within:
        return max(within, key=lambda i: i.bitrate_in_kbps)
    return min(candidates, key=lambda i: i.bitrate_in_kbps)
