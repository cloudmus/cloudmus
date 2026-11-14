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
import socket
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


def describe(exc: BaseException) -> str:
    """A few words on why the service can't be reached, for the front's
    tooltip — not the whole chain a library wraps around it (urllib3's runs
    to a paragraph)."""
    seen: set[int] = set()
    current: BaseException | None = exc
    texts: list[str] = []
    while current is not None and id(current) not in seen:
        seen.add(id(current))
        if isinstance(current, socket.gaierror):
            return "the service's address can't be resolved"
        if isinstance(current, (asyncio.TimeoutError, TimeoutError)):
            return "the service doesn't answer in time"
        if isinstance(current, ConnectionRefusedError):
            return "the connection is refused"
        texts.append(f"{type(current).__name__}: {current}")
        current = current.__cause__ or current.__context__
    whole = " ".join(texts)
    # urllib3 and requests only say so in their text.
    for needle, words in (
        ("name resolution", "the service's address can't be resolved"),
        ("Name or service not known", "the service's address can't be resolved"),
        ("nodename nor servname", "the service's address can't be resolved"),
        ("getaddrinfo failed", "the service's address can't be resolved"),
        ("ProxyError", "the proxy can't be reached"),
        ("timed out", "the service doesn't answer in time"),
        ("Read timed out", "the service doesn't answer in time"),
        ("Connection refused", "the connection is refused"),
        ("Network is unreachable", "the network is unreachable"),
        ("No route to host", "the network is unreachable"),
        ("SSLError", "a secure connection can't be made"),
    ):
        if needle in whole:
            return words
    text = str(exc) or type(exc).__name__
    return text if len(text) <= 80 else text[:79] + "…"


class ConnectionMonitor:
    def __init__(
        self, probe: Probe, notify: NotifyFn | None = None, translate: Callable[[str], str] | None = None
    ):
        self._probe = probe
        self._notify = notify
        # describe()'s words go to the user: in the backend's language (its
        # dictionaries hold them, as they do rpc_common's other texts).
        self._translate = translate or (lambda text: text)
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
            await self._set(OFFLINE, self._translate("the service doesn't answer in time"))
        except Exception as exc:  # noqa: BLE001 - any failure is an answer here
            await self._set(OFFLINE, self._translate(describe(exc)))
        else:
            await self._set(ONLINE, None)

    async def report_failure(self, exc: BaseException) -> bool:
        """What a request or background task failed with. True if it was the
        network (the state is offline now)."""
        if not errors.is_offline_failure(exc):
            return False
        await self._set(OFFLINE, self._translate(describe(exc)))
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
