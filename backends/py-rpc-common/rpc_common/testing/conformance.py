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


_SETTING_VALUE_TYPES = {
    "boolean": bool,
    "integer": int,
    "string": str,
    "secret": str,
    "enum": str,
    "path": str,
}


async def _check_settings(writer: Any, reader: Any, ids: Any, passed: list[str]) -> None:
    describe_id = next(ids)
    await writer.send(jsonrpc.make_request(describe_id, "settings.describe", {}))
    response = await reader.__anext__()
    if response.get("id") != describe_id or "result" not in response:
        raise ConformanceFailure(f"settings.describe failed: {response!r}")
    description = response["result"]
    _validate("settings_description.yaml", description)
    passed.append("settings.describe result matches schema")

    group_ids = {g["id"] for g in description["groups"]}
    keys: set[str] = set()
    for f in description["fields"]:
        where = f"setting {f['key']!r}"
        if f["key"] in keys:
            raise ConformanceFailure(f"{where} is listed twice")
        keys.add(f["key"])
        if "group" in f and f["group"] not in group_ids:
            raise ConformanceFailure(f"{where} names unknown group {f['group']!r}")
        expected = _SETTING_VALUE_TYPES[f["type"]]
        for name in ("default", "value"):
            v = f[name]
            if not isinstance(v, expected) or (expected is int and isinstance(v, bool)):
                raise ConformanceFailure(f"{where}: {name} {v!r} doesn't match type {f['type']}")
        if f["type"] == "enum":
            allowed = {o["value"] for o in f.get("options", [])}
            if not allowed:
                raise ConformanceFailure(f"{where}: an enum needs options")
            if f["default"] not in allowed or f["value"] not in allowed:
                raise ConformanceFailure(f"{where}: default/value not among its options")
        if f["type"] == "secret" and (f["value"] != "" or "isSet" not in f):
            raise ConformanceFailure(f"{where}: a secret must report value \"\" and isSet")
    passed.append("settings fields are consistent with their types")

    # Rejected without saving anything — this suite must not change the
    # settings of whoever runs it.
    update_id = next(ids)
    await writer.send(
        jsonrpc.make_request(update_id, "settings.update", {"values": {"conformance.noSuchKey": True}})
    )
    response = await reader.__anext__()
    if response.get("id") != update_id or response.get("error", {}).get("code") != -32602:
        raise ConformanceFailure(f"settings.update with an unknown key should fail with -32602: {response!r}")
    passed.append("settings.update rejects an unknown key")


async def _check_localization(writer: Any, reader: Any, ids: Any, caps: dict, passed: list[str]) -> None:
    locales = caps["localization"]["locales"]
    if "en" not in locales:
        raise ConformanceFailure(f"localization.locales must include en: {locales!r}")
    # An unsupported language is not an error; it answers in English.
    for requested in [*locales, "xx-YY"]:
        request_id = next(ids)
        await writer.send(jsonrpc.make_request(request_id, "localization.setLanguage", {"locale": requested}))
        response = await reader.__anext__()
        if response.get("id") != request_id or "result" not in response:
            raise ConformanceFailure(f"localization.setLanguage {requested!r} failed: {response!r}")
        applied = response["result"]["locale"]
        expected = requested if requested in locales else "en"
        if applied != expected:
            raise ConformanceFailure(f"localization.setLanguage {requested!r} applied {applied!r}, expected {expected!r}")
        if caps.get("settings"):
            describe_id = next(ids)
            await writer.send(jsonrpc.make_request(describe_id, "settings.describe", {}))
            response = await reader.__anext__()
            if response.get("id") != describe_id or "result" not in response:
                raise ConformanceFailure(f"settings.describe in {requested!r} failed: {response!r}")
            _validate("settings_description.yaml", response["result"])
    passed.append("localization.setLanguage switches among declared locales and falls back to en")


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

        if "localization" in caps:
            await _check_localization(writer, reader, ids, caps, passed)

        if caps.get("settings"):
            await _check_settings(writer, reader, ids, passed)

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
