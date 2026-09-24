"""Conformance suite: spawn any backend and verify it honors docs/protocol.md.

Runnable directly: `python -m rpc_common.testing.conformance <argv...>`
where <argv...> is the backend's own manifest "argv" (e.g. `python -m
cloudmus_backend_local`).
"""
from __future__ import annotations

import asyncio
import sys
from pathlib import Path
from typing import Any

import jsonschema
import yaml

from .. import jsonrpc, transport

# Schema now lives in the top-level protocol/ directory (not nested under any
# one backend's package) since it's consumed by Python backends, this suite,
# and (via protocol/codegen) non-Python fronts. Repo root is 5 parents up from
# this file: rpc_common/testing/conformance.py -> rpc_common -> py-rpc-common
# -> backends -> <repo root>.
SCHEMA_DIR = Path(__file__).resolve().parents[4] / "protocol" / "schema"


def _load_schema_store() -> dict[str, Any]:
    store: dict[str, Any] = {}
    for path in SCHEMA_DIR.glob("*.yaml"):
        schema = yaml.safe_load(path.read_text())
        store[schema["$id"]] = schema
    return store


def _validate(schema_id: str, instance: Any) -> None:
    store = _load_schema_store()
    schema = store[schema_id]
    resolver = jsonschema.RefResolver(base_uri="", referrer=schema, store=store)
    jsonschema.validate(instance=instance, schema=schema, resolver=resolver)


class ConformanceFailure(AssertionError):
    pass


async def run_conformance(argv: list[str]) -> list[str]:
    """Spawns the backend, drives it through the checks below, returns a list
    of human-readable descriptions of checks that passed. Raises
    ConformanceFailure (a subclass of AssertionError) on the first failure.
    """
    passed: list[str] = []
    proc = await asyncio.create_subprocess_exec(
        *argv,
        stdin=asyncio.subprocess.PIPE,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    assert proc.stdin is not None and proc.stdout is not None
    writer = transport.NdjsonWriter(proc.stdin)
    reader = transport.NdjsonReader(proc.stdout)
    ids = jsonrpc.id_generator()

    try:
        init_id = next(ids)
        await writer.send(
            jsonrpc.make_request(
                init_id,
                "initialize",
                {"protocolVersion": "1.1", "front": {"name": "conformance-suite", "version": "0.0.0"}},
            )
        )
        response = await reader.__anext__()
        if response.get("id") != init_id or "result" not in response:
            raise ConformanceFailure(f"initialize did not return a matching result: {response!r}")
        result = response["result"]
        _validate("initialize_result.yaml", result)
        passed.append("initialize result matches schema")

        caps = result["capabilities"]
        if caps["playback"]["providesStream"] and caps["playback"]["selfPlayback"]:
            raise ConformanceFailure("capabilities declare both providesStream and selfPlayback")
        passed.append("providesStream/selfPlayback are not both true")

        authenticated = not caps["auth"]["required"]
        if caps["auth"]["required"]:
            status_id = next(ids)
            await writer.send(jsonrpc.make_request(status_id, "auth.getStatus", {}))
            response = await reader.__anext__()
            if response.get("id") != status_id or "result" not in response:
                raise ConformanceFailure(f"auth.getStatus failed: {response!r}")
            status = response["result"]["status"]
            if status not in ("unauthenticated", "pending", "authenticated", "error"):
                raise ConformanceFailure(f"auth.getStatus returned invalid status: {response!r}")
            passed.append("auth.getStatus returns a valid status")
            authenticated = status == "authenticated"

        if caps["browse"]["playlists"] and authenticated:
            list_id = next(ids)
            await writer.send(jsonrpc.make_request(list_id, "catalog.listPlaylists", {}))
            response = await reader.__anext__()
            if response.get("id") != list_id or "result" not in response:
                raise ConformanceFailure(f"catalog.listPlaylists failed: {response!r}")
            for playlist in response["result"]["playlists"]:
                _validate("playlist.yaml", playlist)
            passed.append("catalog.listPlaylists results match schema")
        elif caps["browse"]["playlists"]:
            passed.append("catalog.listPlaylists skipped (backend requires auth, not authenticated)")

        shutdown_id = next(ids)
        await writer.send(jsonrpc.make_request(shutdown_id, "shutdown", {}))
        response = await reader.__anext__()
        if response.get("id") != shutdown_id or "result" not in response:
            raise ConformanceFailure(f"shutdown did not ack: {response!r}")
        passed.append("shutdown acked")
    finally:
        if proc.stdin is not None and not proc.stdin.is_closing():
            proc.stdin.close()
        try:
            await asyncio.wait_for(proc.wait(), timeout=5)
        except asyncio.TimeoutError:
            proc.kill()
            await proc.wait()

    return passed


def main() -> None:
    argv = sys.argv[1:]
    if not argv:
        print("usage: python -m rpc_common.testing.conformance <backend argv...>", file=sys.stderr)
        raise SystemExit(2)
    results = asyncio.run(run_conformance(argv))
    for line in results:
        print(f"PASS: {line}")


if __name__ == "__main__":
    main()
