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
import time
from typing import Any, Awaitable, Callable

from ytmusicapi import YTMusic

from rpc_common.generated.methods import emit_radio_tracks_added
from rpc_common.generated.models import TracksAddedParams

from . import catalog

logger = logging.getLogger(__name__)

# For this long after tracks were sent, a track's start doesn't ask for more.
STALE_COUNT_SEC = 3.0

# With nothing but lately played tracks to serve, this many of them are —
# the ones played longest ago.
REPEATS_AT_A_TIME = 3

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
        # Played lately, before this session: track id -> how far back (0:
        # the latest) — what the front passed to start(). Not served while
        # there is anything else (docs/protocol.md §7.1).
        self._recent: dict[str, int] = {}
        # How many times a skip has replaced the queue — see _top_up().
        self._replacements = 0
        # When tracks were last sent (time.monotonic()) — see track_started().
        self._topped_up_at = float("-inf")

    async def start(self, seed: str | None, recent_track_ids: list[str] | None = None) -> dict:
        if not seed:
            raise ValueError("YouTube Music radio needs a seed trackId or playlistId")
        self.station_id = seed
        self.mix_id = None if _is_video_id(seed) else seed
        self.last_good = None
        self._played = set()
        self._skipped = set()
        self._recent = {}
        for rank, track_id in enumerate(recent_track_ids or []):
            self._recent.setdefault(track_id, rank)
        # The track a station is started from was asked for, however
        # lately it played.
        self._recent.pop(seed, None)

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
        # track's radio re-includes the track itself as its first entry —
        # and minus what played lately, before this session, while there is
        # anything else: failing that, a few of those, the ones played
        # longest ago first.
        tracks, held, seen = [], [], set()
        for t in result.get("tracks") or []:
            vid = t.get("videoId")
            if not vid or vid in seen or vid in self._played or vid in self._skipped:
                continue
            seen.add(vid)
            (held if vid in self._recent else tracks).append(_normalize_watch_track(t))
        if not tracks:
            held.sort(key=lambda t: self._recent[t["videoId"]], reverse=True)
            tracks = held[:REPEATS_AT_A_TIME]
        return tracks

    def _fetch_batch(self, anchor: str | None = None) -> dict:
        anchor = anchor or self.last_good
        if self.mix_id is not None:
            if anchor is None:
                return self.client.get_watch_playlist(playlistId=self.mix_id, radio=True)
            return self.client.get_watch_playlist(videoId=anchor, playlistId=self.mix_id, radio=True)
        return self.client.get_watch_playlist(videoId=anchor or self.station_id, radio=True)

    async def _top_up(self, *, replace: bool, anchor: str | None = None) -> None:
        if self.station_id is None:
            return
        replacements = self._replacements
        result = await asyncio.to_thread(self._fetch_batch, anchor)
        if replace:
            self._replacements += 1
        elif replacements != self._replacements:
            # A skip replaced the queue while this was on its way: what it
            # brought belongs after a queue that is no more.
            return
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
            self._topped_up_at = time.monotonic()
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

    def _follow_front(self, queued_after: int | None) -> None:
        # The front's queue is not what was served: it keeps tracks it has
        # downloaded when the rest is replaced, drops the ones it already
        # has, and may play tracks of an earlier session. Where it says how
        # many it has left (queuedAfter, docs/protocol.md §7.1), no more
        # than that many of the served ones can still be waiting — so the
        # ones it never took don't keep a top-up's tracks out as "queued".
        # Not by a count from before the last tracks were sent (see
        # track_started()), which would drop those.
        if time.monotonic() - self._topped_up_at <= STALE_COUNT_SEC:
            return
        if queued_after is not None and len(self._upcoming) > queued_after:
            self._upcoming = self._upcoming[len(self._upcoming) - queued_after :]

    async def track_started(self, track_id: str, queued_after: int | None = None) -> None:
        self._played.add(track_id)
        # Everything queued up to it has been played or jumped past.
        position = self._upcoming.index(track_id) if track_id in self._upcoming else -1
        if position >= 0:
            del self._upcoming[: position + 1]
        self._follow_front(queued_after)
        # The front jumped ahead in the queue (the user picked a track
        # further down — not a skip, nothing to replace) and little is left
        # after it: more is appended now rather than when it ends, following
        # the track picked. A track reached in order is topped up after as
        # ever, once it has played.
        if queued_after is None:
            low = position > 0 and len(self._upcoming) < UPCOMING_LOW_WATER
        else:
            # By the front's own count, whichever way the track was reached
            # — going by the served tracks alone, a queue that had come
            # apart from them ran out with no more sent, and so did one
            # whose tracks were picked one after another, none played to
            # its end. Unless more was sent a moment ago: the count is then
            # from before that came (a track ending and the next starting
            # are reported together).
            low = queued_after < UPCOMING_LOW_WATER and time.monotonic() - self._topped_up_at > STALE_COUNT_SEC
        if low:
            await self._top_up(replace=False, anchor=track_id)

    async def track_finished(self, track_id: str, played_ms: int, queued_after: int | None = None) -> None:
        self._report_listen(track_id)
        self.last_good = track_id
        self._follow_front(queued_after)
        # Played to the end: nothing to correct, keep what's queued.
        left = len(self._upcoming) if queued_after is None else queued_after
        if left < UPCOMING_LOW_WATER:
            await self._top_up(replace=False)

    async def skip(self, track_id: str, played_ms: int) -> None:
        self._skipped.add(track_id)
        if played_ms >= LISTEN_MS:
            self._report_listen(track_id)
        # "Not this": a fresh batch replaces what's queued.
        await self._top_up(replace=True)
