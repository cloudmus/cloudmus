"""Device Auth Flow, driven by auth.start (docs/protocol.md §10.1).

Ported from the original ym_player/auth.py: the on_code callback used to
print the URL/code straight to the console; now it emits an auth/prompt
notification instead, and completion emits auth/statusChanged.
"""
from __future__ import annotations

import asyncio
import logging
import threading
from typing import Any, Awaitable, Callable

from yandex_music import Client

from . import client as client_module
from . import config

logger = logging.getLogger(__name__)

NotifyFn = Callable[[str, dict[str, Any]], Awaitable[None]]


class DeviceAuthSession:
    """Tracks one in-flight (or completed) device-code login attempt."""

    def __init__(self) -> None:
        self._cancel_event = threading.Event()
        self._task: asyncio.Task | None = None
        self.status: str = "unauthenticated"
        self.error_message: str | None = None

    def get_status(self) -> dict[str, Any]:
        if config.get_token():
            return {"status": "authenticated"}
        if self.status == "error":
            return {"status": "error", "detail": self.error_message or ""}
        return {"status": self.status}

    def start(self, notify: NotifyFn) -> None:
        if self._task is not None and not self._task.done():
            return  # already in progress
        self._cancel_event = threading.Event()
        self.status = "pending"
        self._task = asyncio.create_task(self._run(notify))

    def cancel(self) -> None:
        self._cancel_event.set()

    async def _run(self, notify: NotifyFn) -> None:
        loop = asyncio.get_event_loop()

        def on_code(code) -> None:
            asyncio.run_coroutine_threadsafe(
                notify(
                    "auth/prompt",
                    {
                        "flow": "deviceCode",
                        "url": code.verification_url,
                        "code": code.user_code,
                        "expiresInSec": code.expires_in,
                    },
                ),
                loop,
            )

        def run_blocking() -> str:
            device_client = Client()
            token = device_client.device_auth(
                on_code=on_code, should_cancel=self._cancel_event.is_set
            )
            return token.access_token

        try:
            access_token = await asyncio.to_thread(run_blocking)
        except Exception as e:
            logger.debug("device auth failed: %s", e)
            self.status = "error"
            self.error_message = str(e)
            await notify("auth/statusChanged", {"status": "error", "message": str(e)})
            return

        if self._cancel_event.is_set():
            self.status = "unauthenticated"
            return

        config.set_token(access_token)
        client_module.reset_client()
        self.status = "authenticated"
        await notify("auth/statusChanged", {"status": "authenticated"})

    def logout(self) -> None:
        config.clear_token()
        client_module.reset_client()
        self.status = "unauthenticated"
