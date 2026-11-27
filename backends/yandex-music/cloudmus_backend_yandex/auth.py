"""Device Auth Flow, driven by auth.start (docs/protocol.md §10.1: the `code`
method).

Ported from the original ym_player/auth.py: the on_code callback used to
print the URL/code straight to the console; now it emits an auth/prompt
notification instead, and completion emits auth/statusChanged.
"""
from __future__ import annotations

import asyncio
import logging
import threading
import time
from typing import Any, Awaitable, Callable

from yandex_music import Client
from yandex_music.exceptions import DeviceAuthError

from rpc_common.generated.methods import emit_auth_prompt, emit_auth_status_changed
from rpc_common.generated.models import AuthMethod, PromptParams, StatusChangedParams

from . import client as client_module
from . import config

logger = logging.getLogger(__name__)

NotifyFn = Callable[[str, dict[str, Any]], Awaitable[None]]


# A code with less left than this isn't worth showing: no time to type it.
_MIN_LEFT_SEC = 30


def _saved_pending() -> dict | None:
    """The device code a previous run was waiting on, while it's still good
    for long enough to be worth showing again."""
    pending = config.get_pending_auth()
    try:
        if pending is not None and float(pending["expiresAt"]) - time.time() > _MIN_LEFT_SEC:
            return pending
    except (KeyError, TypeError, ValueError):
        pass
    return None


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
        # The flow resets this too, but only once it notices — the front
        # asks for the status right after auth.cancel returns.
        if self.status == "pending":
            self.status = "unauthenticated"

    async def _run(self, notify: NotifyFn) -> None:
        # Not Client().device_auth(): that keeps the device code in a local
        # variable, so a backend killed while the user confirms the code in
        # a browser (Android unloads the whole app on a phone short of
        # memory) lost it, and the next start could only ask for a new one —
        # the confirmed code was then of no use. Its two steps, with the code
        # saved in between, let the next start go on polling the same one.
        loop = asyncio.get_running_loop()
        cancel_event = self._cancel_event

        def run_blocking() -> str | None:
            device_client = Client()
            pending = _saved_pending()
            if pending is None:
                code = device_client.request_device_code()
                pending = {
                    "deviceCode": code.device_code,
                    "userCode": code.user_code,
                    "url": code.verification_url,
                    "interval": code.interval,
                    "expiresAt": time.time() + code.expires_in,
                }
                config.set_pending_auth(pending)
            method = AuthMethod(
                kind="code",
                url=pending["url"],
                code=pending["userCode"],
                expiresInSec=max(0, int(pending["expiresAt"] - time.time())),
            )
            asyncio.run_coroutine_threadsafe(emit_auth_prompt(notify, PromptParams(methods=[method])), loop)
            # Polled at once: a resumed code has likely been confirmed already.
            while not cancel_event.is_set():
                token = device_client.poll_device_token(pending["deviceCode"])
                if token is not None:
                    return token.access_token
                if time.time() >= pending["expiresAt"]:
                    raise DeviceAuthError("the code expired before it was confirmed")
                cancel_event.wait(max(1, pending["interval"]))
            return None

        try:
            access_token = await asyncio.to_thread(run_blocking)
        except Exception as e:
            logger.debug("device auth failed: %s", e)
            # Yandex turned the code down (expired, denied): it's of no more
            # use. Anything else — no network, say — leaves it for the next
            # try, which may well find it confirmed.
            if isinstance(e, DeviceAuthError):
                config.clear_pending_auth()
            self.status = "error"
            self.error_message = str(e)
            await emit_auth_status_changed(notify, StatusChangedParams(status="error", message=str(e)))
            return

        if access_token is None or self._cancel_event.is_set():
            config.clear_pending_auth()
            self.status = "unauthenticated"
            return

        config.set_token(access_token)
        config.clear_pending_auth()
        client_module.reset_client()
        self.status = "authenticated"
        await emit_auth_status_changed(notify, StatusChangedParams(status="authenticated"))

    def logout(self) -> None:
        self._cancel_event.set()
        config.clear_token()
        client_module.reset_client()
        self.status = "unauthenticated"
