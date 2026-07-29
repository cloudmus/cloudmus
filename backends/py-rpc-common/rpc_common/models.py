"""Shared data shapes from docs/protocol.md §6.

Each dataclass mirrors the wire shape. `to_dict()` drops unset optional
fields (None) rather than emitting them as JSON null; `from_dict()` is
lenient and ignores unknown keys, since these are also used to parse
responses from backends that may be running an older/newer minor version.
"""
from __future__ import annotations

from dataclasses import asdict, dataclass, field
from typing import Any


def _drop_none(d: dict[str, Any]) -> dict[str, Any]:
    return {k: v for k, v in d.items() if v is not None}


@dataclass
class Artist:
    id: str
    name: str

    def to_dict(self) -> dict[str, Any]:
        return _drop_none(asdict(self))

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "Artist":
        return cls(id=d["id"], name=d["name"])


@dataclass
class Album:
    id: str
    title: str
    coverUrl: str | None = None

    def to_dict(self) -> dict[str, Any]:
        return _drop_none(asdict(self))

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "Album":
        return cls(id=d["id"], title=d["title"], coverUrl=d.get("coverUrl"))


@dataclass
class Track:
    id: str
    title: str
    artists: list[Artist]
    durationMs: int
    album: Album | None = None
    coverUrl: str | None = None
    liked: bool | None = None
    explicit: bool | None = None

    def to_dict(self) -> dict[str, Any]:
        d: dict[str, Any] = {
            "id": self.id,
            "title": self.title,
            "artists": [a.to_dict() for a in self.artists],
            "durationMs": self.durationMs,
        }
        if self.album is not None:
            d["album"] = self.album.to_dict()
        d["coverUrl"] = self.coverUrl
        d["liked"] = self.liked
        d["explicit"] = self.explicit
        return _drop_none(d)

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "Track":
        album = d.get("album")
        return cls(
            id=d["id"],
            title=d["title"],
            artists=[Artist.from_dict(a) for a in d.get("artists", [])],
            durationMs=d["durationMs"],
            album=Album.from_dict(album) if album is not None else None,
            coverUrl=d.get("coverUrl"),
            liked=d.get("liked"),
            explicit=d.get("explicit"),
        )


@dataclass
class Playlist:
    id: str
    title: str
    trackCount: int
    kind: str  # "playlist" | "liked" | "radioStation"
    description: str | None = None
    coverUrl: str | None = None

    def to_dict(self) -> dict[str, Any]:
        return _drop_none(asdict(self))

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "Playlist":
        return cls(
            id=d["id"],
            title=d["title"],
            trackCount=d["trackCount"],
            kind=d["kind"],
            description=d.get("description"),
            coverUrl=d.get("coverUrl"),
        )


@dataclass
class PlaybackState:
    state: str  # "idle" | "playing" | "paused" | "buffering" | "stopped"
    trackId: str | None = None
    positionMs: int | None = None
    durationMs: int | None = None
    liked: bool | None = None

    def to_dict(self) -> dict[str, Any]:
        return _drop_none(asdict(self))

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "PlaybackState":
        return cls(
            state=d["state"],
            trackId=d.get("trackId"),
            positionMs=d.get("positionMs"),
            durationMs=d.get("durationMs"),
            liked=d.get("liked"),
        )


@dataclass
class StreamDescriptor:
    url: str
    mimeType: str
    kind: str = "url"
    headers: dict[str, str] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        d = _drop_none(asdict(self))
        if not d.get("headers"):
            d.pop("headers", None)
        return d

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "StreamDescriptor":
        return cls(
            url=d["url"],
            mimeType=d["mimeType"],
            kind=d.get("kind", "url"),
            headers=d.get("headers", {}),
        )
