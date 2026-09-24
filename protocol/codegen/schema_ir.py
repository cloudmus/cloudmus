"""Builds a small language-agnostic intermediate representation (IR) of the
cloudmus protocol from protocol/schema/*.yaml + protocol/methods.yaml, for
generate.py to render into per-language stubs.

Only the constructs this protocol actually uses are supported: string /
integer / number / boolean, arrays, `$ref` to a named schema file, inline
objects with fixed properties (needing an explicit `x-name` if they're
nested/anonymous), a bare `{}` ("no fields" placeholder used for empty
params/results), and additionalProperties-based maps / free-form objects.
This is deliberately not a general JSON-Schema-to-code engine.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml

PROTOCOL_DIR = Path(__file__).resolve().parent.parent
SCHEMA_DIR = PROTOCOL_DIR / "schema"
METHODS_FILE = PROTOCOL_DIR / "methods.yaml"


def _load_yaml(path: Path) -> Any:
    return yaml.safe_load(path.read_text())


def _pascal(s: str) -> str:
    """'listTracks' -> 'ListTracks', 'stream_descriptor' -> 'StreamDescriptor',
    'streamReady' -> 'StreamReady'."""
    parts = re.split(r"[_\s]+", s)
    out = []
    for part in parts:
        if not part:
            continue
        out.append(part[0].upper() + part[1:])
    return "".join(out)


def _method_type_basename(method_name: str) -> str:
    # "catalog.listTracks" -> "ListTracks"; "auth.getStatus" -> "GetStatus"
    tail = method_name.rsplit(".", 1)[-1]
    return _pascal(tail)


def _notification_type_basename(notif_name: str) -> str:
    # "track/streamReady" -> "StreamReady"; "error" -> "Error"
    tail = notif_name.rsplit("/", 1)[-1]
    return _pascal(tail)


# --- IR ---


@dataclass
class TypeIR:
    kind: str  # string|integer|number|boolean|array|object_ref|map|any|empty
    item: "TypeIR | None" = None  # for array
    ref_name: str | None = None  # for object_ref
    enum: list[str] | None = None  # for string enum
    map_value: "TypeIR | None" = None  # for map


@dataclass
class FieldIR:
    name: str
    required: bool
    type: TypeIR


@dataclass
class NamedType:
    name: str
    fields: list[FieldIR]


@dataclass
class MethodIR:
    name: str
    params_type: str | None  # None means "no params"
    result_type: str | None  # None means "no result fields" ({} ack)
    timeout_ms: int
    requires_capability: str | None


@dataclass
class NotificationIR:
    name: str
    params_type: str | None


@dataclass
class ErrorRange:
    min: int
    max: int
    name: str
    description: str


@dataclass
class ProtocolIR:
    protocol_version: str
    named_types: dict[str, NamedType] = field(default_factory=dict)  # dependency-ordered (dict preserves insertion order)
    methods: list[MethodIR] = field(default_factory=list)
    notifications: list[NotificationIR] = field(default_factory=list)
    error_ranges: list[ErrorRange] = field(default_factory=list)

    def ordered_named_types(self) -> list[NamedType]:
        return list(self.named_types.values())


class _Builder:
    def __init__(self) -> None:
        self._schema_cache: dict[str, Any] = {}
        self.named_types: dict[str, NamedType] = {}

    def _schema(self, filename: str) -> Any:
        if filename not in self._schema_cache:
            self._schema_cache[filename] = _load_yaml(SCHEMA_DIR / filename)
        return self._schema_cache[filename]

    def resolve(self, node: dict[str, Any], name_hint: str | None = None) -> TypeIR:
        if "$ref" in node:
            ref = node["$ref"]
            filename = ref.rsplit("/", 1)[-1]  # "schema/track.yaml" -> "track.yaml"
            ref_node = self._schema(filename)
            type_name = _pascal(Path(filename).stem)
            if type_name not in self.named_types:
                self.resolve(ref_node, name_hint=type_name)
            return TypeIR(kind="object_ref", ref_name=type_name)

        node_type = node.get("type")

        if node_type == "string":
            return TypeIR(kind="string", enum=node.get("enum"))
        if node_type == "integer":
            return TypeIR(kind="integer")
        if node_type == "number":
            return TypeIR(kind="number")
        if node_type == "boolean":
            return TypeIR(kind="boolean")
        if node_type == "array":
            item_hint = name_hint[:-1] if name_hint and name_hint.endswith("s") else name_hint
            item = self.resolve(node["items"], name_hint=item_hint)
            return TypeIR(kind="array", item=item)
        if node_type == "object":
            has_props_key = "properties" in node
            additional = node.get("additionalProperties")
            if not has_props_key and isinstance(additional, dict):
                return TypeIR(kind="map", map_value=self.resolve(additional))
            if not has_props_key and additional is True:
                return TypeIR(kind="any")
            props: dict[str, Any] = node.get("properties") or {}
            if not props:
                return TypeIR(kind="empty")

            type_name = node.get("x-name") or name_hint
            if not type_name:
                raise ValueError(f"inline object needs x-name or a name hint: {node!r}")
            if type_name in self.named_types:
                return TypeIR(kind="object_ref", ref_name=type_name)

            required = set(node.get("required", []))
            fields: list[FieldIR] = []
            for prop_name, prop_node in props.items():
                child_hint = _pascal(prop_name)
                field_type = self.resolve(prop_node, name_hint=child_hint)
                fields.append(FieldIR(name=prop_name, required=prop_name in required, type=field_type))
            # Insert after resolving children so dependencies precede this
            # type in iteration order (dict preserves insertion order).
            self.named_types[type_name] = NamedType(name=type_name, fields=fields)
            return TypeIR(kind="object_ref", ref_name=type_name)

        raise ValueError(f"unsupported schema node: {node!r}")

    def resolve_top_level(self, node: dict[str, Any], synthesized_name: str) -> str | None:
        """Like resolve(), but for a method/notification's params or result
        object: returns the type name to use (registering a synthesized named
        type if the node is an inline object), or None if it has no fields
        ("empty" placeholder — the method takes/returns no data)."""
        type_ir = self.resolve(node, name_hint=synthesized_name)
        if type_ir.kind == "empty":
            return None
        if type_ir.kind == "object_ref":
            return type_ir.ref_name
        raise ValueError(f"method/notification params or result must be an object, got {type_ir.kind}: {node!r}")


def build() -> ProtocolIR:
    methods_doc = _load_yaml(METHODS_FILE)
    builder = _Builder()
    ir = ProtocolIR(protocol_version=methods_doc["protocolVersion"])

    for method_name, spec in methods_doc.get("methods", {}).items():
        basename = _method_type_basename(method_name)
        params_type = builder.resolve_top_level(spec["params"], f"{basename}Params")
        result_type = builder.resolve_top_level(spec["result"], f"{basename}Result")
        ir.methods.append(
            MethodIR(
                name=method_name,
                params_type=params_type,
                result_type=result_type,
                timeout_ms=spec["timeoutMs"],
                requires_capability=spec.get("requiresCapability"),
            )
        )

    for notif_name, spec in methods_doc.get("notifications", {}).items():
        basename = _notification_type_basename(notif_name)
        params_type = builder.resolve_top_level(spec["params"], f"{basename}Params")
        ir.notifications.append(NotificationIR(name=notif_name, params_type=params_type))

    for r in methods_doc.get("errors", {}).get("ranges", []):
        ir.error_ranges.append(ErrorRange(min=r["min"], max=r["max"], name=r["name"], description=r["description"]))

    ir.named_types = builder.named_types
    return ir
