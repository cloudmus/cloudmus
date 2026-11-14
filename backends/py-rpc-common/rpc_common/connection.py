"""Whether a backend can reach its service: connection.check and
connection/changed (docs/protocol.md §7.9, 1.19+).

One per backend, owned by BackendServer once the backend gives it a probe
(BackendServer.set_connection_probe()). The server reports to it what every
request shows — a failure for lack of a network makes it offline — and a
backend's own background work (a radio top-up, a stream download) reports
through report_failure(). Back to online only after a probe reaches the
service: a request answered from a cache would say nothing about it.
"""
from __future__ import annotations

import asyncio
import logging
from typing import Any, Awaitable, Callable

from . import errors

logger = logging.getLogger(__name__)

# One light request to the service. Returns when the service answered, in
# whatever way short of a server error (an expired session is the service
# answering); raises otherwise.
Probe = Callable[[], Awaitable[None]]
NotifyFn = Callable[[str, dict[str, Any]], Awaitable[None]]

ONLINE = "online"
OFFLINE = "offline"

# Below connection.check's 10 s method timeout, with room for the answer.
PROBE_TIMEOUT_SEC = 6.0


class ConnectionMonitor:
    def __init__(self, probe: Probe, notify: NotifyFn | None = None):
        self._probe = probe
        self._notify = notify
        self.state = ONLINE
        self.detail: str | None = None
        # One probe at a time: checks asked for meanwhile share its answer.
        self._running: asyncio.Task | None = None

    def set_notify(self, notify: NotifyFn) -> None:
        self._notify = notify

    async def check(self) -> dict[str, Any]:
        """connection.check's result; never raises."""
        if self._running is None or self._running.done():
            self._running = asyncio.create_task(self._run_probe())
        await asyncio.shield(self._running)
        result: dict[str, Any] = {"state": self.state}
        if self.detail:
            result["detail"] = self.detail
        return result

    async def _run_probe(self) -> None:
        try:
            await asyncio.wait_for(self._probe(), PROBE_TIMEOUT_SEC)
        except asyncio.TimeoutError:
            await self._set(OFFLINE, "the service did not answer in time")
        except Exception as exc:  # noqa: BLE001 - any failure is an answer here
            await self._set(OFFLINE, str(exc) or type(exc).__name__)
        else:
            await self._set(ONLINE, None)

    async def report_failure(self, exc: BaseException) -> bool:
        """What a request or background task failed with. True if it was the
        network (the state is offline now)."""
        if not errors.is_offline_failure(exc):
            return False
        await self._set(OFFLINE, str(exc) or type(exc).__name__)
        return True

    def reached_maybe(self) -> None:
        """Something that needs the service just worked while offline: a probe
        makes sure, in the background."""
        if self.state == OFFLINE and (self._running is None or self._running.done()):
            self._running = asyncio.create_task(self._run_probe())

    async def _set(self, state: str, detail: str | None) -> None:
        changed = state != self.state
        self.state = state
        self.detail = detail
        if not changed:
            return
        logger.info("connection: %s%s", state, f" ({detail})" if detail else "")
        if self._notify is None:
            return
        params: dict[str, Any] = {"state": state}
        if detail:
            params["detail"] = detail
        try:
            await self._notify("connection/changed", params)
        except Exception:  # noqa: BLE001 - the front may be gone
            logger.debug("connection/changed not sent", exc_info=True)
