"""Rotor/wave logic, extracted from ym_player/player.py's start_wave() /
_fetch_wave_batch() / the wave-feedback calls inside _advance().

In the old monolith the Player owned the queue and decided for itself when
to fetch more tracks (once the local queue ran low). In this protocol the
front owns the queue instead (docs/protocol.md §7.1) — the backend only
gets a chance to react on catalog.startRadio and on each feedback.* call, so
it pushes the session's recomputed upcoming sequence via radio/tracksAdded
(with replaceUpcoming, see docs/protocol.md §7.1) whenever a track finishes
or is skipped: the wave adapts to every play/skip/like, so each batch
supersedes the previous one's unplayed tracks rather than adding to them.

This backend talks to Yandex's *session* rotor API (/rotor/session/*), not
the legacy station one (/rotor/station/{station}/feedback): the legacy
feedback endpoint is dead (every call returns 400 "condition is not met"),
so the server never learned about plays/skips and kept re-serving the same
chain head. The session flow accepts feedback (radioStarted/trackStarted/
trackFinished/skip, each 200) and advances the chain via session/tracks.
"""
from __future__ import annotations

import asyncio
import logging
import time
from typing import Any, Awaitable, Callable

from yandex_music import Client, Track

from rpc_common.generated.methods import emit_radio_tracks_added
from rpc_common.generated.models import TracksAddedParams

from . import catalog

logger = logging.getLogger(__name__)

NotifyFn = Callable[[str, dict[str, Any]], Awaitable[None]]


def _resolve_station(seed: str | None) -> str:
    # seed (protocol/methods.yaml's catalog.startRadio) is source-defined —
    # the front never formats it, only ever either omits it or echoes an id
    # it already has verbatim (docs/protocol.md §7.1). Two shapes reach
    # here in practice: an already-complete rotor station address (e.g.
    # WAVE_STATION_ID = "user:onyourwave", from the My Wave sidebar entry's
    # own Playlist.id), or a bare trackId (from the track list's "Start
    # Radio from This Track" context-menu action). Yandex's rotor API
    # addresses every station as "<type>:<id>" (confirmed in the
    # yandex_music library's own docs — "user:onyourwave", "track:1234",
    # "genre:pop", ...), so a seed with no colon at all can only be the
    # bare-trackId case and needs the "track:" type prefixed on here,
    # where this Yandex-specific formatting knowledge belongs (it used to
    # live in the front, which broke once a second radio.RadioSession-style
    # backend — YouTube — started receiving the same seed unprefixed).
    if not seed:
        return catalog.WAVE_STATION_ID
    if ":" in seed:
        return seed
    return f"track:{seed}"


class RadioSession:
    def __init__(self, client: Client, notify: NotifyFn):
        self.client = client
        self._notify = notify
        self.station = catalog.WAVE_STATION_ID
        self.radio_session_id: str | None = None
        self.batch_id: str | None = None
        # Tracks that actually started playing this session — never
        # re-served as "upcoming". Tracks merely served (but replaced before
        # being reached) may legitimately come back in a later sequence.
        self._played: set[str] = set()

    def _post(self, path: str, payload: dict) -> dict:
        result = self.client._request.post(f"{self.client.base_url}{path}", json=payload)
        return result if isinstance(result, dict) else {}

    def _tracks_from(self, result: dict) -> list[Track]:
        tracks = []
        for entry in result.get("sequence") or []:
            if entry.get("type") != "track":
                continue
            track = entry.get("track")
            if track:
                t = Track.de_json(track, self.client)
                if t is not None:
                    tracks.append(t)
        return tracks

    async def _send_feedback(self, event_type: str, **event: Any) -> None:
        if not self.radio_session_id or not self.batch_id:
            return
        path = f"/rotor/session/{self.radio_session_id}/feedback"
        payload = {
            "batch_id": self.batch_id,
            "event": {"type": event_type, "timestamp": int(time.time()), **event},
        }

        def send() -> None:
            try:
                self._post(path, payload)
            except Exception as e:
                logger.debug("session feedback %s failed: %s", event_type, e)

        await asyncio.to_thread(send)

    async def start(self, seed: str | None) -> dict:
        self.station = _resolve_station(seed)
        # So the station's tracks carry liked/disliked too (see catalog.LikeCache).
        await asyncio.to_thread(catalog.likes.ensure_loaded, self.client)

        result = await asyncio.to_thread(
            self._post,
            "/rotor/session/new",
            {
                "seeds": [self.station],
                "includeTracksInResponse": True,
                "includeWaveModel": True,
                "interactive": True,
            },
        )
        self.radio_session_id = result.get("radioSessionId")
        self.batch_id = result.get("batchId")
        tracks = self._tracks_from(result)
        self._played = set()
        await self._send_feedback("radioStarted", **{"from": "cloudmus"})

        return {
            "stationId": self.station,
            "initialTracks": [catalog.to_track(t).to_dict() for t in tracks],
        }

    async def _top_up(self, played_track_id: str) -> None:
        # queue=[played_track_id] tells the session API to advance the chain
        # past the track that was just finished/skipped. Unlike the legacy
        # station flow this is a session-scoped call, so it also updates our
        # batch_id — feedback events for the *next* batch must reference it.
        if not self.radio_session_id:
            return
        result = await asyncio.to_thread(
            self._post,
            f"/rotor/session/{self.radio_session_id}/tracks",
            {"queue": [played_track_id]},
        )
        if not result:
            return
        self.batch_id = result.get("batchId", self.batch_id)
        tracks = self._tracks_from(result)
        # The sequence can still start with the track just played (the
        # chain head advancing past it) — only unplayed ones are upcoming.
        upcoming = [t for t in tracks if str(t.id) not in self._played]

        if upcoming:
            await emit_radio_tracks_added(
                self._notify,
                TracksAddedParams(
                    stationId=self.station,
                    tracks=[catalog.to_track(t) for t in upcoming],
                    replaceUpcoming=True,
                ),
            )

    async def track_started(self, track_id: str) -> None:
        self._played.add(str(track_id))
        await self._send_feedback("trackStarted", trackId=track_id)

    async def track_finished(self, track_id: str, played_ms: int) -> None:
        await self._send_feedback(
            "trackFinished", trackId=track_id, totalPlayedSeconds=played_ms / 1000
        )
        await self._top_up(track_id)

    async def skip(self, track_id: str, played_ms: int) -> None:
        await self._send_feedback("skip", trackId=track_id, totalPlayedSeconds=played_ms / 1000)
        await self._top_up(track_id)