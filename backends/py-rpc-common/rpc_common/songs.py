"""Telling versions of one song apart from different songs, for a radio
that keeps a song from coming again (docs/protocol.md §7.1, recentTracks).

Services carry the same song under several ids: the original and a sped
up or slowed upload, a remaster, YouTube's music video and its audio
track. `song_key()` gives them all the same key — artist and title with
such a version's mark taken off — while a remix, a live recording or a
cover, which sound different, keep their own."""
from __future__ import annotations

import re

# A title's tail made of these words alone (and numbers: "2011 Remaster")
# marks a version of the song, not a different one.
VERSION_WORDS = frozenset(
    """
    sped speed spedup up slowed slow reverb reverbed nightcore super ultra
    bass boosted extended original mix radio edit remaster remastered
    official audio video music lyric lyrics visualizer visualiser clip mv
    hd hq 4k version ver tiktok and
    """.split()
)

_DASHES = (" - ", " – ", " — ", " | ")
_FEATURING = re.compile(r"^(feat\.?|ft\.?|featuring|with)\s")
_TRAILING_BRACKETS = re.compile(r"\s*[(\[]([^()\[\]]*)[)\]]$")
_TRAILING_FEATURING = re.compile(r"\s+(feat\.?|ft\.?|featuring)\s.*$")
_WORDS = re.compile(r"[^\W_]+")


def _is_version_mark(text: str) -> bool:
    if _FEATURING.match(text):
        return True
    words = _WORDS.findall(text)
    return bool(words) and all(w in VERSION_WORDS or w.isdigit() for w in words)


def _strip_version(title: str) -> str:
    while True:
        stripped = _TRAILING_FEATURING.sub("", title)
        m = _TRAILING_BRACKETS.search(stripped)
        if m and _is_version_mark(m.group(1)):
            stripped = stripped[: m.start()]
        else:
            for dash in _DASHES:
                head, sep, tail = stripped.rpartition(dash)
                if sep and head and _is_version_mark(tail):
                    stripped = head
                    break
        if stripped == title:
            return title
        title = stripped


def song_key(title: str, artists: list[str]) -> str:
    """The same for every version of a song: its first artist and its
    title, case and spacing aside, without a version's mark ("Sped Up",
    "Slowed + Reverb", "Official Video", "feat. ...")."""
    artist = " ".join((artists[0] if artists else "").casefold().split())
    title = " ".join(title.casefold().split())
    # A music video's title is often "Artist - Song".
    for dash in _DASHES:
        if artist and title.startswith(artist + dash):
            title = title[len(artist + dash) :]
            break
    return f"{artist}\x1f{_strip_version(title) or title}"
