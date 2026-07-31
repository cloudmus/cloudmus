"""Logs every outbound HTTP call yandex_music makes to Yandex's own API —
one level below rpc_common.server's front/backend protocol logging, so a
timeout or wrong-data bug can be traced all the way to which Yandex Music
request caused it (method/URL/params, timing, and success/failure), not just
that "catalog.listTracks took 733ms" with no visibility into why.

yandex_music.utils.request.Request._request_wrapper is the single choke
point every GET/POST/PUT/DELETE/retrieve call goes through (see that
module), so patching it there covers the whole library — the device-auth
flow's own throwaway Client() (auth.py) included, not just the cached one
from client.py.
"""
from __future__ import annotations

import logging
import time
from typing import Any

logger = logging.getLogger(__name__)

_installed = False


def install() -> None:
    """Idempotent — safe to call from more than one place at startup."""
    global _installed
    if _installed:
        return
    _installed = True

    from yandex_music.utils.request import Request

    original = Request._request_wrapper

    def logged_request_wrapper(self: Request, *args: Any, **kwargs: Any) -> bytes:
        method = args[0] if len(args) > 0 else kwargs.get("method", "?")
        url = args[1] if len(args) > 1 else kwargs.get("url", "?")
        # params (GET) / data or json (POST/PUT/DELETE) is the actual
        # request content. headers/proxies/timeout are deliberately left
        # out — headers carry the OAuth bearer token, which must never end
        # up in a log file even a local, opt-in debug one.
        payload = kwargs.get("params") or kwargs.get("data") or kwargs.get("json")
        logger.info("YM -> %s %s params=%r", method, url, payload)
        started = time.monotonic()
        try:
            result = original(self, *args, **kwargs)
        except Exception as exc:
            logger.warning(
                "YM <- %s %s failed (%.0fms): %s", method, url, (time.monotonic() - started) * 1000, exc
            )
            raise
        # Body content isn't logged here (unlike rpc_common.server's own
        # params/result logging) — a handful of Yandex endpoints (e.g. the
        # device-auth token exchange) return the OAuth access token itself
        # in the response body, which must never land in a log file.
        logger.info(
            "YM <- %s %s (%.0fms) %d bytes",
            method, url, (time.monotonic() - started) * 1000, len(result) if result else 0,
        )
        return result

    Request._request_wrapper = logged_request_wrapper
