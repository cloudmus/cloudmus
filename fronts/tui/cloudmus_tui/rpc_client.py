"""Per-backend subprocess + NDJSON JSON-RPC client (docs/protocol.md §2-3, §5).

Deliberately self-contained rather than importing backends/py-rpc-common:
front and backends are independent components, and the client-side NDJSON
read/write loop is small enough (~100 lines) that duplicating it here is
cheaper than a cross-folder dependency.
"""
from __future__ import annotations

import asyncio
import itertools
import json
import logging
from typing import Any, Awaitable, Callable

from .discovery import BackendManifest

logger = logging.getLogger(__name__)

NotificationHandler = Callable[[str, dict[str, Any]], None]

DEFAULT_TIMEOUT = 5.0
DOWNLOAD_TIMEOUT = 60.0
STDERR_TAIL_LINES = 50


class RpcError(Exception):
    def __init__(self, code: int, message: str, data: dict[str, Any] | None = None):
        super().__init__(message)
        self.code = code
        self.data = data or {}


class BackendClient:
    """Owns one backend subprocess. `available` is False until initialize
    succeeds and turns False again on disconnect (EOF/crash)."""

    def __init__(self, manifest: BackendManifest, on_notification: NotificationHandler):
        self.manifest = manifest
        self._on_notification = on_notification
        self._proc: asyncio.subprocess.Process | None = None
        self._pending: dict[int, "asyncio.Future[Any]"] = {}
        self._ids = itertools.count(1)
        self._write_lock = asyncio.Lock()
        self.capabilities: dict[str, Any] | None = None
        self.source_info: dict[str, Any] | None = None
        self.available = False
        self.stderr_tail: list[str] = []

    async def start(self) -> None:
        self._proc = await asyncio.create_subprocess_exec(
            *self.manifest.argv,
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        asyncio.create_task(self._read_loop())
        asyncio.create_task(self._drain_stderr())
        result = await self.request(
            "initialize",
            {"protocolVersion": "1.0", "front": {"name": "cloudmus-tui", "version": "0.1.0"}},
            timeout=10.0,
        )
        self.capabilities = result["capabilities"]
        self.source_info = result["source"]
        self.available = True

    async def _drain_stderr(self) -> None:
        assert self._proc is not None and self._proc.stderr is not None
        async for raw in self._proc.stderr:
            text = raw.decode(errors="replace").rstrip()
            self.stderr_tail.append(text)
            del self.stderr_tail[:-STDERR_TAIL_LINES]

    async def _read_loop(self) -> None:
        assert self._proc is not None and self._proc.stdout is not None
        try:
            while True:
                raw = await self._proc.stdout.readline()
                if raw == b"":
                    break
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                try:
                    message = json.loads(line)
                except json.JSONDecodeError:
                    logger.warning("non-JSON line from %s: %r", self.manifest.id, line)
                    continue
                self._dispatch(message)
        finally:
            self._on_disconnect()

    def _dispatch(self, message: dict[str, Any]) -> None:
        if "method" in message and "id" not in message:
            try:
                self._on_notification(message["method"], message.get("params", {}))
            except Exception:
                logger.exception("notification handler raised for %s", message.get("method"))
            return
        if "id" in message and ("result" in message or "error" in message):
            future = self._pending.pop(message["id"], None)
            if future is None or future.done():
                return
            if "error" in message:
                err = message["error"]
                future.set_exception(RpcError(err["code"], err["message"], err.get("data")))
            else:
                future.set_result(message["result"])

    def _on_disconnect(self) -> None:
        self.available = False
        for future in self._pending.values():
            if not future.done():
                future.set_exception(RpcError(-1, "backend disconnected"))
        self._pending.clear()

    async def _send(self, message: dict[str, Any]) -> None:
        assert self._proc is not None and self._proc.stdin is not None
        line = json.dumps(message, separators=(",", ":"), ensure_ascii=False)
        async with self._write_lock:
            self._proc.stdin.write(line.encode("utf-8") + b"\n")
            await self._proc.stdin.drain()

    async def start_call(
        self, method: str, params: dict[str, Any] | None = None, timeout: float = DEFAULT_TIMEOUT
    ) -> tuple[int, "asyncio.Future[dict[str, Any]]"]:
        """Sends the request and returns (request_id, pending_future)
        immediately once the write is flushed — *before* awaiting the
        response. This matters for correlating a later track/streamReady
        notification (docs/protocol.md §11.1): a fast-resolving backend can
        get its notification read and dispatched before an `await
        client.call(...)`-style continuation resumes to record the request
        id, discarding the notification as unrecognized. Recording the id
        right after send, with no `await` in between, closes that race.
        """
        request_id = next(self._ids)
        future: "asyncio.Future[Any]" = asyncio.get_event_loop().create_future()
        self._pending[request_id] = future
        await self._send({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params or {}})

        async def _await_with_timeout() -> dict[str, Any]:
            try:
                return await asyncio.wait_for(future, timeout=timeout)
            except asyncio.TimeoutError:
                self._pending.pop(request_id, None)
                raise

        return request_id, asyncio.ensure_future(_await_with_timeout())

    async def call(
        self, method: str, params: dict[str, Any] | None = None, timeout: float = DEFAULT_TIMEOUT
    ) -> tuple[int, dict[str, Any]]:
        """Like request(), but also returns the request id. Callers that
        need to record the id *before* the response arrives (to avoid the
        race described in start_call's docstring) should use start_call
        directly instead — this wraps it for callers who don't care."""
        request_id, pending = await self.start_call(method, params, timeout=timeout)
        result = await pending
        return request_id, result

    async def request(
        self, method: str, params: dict[str, Any] | None = None, timeout: float = DEFAULT_TIMEOUT
    ) -> dict[str, Any]:
        _, result = await self.call(method, params, timeout=timeout)
        return result

    async def notify_fire_and_forget(self, method: str, params: dict[str, Any] | None = None) -> None:
        """Sends a request but never waits for/registers a pending future for
        its response — used for playback.cancel, where the front doesn't
        care about the result (docs/protocol.md §7.2)."""
        request_id = next(self._ids)
        await self._send({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params or {}})

    async def shutdown(self) -> None:
        if self._proc is None:
            return
        try:
            await self.request("shutdown", {}, timeout=3.0)
        except Exception:
            pass
        if self._proc.stdin is not None and not self._proc.stdin.is_closing():
            self._proc.stdin.close()
        try:
            await asyncio.wait_for(self._proc.wait(), timeout=3.0)
        except asyncio.TimeoutError:
            self._proc.terminate()
            try:
                await asyncio.wait_for(self._proc.wait(), timeout=2.0)
            except asyncio.TimeoutError:
                self._proc.kill()
                await self._proc.wait()
