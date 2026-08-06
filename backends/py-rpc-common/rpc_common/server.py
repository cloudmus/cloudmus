"""Shared stdio JSON-RPC server scaffold for backends.

Handles the initialize/shutdown lifecycle (docs/protocol.md §4) and request
dispatch so each backend only implements its own catalog.*/playback.*/etc.
handlers. Every handler receives (params, request_id) and returns a result
dict (or raises BackendError for an application error, per §9).
"""
from __future__ import annotations

import asyncio
import json
import logging
import time
from typing import Any, Awaitable, Callable

from . import jsonrpc, transport

logger = logging.getLogger(__name__)

RequestHandler = Callable[[dict[str, Any], int], "Awaitable[dict[str, Any]] | dict[str, Any]"]

# Full params/result payloads get logged (not just method+id+duration) so a
# hang or a wrong-data bug shows exactly what was asked for and what came
# back — but a handful of methods (catalog.listTracks and friends) can
# return hundreds of full track objects, so anything past this length is
# truncated rather than let one line balloon to megabytes.
_MAX_LOGGED_VALUE_CHARS = 2000


def _summarize(value: Any) -> str:
    text = json.dumps(value, ensure_ascii=False, default=str)
    if len(text) > _MAX_LOGGED_VALUE_CHARS:
        omitted = len(text) - _MAX_LOGGED_VALUE_CHARS
        return f"{text[:_MAX_LOGGED_VALUE_CHARS]}... ({omitted} more chars)"
    return text


class BackendError(Exception):
    """Raise from a handler to send a JSON-RPC error response with an
    application error code (see errors.py for the standard ranges)."""

    def __init__(self, code: int, message: str, data: dict[str, Any] | None = None):
        super().__init__(message)
        self.code = code
        self.message = message
        self.data = data


class BackendServer:
    def __init__(
        self,
        source_id: str,
        source_name: str,
        source_version: str,
        source_description: str,
        capabilities: dict[str, Any],
    ):
        self.source_id = source_id
        self.source_name = source_name
        self.source_version = source_version
        self.source_description = source_description
        self.capabilities = capabilities
        self._handlers: dict[str, RequestHandler] = {}
        self._writer: transport.NdjsonWriter | None = None

    def method(self, name: str) -> Callable[[RequestHandler], RequestHandler]:
        """Decorator: registers a handler for a `namespace.methodName` request."""

        def decorator(fn: RequestHandler) -> RequestHandler:
            self._handlers[name] = fn
            return fn

        return decorator

    async def notify(self, method: str, params: dict[str, Any] | None = None) -> None:
        assert self._writer is not None, "notify() called before run() started"
        # <~ (not -> or <-): this is the backend pushing unsolicited, not a
        # request/response pair — e.g. radio/tracksAdded, track/streamReady.
        logger.info("<~ %s params=%s", method, _summarize(params))
        await self._writer.send(jsonrpc.make_notification(method, params))

    def _handle_initialize(self, params: dict[str, Any], request_id: int) -> dict[str, Any]:
        return {
            "protocolVersion": "1.2",
            "source": {
                "id": self.source_id,
                "name": self.source_name,
                "version": self.source_version,
                "description": self.source_description,
            },
            "capabilities": self.capabilities,
        }

    def _handle_shutdown(self, params: dict[str, Any], request_id: int) -> dict[str, Any]:
        return {}

    async def run(self, rpc_out: Any) -> None:
        reader, writer = await transport.open_stdio(rpc_out)
        self._writer = writer

        handlers: dict[str, RequestHandler] = {
            "initialize": self._handle_initialize,
            "shutdown": self._handle_shutdown,
            **self._handlers,
        }

        async for message in reader:
            request_id = message.get("id")
            method_name = message.get("method")
            if method_name is None:
                continue
            handler = handlers.get(method_name)
            try:
                if handler is None:
                    if request_id is not None:
                        await writer.send(
                            jsonrpc.make_error(
                                request_id, -32601, f"method not found: {method_name}"
                            )
                        )
                    continue
                # Logged around every dispatch (not just failures), params
                # and result included, so a request that hangs inside a
                # handler — e.g. a blocking network call via
                # asyncio.to_thread() — still leaves a trace on stderr: which
                # method, with what params, and that it never completed,
                # instead of a front-side "request timed out" with nothing
                # to go on. Also makes a wrong-data bug (e.g. a field the
                # handler forgot to map) visible directly in the log instead
                # of only inferable from its symptom in the UI.
                params = message.get("params", {})
                logger.info("-> %s id=%s params=%s", method_name, request_id, _summarize(params))
                started = time.monotonic()
                result = handler(params, request_id)
                if asyncio.iscoroutine(result):
                    result = await result
                logger.info(
                    "<- %s id=%s (%.0fms) result=%s",
                    method_name, request_id, (time.monotonic() - started) * 1000, _summarize(result),
                )
                if request_id is not None:
                    await writer.send(jsonrpc.make_result(request_id, result))
            except BackendError as exc:
                logger.warning(
                    "<- %s id=%s failed (%.0fms): code=%s message=%s data=%s",
                    method_name, request_id, (time.monotonic() - started) * 1000, exc.code, exc.message,
                    _summarize(exc.data),
                )
                if request_id is not None:
                    await writer.send(
                        jsonrpc.make_error(request_id, exc.code, exc.message, exc.data)
                    )
            except Exception as exc:  # noqa: BLE001 - must never crash the stdio loop
                logger.exception("unhandled error in %s id=%s", method_name, request_id)
                if request_id is not None:
                    await writer.send(jsonrpc.make_error(request_id, -32603, str(exc)))

            if method_name == "shutdown":
                return
