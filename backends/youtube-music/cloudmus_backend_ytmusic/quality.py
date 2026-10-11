"""Stream quality: which of a video's audio formats to use.

YouTube offers each track as Opus at about 130/70/50 kbps and AAC at about
130/48 kbps (256 kbps is for Premium accounts only, and yt-dlp isn't
given the account's cookies here, so it never shows up). The
"streamQuality" setting (see config.settings_store()) caps the bitrate —
for listening, and for the tracks the front saves from the stream.
"""
from __future__ import annotations

from typing import Any, Sequence

BEST = "best"

# (setting value, label, bitrate cap in kbps — None: no cap). The caps sit
# above the nominal bitrate in the label: the real average of a "70 kbps"
# stream is anywhere between 60 and 75.
LEVELS: list[tuple[str, str, int | None]] = [
    (BEST, "Best available (up to 130 kbps, 256 with Premium)", None),
    ("medium", "Up to 70 kbps", 80),
    ("low", "Up to 50 kbps", 56),
]

OPTIONS = [(value, label) for value, label, _ in LEVELS]
_CAPS = {value: cap for value, _, cap in LEVELS}

# Formats this close in bitrate are the same tier in two codecs, and there
# Opus sounds better than AAC — even when its measured average is the
# lower one by a fraction of a kbps.
_SAME_TIER = 0.85


def _bitrate(f: dict) -> float:
    return f.get("abr") or f.get("tbr") or 0


def _preference(f: dict) -> tuple:
    return (
        # The original audio track over a dubbed one.
        f.get("language_preference") or 0,
        # "-drc" is the same stream with its dynamic range compressed.
        not str(f.get("format_id", "")).endswith("-drc"),
        str(f.get("acodec", "")).startswith("opus"),
        _bitrate(f),
    )


def pick(formats: Sequence[dict], quality: str) -> dict:
    """The best format within the quality's cap — or, if every format is
    above it, the lowest one there is."""
    cap = _CAPS.get(quality)
    within = [f for f in formats if cap is None or _bitrate(f) <= cap]
    if not within:
        return min(formats, key=_bitrate)
    top = max(_bitrate(f) for f in within)
    return max((f for f in within if _bitrate(f) >= top * _SAME_TIER), key=_preference)
