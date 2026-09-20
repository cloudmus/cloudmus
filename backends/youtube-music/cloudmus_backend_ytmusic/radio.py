"""catalog.startRadio / feedback.trackStarted/.trackFinished/.skip — a
"radio" queue built on ytmusicapi's get_watch_playlist(radio=True), seeded
either from a single track (the track list's "Start Radio from This
Track") or from "My Supermix" (catalog.py's SUPERMIX_TITLE/
_find_supermix_id(), surfaced as a synthetic kind: radioStation playlist —
this backend's My-Wave-equivalent).

There is no dedicated ytmusicapi method for "give me my personalized
radio" — no get_mixes()-style call, nothing get_home()/
get_library_playlists() document as returning a mix id directly.
"My Supermix" is only discoverable by scanning get_home()'s shelves for a
title match (confirmed live against a real account — see catalog.py), and
get_watch_playlist() itself always needs a concrete seed (a videoId or an
existing playlistId), never a bare "start my radio" request. Once you have
either kind of id, get_watch_playlist(radio=True) accepts it as either
videoId= (an 11-character YouTube video id) or playlistId= (everything
else — Supermix's own id is "RDTM..."-prefixed) — _is_video_id() below
picks the right kwarg.

No adaptive feedback: unlike Yandex's rotor API (batch_id-tied
trackStarted/trackFinished/skip calls that actually influence future
picks — see cloudmus_backend_yandex/radio.py), ytmusicapi has no
radio/mix-specific feedback endpoint. feedback.trackStarted/
.trackFinished/.skip are still implemented here (the protocol requires
accepting them once browse.radio is declared — the front sends them
unconditionally once a radio session is active, with no separate
capability check), but only track_started()'s bookkeeping of the
last-played id has any real effect (it reseeds the next _top_up(), always
by videoId — see RadioSession.last_track_id).

get_watch_playlist()'s track dict shape (ytmusicapi/mixins/watch.py)
matches catalog.to_track()'s expectations (videoId/title/artists/album/
likeStatus) except for two field names: `length` (a "3:07"-style string)
instead of `duration_seconds`, and `thumbnail` (singular) instead of
`thumbnails`. _normalize_watch_track() converts just those two fields so
the existing catalog.to_track() can be reused unchanged.
"""
from __future__ import annotations

import asyncio
from typing import Any, Awaitable, Callable

from ytmusicapi import YTMusic

from rpc_common.generated.methods import emit_radio_tracks_added
from rpc_common.generated.models import TracksAddedParams

from . import catalog

NotifyFn = Callable[[str, dict[str, Any]], Awaitable[None]]


def _parse_length_to_seconds(length: str | None) -> int:
    if not length:
        return 0
    try:
        parts = [int(p) for p in length.split(":")]
    except ValueError:
        return 0
    seconds = 0
    for p in parts:
        seconds = seconds * 60 + p
    return seconds


def _normalize_watch_track(track: dict) -> dict:
    normalized = dict(track)
    normalized["duration_seconds"] = _parse_length_to_seconds(track.get("length"))
    normalized["thumbnails"] = track.get("thumbnail")
    return normalized


def _is_video_id(seed: str) -> bool:
    # YouTube video ids are always exactly 11 base64url characters;
    # playlist/mix ids (PL-/OLA-/RD-/VL-/LM-prefixed, as returned by
    # get_library_playlists()/get_home()/catalog._liked_playlist()) are
    # always longer than that — a cheap, reliable enough discriminator for
    # the two seed shapes start() can receive.
    return len(seed) == 11


class RadioSession:
    def __init__(self, client: YTMusic, notify: NotifyFn):
        self.client = client
        self._notify = notify
        # Fixed for the whole session, echoed in every radio/tracksAdded —
        # PlaybackController::handleTracksAdded (fronts/qt) silently drops
        # a notification whose stationId doesn't match what start()
        # returned, so this must never change once set.
        self.station_id: str | None = None
        # Evolves as the radio plays — reseeds _top_up() from wherever
        # listening actually got to, so the radio progresses instead of
        # repeating the same get_watch_playlist(videoId=<original seed>)
        # batch forever.
        self.last_track_id: str | None = None

    async def start(self, seed: str | None) -> dict:
        if not seed:
            raise ValueError("YouTube Music radio needs a seed trackId or playlistId")
        self.station_id = seed
        # Only a real videoId is a valid _top_up() reseed point (it's
        # passed straight through as videoId= there) — a playlistId seed
        # (Supermix) leaves this None until the front's first
        # feedback.trackStarted call supplies an actual track id;
        # _top_up() already no-ops while it's None.
        self.last_track_id = seed if _is_video_id(seed) else None
        seeded_by_track = _is_video_id(seed)

        def fetch() -> dict:
            if seeded_by_track:
                return self.client.get_watch_playlist(videoId=seed, radio=True)
            return self.client.get_watch_playlist(playlistId=seed, radio=True)

        result = await asyncio.to_thread(fetch)
        tracks = [_normalize_watch_track(t) for t in (result.get("tracks") or []) if t.get("videoId")]
        return {
            "stationId": self.station_id,
            "initialTracks": [catalog.to_track(t).to_dict() for t in tracks],
        }

    async def _top_up(self) -> None:
        if self.last_track_id is None:
            return

        seed = self.last_track_id

        def fetch() -> dict:
            return self.client.get_watch_playlist(videoId=seed, radio=True)

        result = await asyncio.to_thread(fetch)
        raw = [t for t in (result.get("tracks") or []) if t.get("videoId")]
        # get_watch_playlist(videoId=X, radio=True) re-includes X itself
        # as the first result — drop it here or the just-played track
        # would replay immediately (fine in start(), where X is exactly
        # the track meant to play first).
        if raw and raw[0]["videoId"] == seed:
            raw = raw[1:]
        tracks = [_normalize_watch_track(t) for t in raw]
        if tracks:
            await emit_radio_tracks_added(
                self._notify,
                TracksAddedParams(stationId=self.station_id, tracks=[catalog.to_track(t) for t in tracks]),
            )

    async def track_started(self, track_id: str) -> None:
        self.last_track_id = track_id

    async def track_finished(self, track_id: str, played_ms: int) -> None:
        await self._top_up()

    async def skip(self, track_id: str, played_ms: int) -> None:
        await self._top_up()
