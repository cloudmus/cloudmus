"""Owns one BackendClient per discovered manifest: starts them all, routes
notifications with the originating source id attached, and restarts a
crashed backend with backoff (docs/protocol.md §9's "crash handling", 2s/5s/10s,
max 3 attempts before giving up and marking it unavailable for the session).
"""
from __future__ import annotations

import asyncio
import logging
from typing import Any, Callable

from .discovery import BackendManifest, discover_manifests
from .rpc_client import BackendClient

logger = logging.getLogger(__name__)

RESTART_DELAYS = (2.0, 5.0, 10.0)

# Front-internal notification (never sent over the wire) telling the app a
# backend has given up restarting and should be treated as permanently gone
# for this session.
BACKEND_UNAVAILABLE = "backend/unavailable"


class SourceManager:
    def __init__(self, on_notification: Callable[[str, str, dict[str, Any]], None]):
        """on_notification(source_id, method, params)"""
        self._on_notification = on_notification
        self.clients: dict[str, BackendClient] = {}
        self._manifests: dict[str, BackendManifest] = {}
        self._restart_attempts: dict[str, int] = {}

    async def start_all(self, manifests: list[BackendManifest] | None = None) -> None:
        manifests = manifests if manifests is not None else discover_manifests()
        for manifest in manifests:
            self._manifests[manifest.id] = manifest
            await self._start_one(manifest)

    async def _start_one(self, manifest: BackendManifest) -> None:
        client = BackendClient(
            manifest, lambda method, params, sid=manifest.id: self._on_notification(sid, method, params)
        )
        try:
            await client.start()
        except Exception as e:
            logger.warning("failed to start backend %s: %s", manifest.id, e)
            self._on_notification(manifest.id, BACKEND_UNAVAILABLE, {"reason": str(e)})
            return
        self.clients[manifest.id] = client
        asyncio.create_task(self._watch(manifest))

    async def _watch(self, manifest: BackendManifest) -> None:
        client = self.clients.get(manifest.id)
        if client is None:
            return
        while client.available:
            await asyncio.sleep(0.5)

        self.clients.pop(manifest.id, None)
        attempts = self._restart_attempts.get(manifest.id, 0)
        if attempts >= len(RESTART_DELAYS):
            self._on_notification(
                manifest.id, BACKEND_UNAVAILABLE, {"reason": "crashed repeatedly", "stderr": client.stderr_tail}
            )
            return

        delay = RESTART_DELAYS[attempts]
        self._restart_attempts[manifest.id] = attempts + 1
        logger.info("backend %s disconnected, restarting in %ss (attempt %s)", manifest.id, delay, attempts + 1)
        await asyncio.sleep(delay)
        await self._start_one(manifest)

    async def shutdown_all(self) -> None:
        await asyncio.gather(*(c.shutdown() for c in self.clients.values()), return_exceptions=True)
        self.clients.clear()
