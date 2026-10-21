"""Precomputes everything templates/rust/*.jinja need: per-struct field
records with their Rust type and serde attributes worked out in Python, so
the templates only loop and interpolate — same split as pyrender.py.

Validation is serde's own: a required field missing or a value of the wrong
JSON type fails deserialization. String enums stay `String` (pyrender checks
them by hand; a Rust enum would reject a value a newer front adds).
"""
from __future__ import annotations

import re
from dataclasses import dataclass

from schema_ir import MethodIR, NamedType, NotificationIR, ProtocolIR, TypeIR

_RUST_KEYWORDS = {
    "as", "break", "const", "continue", "crate", "else", "enum", "extern", "false", "fn", "for",
    "if", "impl", "in", "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return",
    "self", "static", "struct", "super", "trait", "true", "type", "unsafe", "use", "where",
    "while", "async", "await", "dyn", "abstract", "become", "box", "do", "final", "macro",
    "override", "priv", "typeof", "unsized", "virtual", "yield", "try",
}

_PRIMITIVE = {"string": "String", "integer": "i64", "number": "f64", "boolean": "bool"}


def _snake(s: str) -> str:
    return re.sub(r"(?<!^)(?=[A-Z])", "_", s).lower()


def _field_ident(name: str) -> str:
    ident = _snake(name)
    return f"{ident}_" if ident in _RUST_KEYWORDS else ident


def _const_name(wire_name: str) -> str:
    # "catalog.listTracks" -> "CATALOG_LIST_TRACKS"; "track/streamReady" -> "TRACK_STREAM_READY"
    parts = re.split(r"[./]", wire_name)
    return "_".join(_snake(p).upper() for p in parts)


def _rust_type(t: TypeIR) -> str:
    if t.kind in _PRIMITIVE:
        return _PRIMITIVE[t.kind]
    if t.kind == "object_ref":
        return t.ref_name
    if t.kind == "array":
        return f"Vec<{_rust_type(t.item)}>"
    if t.kind == "map":
        return f"BTreeMap<String, {_rust_type(t.map_value)}>"
    if t.kind in ("any", "empty"):
        return "Value"
    raise ValueError(f"no Rust type for kind {t.kind!r}")


@dataclass
class FieldRender:
    ident: str
    wire_name: str
    rust_type: str
    attrs: str
    enum_doc: str | None


@dataclass
class StructRender:
    name: str
    fields: list[FieldRender]


def render_struct(nt: NamedType) -> StructRender:
    fields: list[FieldRender] = []
    for f in nt.fields:
        rust_type = _rust_type(f.type)
        attrs = [f'rename = "{f.name}"']
        if not f.required:
            rust_type = f"Option<{rust_type}>"
            attrs += ["default", 'skip_serializing_if = "Option::is_none"']
        enum_doc = None
        if f.type.kind == "string" and f.type.enum:
            enum_doc = "One of: " + ", ".join(f"`{v}`" for v in f.type.enum) + "."
        fields.append(
            FieldRender(
                ident=_field_ident(f.name),
                wire_name=f.name,
                rust_type=rust_type,
                attrs=", ".join(attrs),
                enum_doc=enum_doc,
            )
        )
    return StructRender(name=nt.name, fields=fields)


@dataclass
class MethodRender:
    name: str
    const_name: str
    params_type: str | None
    result_type: str | None
    timeout_ms: int


@dataclass
class NotificationRender:
    name: str
    const_name: str
    params_type: str | None


def render_method(m: MethodIR) -> MethodRender:
    return MethodRender(
        name=m.name,
        const_name=_const_name(m.name),
        params_type=m.params_type,
        result_type=m.result_type,
        timeout_ms=m.timeout_ms,
    )


def render_notification(n: NotificationIR) -> NotificationRender:
    return NotificationRender(name=n.name, const_name=_const_name(n.name), params_type=n.params_type)


def render_context(ir: ProtocolIR) -> dict:
    return {
        "protocol_version": ir.protocol_version,
        "structs": [render_struct(nt) for nt in ir.ordered_named_types()],
        "methods": [render_method(m) for m in ir.methods],
        "notifications": [render_notification(n) for n in ir.notifications],
        "error_ranges": ir.error_ranges,
    }
