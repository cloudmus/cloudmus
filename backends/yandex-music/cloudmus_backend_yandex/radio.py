"""Rotor/wave logic, extracted from ym_player/player.py's start_wave() /
_fetch_wave_batch() / the wave-feedback calls inside _advance().

In the old monolith the Player owned the queue and decided for itself when
to fetch more tracks (once the local queue ran low). In this protocol the
front owns the queue instead (docs/protocol.md §7.1) — the backend only
gets a chance to react on catalog.startRadio and on each feedback.* call, so
it tops up by pushing a fresh batch via radio/tracksAdded whenever a track
finishes or is skipped, mirroring the same "keep the pipeline full" intent
without needing to know the front's exact queue depth.
"""
from __future__ import annotations

import asyncio
import logging
from typing import Any, Awaitable, Callable

from yandex_music import Client

from rpc_common.generated.methods import emit_radio_tracks_added
from rpc_common.generated.models import TracksAddedParams

from . import catalog

logger = logging.getLogger(__name__)

NotifyFn = Callable[[str, dict[str, Any]], Awaitable[None]]


class RadioSession:
    def __init__(self, client: Client, notify: NotifyFn):
        self.client = client
        self._notify = notify
        self.station = catalog.WAVE_STATION_ID
        self.batch_id: str | None = None

    async def start(self, seed: str | None) -> dict:
        self.station = seed or catalog.WAVE_STATION_ID

        def start_feedback() -> None:
            try:
                self.client.rotor_station_feedback_radio_started(self.station, from_="cloudmus")
            except Exception as e:
                logger.debug("rotor_station_feedback_radio_started failed: %s", e)

        await asyncio.to_thread(start_feedback)

        result = await asyncio.to_thread(self.client.rotor_station_tracks, self.station)
        tracks = []
        if result is not None:
            self.batch_id = result.batch_id
            tracks = [seq.track for seq in result.sequence if seq.track]

        return {
            "stationId": self.station,
            "initialTracks": [catalog.to_track(t).to_dict() for t in tracks],
        }

    async def _top_up(self, played_track_id: str) -> None:
        # queue=played_track_id tells the rotor API to advance the chain
        # past the track that was just finished/skipped — the yandex_music
        # library's own rotor_station_tracks() docstring documents this as
        # required ("1. pass the id of the track that came before"); without
        # it the API just re-returns the same batch head every time, which
        # is why the wave used to appear stuck replaying the same tracks.
        result = await asyncio.to_thread(self.client.rotor_station_tracks, self.station, queue=played_track_id)
        if result is None:
            return
        self.batch_id = result.batch_id
        tracks = [seq.track for seq in result.sequence if seq.track]
        if tracks:
            await emit_radio_tracks_added(
                self._notify,
                TracksAddedParams(stationId=self.station, tracks=[catalog.to_track(t) for t in tracks]),
            )

    async def track_started(self, track_id: str) -> None:
        def send() -> None:
            try:
                self.client.rotor_station_feedback_track_started(
                    self.station, track_id, batch_id=self.batch_id
                )
            except Exception as e:
                logger.debug("rotor_station_feedback_track_started failed: %s", e)

        await asyncio.to_thread(send)

    async def track_finished(self, track_id: str, played_ms: int) -> None:
        def send() -> None:
            try:
                self.client.rotor_station_feedback_track_finished(
                    self.station, track_id, played_ms / 1000, batch_id=self.batch_id
                )
            except Exception as e:
                logger.debug("rotor_station_feedback_track_finished failed: %s", e)

        await asyncio.to_thread(send)
        await self._top_up(track_id)

    async def skip(self, track_id: str, played_ms: int) -> None:
        def send() -> None:
            try:
                self.client.rotor_station_feedback_skip(
                    self.station, track_id, played_ms / 1000, batch_id=self.batch_id
                )
            except Exception as e:
                logger.debug("rotor_station_feedback_skip failed: %s", e)

        await asyncio.to_thread(send)
        await self._top_up(track_id)
