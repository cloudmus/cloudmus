"""catalog.startRadio / feedback.trackStarted/.trackFinished/.skip — a
radio queue built on ytmusicapi's get_watch_playlist(), seeded either from
a single track (the track list's "Start Radio from This Track") or from one
of the account's personal mixes (catalog._find_mixes(), surfaced as
kind: radioStation playlists — "My Supermix" is this backend's
My-Wave-equivalent).

Shaped after cloudmus_backend_yandex/radio.py, since the front owns the
queue (docs/protocol.md §7.1) and the backend only reacts to feedback.*: a
skip means "not this" and replaces the front's unplayed tail with a fresh
batch (radio/tracksAdded with replaceUpcoming); a track played to the end
leaves what's queued alone and only tops it up once it runs low. A fresh
batch is anchored on the last track played to the end, so the radio
follows what was actually listened to.

For a mix, the first batch is the mix itself (get_watch_playlist(
playlistId=mix) — the same list YouTube Music plays), and later batches
are get_watch_playlist(videoId=anchor, playlistId=mix, radio=True): a
different ~25 tracks on every call, still largely from the mix (measured
live: ~15 of 25). For a track seed it's the track's radio
(get_watch_playlist(videoId=..., radio=True)) throughout. _is_video_id()
below tells the two seed kinds apart.

YouTube has no radio feedback endpoint the way Yandex's rotor has; the one
signal ytmusicapi can send is a play in the account's history
(add_history_item(), which feeds its recommendations and mixes). A track
is reported once played to the end, or skipped after LISTEN_MS — a quick
skip is not a listen.

get_watch_playlist()'s track dict shape (ytmusicapi/mixins/watch.py)
matches catalog.to_track()'s expectations (videoId/title/artists/album/
likeStatus) except for two field names: `length` (a "3:07"-style string)
instead of `duration_seconds`, and `thumbnail` (singular) instead of
`thumbnails`. _normalize_watch_track() converts just those two fields so
the existing catalog.to_track() can be reused unchanged.
"""
from __future__ import annotations

import asyncio
import logging
from typing import Any, Awaitable, Callable

from ytmusicapi import YTMusic

from rpc_common.generated.methods import emit_radio_tracks_added
from rpc_common.generated.models import TracksAddedParams

from . import catalog

logger = logging.getLogger(__name__)

NotifyFn = Callable[[str, dict[str, Any]], Awaitable[None]]

# After a track plays to the end, the queue is only topped up (appended to)
# once fewer than this many served tracks are still waiting to play.
UPCOMING_LOW_WATER = 3

# A skipped track still counts as listened to past this point — YouTube's
# own threshold for counting a play is about the same.
LISTEN_MS = 30_000


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
        # The mix a mix-seeded session keeps drawing from; None for a
        # track-seeded one.
        self.mix_id: str | None = None
        # The last track played to the end — what the next batch follows.
        self.last_good: str | None = None
        # Started tracks are never re-served; skipped ones neither — "not
        # this" should stick for the rest of the session.
        self._played: set[str] = set()
        self._skipped: set[str] = set()
        # Tracks served to the front that haven't started yet, in queue
        # order — the station's own part of the front's upcoming queue.
        self._upcoming: list[str] = []
        # Keeps the fire-and-forget history reports alive until they finish.
        self._reports: set[asyncio.Task] = set()

    async def start(self, seed: str | None) -> dict:
        if not seed:
            raise ValueError("YouTube Music radio needs a seed trackId or playlistId")
        self.station_id = seed
        self.mix_id = None if _is_video_id(seed) else seed
        self.last_good = None
        self._played = set()
        self._skipped = set()

        def fetch() -> dict:
            if self.mix_id is None:
                return self.client.get_watch_playlist(videoId=seed, radio=True)
            return self.client.get_watch_playlist(playlistId=seed)

        result = await asyncio.to_thread(fetch)
        tracks = self._fresh(result)
        self._upcoming = [t["videoId"] for t in tracks]
        return {
            "stationId": self.station_id,
            "initialTracks": [catalog.to_track(t).to_dict() for t in tracks],
        }

    def _fresh(self, result: dict) -> list[dict]:
        # Normalized, once each, minus anything played or skipped — a
        # track's radio re-includes the track itself as its first entry.
        tracks, seen = [], set()
        for t in result.get("tracks") or []:
            vid = t.get("videoId")
            if not vid or vid in seen or vid in self._played or vid in self._skipped:
                continue
            seen.add(vid)
            tracks.append(_normalize_watch_track(t))
        return tracks

    def _fetch_batch(self) -> dict:
        anchor = self.last_good
        if self.mix_id is not None:
            if anchor is None:
                return self.client.get_watch_playlist(playlistId=self.mix_id, radio=True)
            return self.client.get_watch_playlist(videoId=anchor, playlistId=self.mix_id, radio=True)
        return self.client.get_watch_playlist(videoId=anchor or self.station_id, radio=True)

    async def _top_up(self, *, replace: bool) -> None:
        if self.station_id is None:
            return
        result = await asyncio.to_thread(self._fetch_batch)
        tracks = self._fresh(result)
        if replace:
            # The new batch supersedes everything still queued.
            new = tracks
            self._upcoming = [t["videoId"] for t in new]
        else:
            # Appended after what's already queued — skip anything in it.
            new = [t for t in tracks if t["videoId"] not in self._upcoming]
            self._upcoming += [t["videoId"] for t in new]
        if new:
            await emit_radio_tracks_added(
                self._notify,
                TracksAddedParams(
                    stationId=self.station_id,
                    tracks=[catalog.to_track(t) for t in new],
                    replaceUpcoming=replace or None,
                ),
            )

    def _report_listen(self, video_id: str) -> None:
        # In the background: the feedback reply (and with it the next top-up)
        # shouldn't wait on two more round trips to YouTube.
        def send() -> None:
            try:
                self.client.add_history_item(self.client.get_song(video_id))
            except Exception as e:
                logger.debug("reporting %s to the history failed: %s", video_id, e)

        task = asyncio.create_task(asyncio.to_thread(send))
        self._reports.add(task)
        task.add_done_callback(self._reports.discard)

    async def track_started(self, track_id: str) -> None:
        self._played.add(track_id)
        # Everything queued up to it has been played or jumped past.
        if track_id in self._upcoming:
            del self._upcoming[: self._upcoming.index(track_id) + 1]

    async def track_finished(self, track_id: str, played_ms: int) -> None:
        self._report_listen(track_id)
        self.last_good = track_id
        # Played to the end: nothing to correct, keep what's queued.
        if len(self._upcoming) < UPCOMING_LOW_WATER:
            await self._top_up(replace=False)

    async def skip(self, track_id: str, played_ms: int) -> None:
        self._skipped.add(track_id)
        if played_ms >= LISTEN_MS:
            self._report_listen(track_id)
        # "Not this": a fresh batch replaces what's queued.
        await self._top_up(replace=True)
