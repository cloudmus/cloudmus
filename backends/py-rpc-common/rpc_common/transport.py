"""NDJSON transport for the cloudmus front/backend JSON-RPC protocol.

One JSON value per line, compact serialization, UTF-8, no BOM. See
docs/protocol.md §2 for the normative framing rules this module implements.
"""
from __future__ import annotations

import asyncio
import json
from typing import Any


class FramingError(Exception):
    """A line on the wire failed to parse as a single JSON object."""


class NdjsonWriter:
    """Serializes JSON-RPC messages compactly, one per line, to an asyncio StreamWriter.

    A single asyncio.Lock guards writes so concurrent coroutines never
    interleave partial lines onto the wire (docs/protocol.md §5).
    """

    def __init__(self, stream: asyncio.StreamWriter):
        self._stream = stream
        self._lock = asyncio.Lock()

    async def send(self, message: dict[str, Any]) -> None:
        line = json.dumps(message, separators=(",", ":"), ensure_ascii=False)
        async with self._lock:
            self._stream.write(line.encode("utf-8"))
            self._stream.write(b"\n")
            await self._stream.drain()


class NdjsonReader:
    """Reads NDJSON lines from an asyncio StreamReader, yielding parsed JSON objects.

    Blank lines are tolerated and skipped (a conformant writer never emits
    one, but a reader must not choke on one). EOF ends iteration.
    """

    def __init__(self, stream: asyncio.StreamReader):
        self._stream = stream

    def __aiter__(self) -> "NdjsonReader":
        return self

    async def __anext__(self) -> dict[str, Any]:
        while True:
            raw = await self._stream.readline()
            if raw == b"":
                raise StopAsyncIteration
            line = raw.decode("utf-8").strip()
            if not line:
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError as exc:
                raise FramingError(f"non-JSON line on wire: {line!r}") from exc
            if not isinstance(value, dict):
                raise FramingError(f"line is not a JSON object: {line!r}")
            return value


async def open_stdio(rpc_out: Any) -> tuple["NdjsonReader", "NdjsonWriter"]:
    """Wires real stdin/stdout (rpc_out is the pre-redirect stdout captured by
    install_stdout_purity_guard) into asyncio streams and wraps them as an
    NdjsonReader/NdjsonWriter pair. Call once, after installing the guard.
    """
    import sys

    loop = asyncio.get_event_loop()

    reader = asyncio.StreamReader()
    reader_protocol = asyncio.StreamReaderProtocol(reader)
    await loop.connect_read_pipe(lambda: reader_protocol, sys.stdin)

    write_transport, write_protocol = await loop.connect_write_pipe(
        lambda: asyncio.streams.FlowControlMixin(loop=loop), rpc_out
    )
    writer_stream = asyncio.StreamWriter(write_transport, write_protocol, None, loop)

    return NdjsonReader(reader), NdjsonWriter(writer_stream)


def install_stdout_purity_guard() -> Any:
    """Redirects sys.stdout to sys.stderr and returns the real stdout stream.

    Must be called before importing anything that might print to stdout.
    Only the caller's RPC writer should ever touch the returned stream.
    See docs/protocol.md §2.
    """
    import logging
    import sys

    rpc_out = sys.stdout
    sys.stdout = sys.stderr
    logging.basicConfig(stream=sys.stderr, level=logging.INFO)
    return rpc_out
