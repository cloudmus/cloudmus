"""In-process fake front/backend harnesses for unit tests.

Two duplex ends are connected by asyncio.Queue pairs instead of OS pipes, so
tests never spawn a real subprocess, but the message shapes and the
send()/async-iteration interface are identical to transport.NdjsonWriter /
NdjsonReader — anything written against that duck-typed interface (e.g. a
front's rpc_client) can be pointed at a QueueWriter/QueueReader pair in tests
with no other changes.
"""
from __future__ import annotations

import asyncio
from typing import Any, Awaitable, Callable

from .. import jsonrpc

Handler = Callable[[dict[str, Any]], Awaitable[dict[str, Any]] | dict[str, Any]]


class QueueWriter:
    def __init__(self, queue: "asyncio.Queue[dict[str, Any] | None]"):
        self._queue = queue

    async def send(self, message: dict[str, Any]) -> None:
        await self._queue.put(message)


class QueueReader:
    def __init__(self, queue: "asyncio.Queue[dict[str, Any] | None]"):
        self._queue = queue

    def __aiter__(self) -> "QueueReader":
        return self

    async def __anext__(self) -> dict[str, Any]:
        item = await self._queue.get()
        if item is None:
            raise StopAsyncIteration
        return item


def make_duplex_pair() -> tuple[QueueWriter, QueueReader, QueueWriter, QueueReader]:
    """Returns (front_writer, front_reader, backend_writer, backend_reader):
    what the front writes, the backend reads, and vice versa.
    """
    front_to_backend: "asyncio.Queue[dict[str, Any] | None]" = asyncio.Queue()
    backend_to_front: "asyncio.Queue[dict[str, Any] | None]" = asyncio.Queue()
    return (
        QueueWriter(front_to_backend),
        QueueReader(backend_to_front),
        QueueWriter(backend_to_front),
        QueueReader(front_to_backend),
    )


class ScriptedBackend:
    """A minimal in-process backend driven by a method->handler map, plus an
    optional list of (delay_seconds, notification) pairs fired as background
    tasks once serve() starts. Used to unit-test a front's rpc_client /
    source_manager / playback_engine without a real subprocess.
    """

    def __init__(
        self,
        handlers: dict[str, Handler],
        scheduled_notifications: list[tuple[float, dict[str, Any]]] | None = None,
    ):
        self._handlers = handlers
        self._scheduled = scheduled_notifications or []

    async def serve(self, writer: QueueWriter, reader: QueueReader) -> None:
        for delay, notification in self._scheduled:
            asyncio.create_task(self._send_after(writer, delay, notification))

        async for message in reader:
            method = message.get("method")
            if method is None:
                continue
            handler = self._handlers.get(method)
            if handler is None:
                if "id" in message:
                    await writer.send(
                        jsonrpc.make_error(
                            message["id"], -32601, f"method not found: {method}"
                        )
                    )
                continue
            result = handler(message.get("params", {}))
            if isinstance(result, Awaitable):
                result = await result
            if "id" in message:
                await writer.send(jsonrpc.make_result(message["id"], result))
            if method == "shutdown":
                return

    @staticmethod
    async def _send_after(writer: QueueWriter, delay: float, notification: dict[str, Any]) -> None:
        await asyncio.sleep(delay)
        await writer.send(notification)


def stream_ready_race_fixture() -> ScriptedBackend:
    """Two playback.play calls (ids implied by caller) resolve out of order:
    the *first* play's stream becomes ready *after* the second's, proving a
    front discards a track/streamReady whose requestId no longer matches its
    latestRequestId. See docs/protocol.md §11.1.
    """
    play_ids: list[int] = []

    def handle_play(params: dict[str, Any]) -> dict[str, Any]:
        return {"accepted": True}

    async def emit_out_of_order(writer: QueueWriter) -> None:
        # second play's stream arrives first
        await asyncio.sleep(0.01)
        await writer.send(
            jsonrpc.make_notification(
                "track/streamReady",
                {"requestId": 2, "trackId": "second", "stream": {"kind": "url", "url": "https://second", "mimeType": "audio/mpeg"}},
            )
        )
        await asyncio.sleep(0.01)
        await writer.send(
            jsonrpc.make_notification(
                "track/streamReady",
                {"requestId": 1, "trackId": "first", "stream": {"kind": "url", "url": "https://first", "mimeType": "audio/mpeg"}},
            )
        )

    backend = ScriptedBackend(handlers={"playback.play": handle_play, "shutdown": lambda p: {}})
    original_serve = backend.serve

    async def serve_with_race(writer: QueueWriter, reader: QueueReader) -> None:
        asyncio.create_task(emit_out_of_order(writer))
        await original_serve(writer, reader)

    backend.serve = serve_with_race  # type: ignore[method-assign]
    return backend


SELF_PLAYBACK_CAPABILITIES = {
    "playback": {
        "providesStream": False,
        "selfPlayback": True,
        "controls": {"pause": True, "seek": True, "volume": True},
    },
    "browse": {"playlists": False, "likedTracks": False, "radio": False, "search": False},
    "feedback": {"like": False, "dislike": False, "skip": False},
    "download": False,
    "auth": {"required": False, "flow": "none"},
}


def self_playback_fixture() -> ScriptedBackend:
    """A synthetic selfPlayback=true backend, used only to unit-test the
    front's selfPlayback branch — no real v1 backend uses this capability.
    """
    state = {"state": "idle", "trackId": None, "positionMs": 0, "durationMs": 0}

    def handle_initialize(params: dict[str, Any]) -> dict[str, Any]:
        return {
            "protocolVersion": "1.0",
            "source": {"id": "fixture-self-playback", "name": "Fixture", "version": "0.0.0"},
            "capabilities": SELF_PLAYBACK_CAPABILITIES,
        }

    def handle_play(params: dict[str, Any]) -> dict[str, Any]:
        state.update(state="playing", trackId=params["trackId"], positionMs=0, durationMs=180000)
        return {"accepted": True}

    def handle_pause(params: dict[str, Any]) -> dict[str, Any]:
        state["state"] = "paused"
        return {}

    def handle_resume(params: dict[str, Any]) -> dict[str, Any]:
        state["state"] = "playing"
        return {}

    return ScriptedBackend(
        handlers={
            "initialize": handle_initialize,
            "playback.play": handle_play,
            "playback.pause": handle_pause,
            "playback.resume": handle_resume,
            "shutdown": lambda p: {},
        }
    )
