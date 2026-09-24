"""JSON-RPC 2.0 envelope helpers. See docs/protocol.md §3."""
from __future__ import annotations

import itertools
from typing import Any, Iterator


def id_generator(start: int = 1) -> Iterator[int]:
    """An unbounded sequence of request ids, unique within one connection."""
    return itertools.count(start)


def make_request(id: int, method: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": id, "method": method, "params": params or {}}


def make_notification(method: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "method": method, "params": params or {}}


def make_result(id: int, result: dict[str, Any]) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": id, "result": result}


def make_error(
    id: int, code: int, message: str, data: dict[str, Any] | None = None
) -> dict[str, Any]:
    error: dict[str, Any] = {"code": code, "message": message}
    if data is not None:
        error["data"] = data
    return {"jsonrpc": "2.0", "id": id, "error": error}


def is_request(message: dict[str, Any]) -> bool:
    return "method" in message and "id" in message


def is_notification(message: dict[str, Any]) -> bool:
    return "method" in message and "id" not in message


def is_response(message: dict[str, Any]) -> bool:
    return "id" in message and ("result" in message or "error" in message)
