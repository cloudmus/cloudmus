"""Precomputes everything templates/python/*.jinja need: per-class field
records with the actual generated validation code (as ready-to-embed,
already-indented text blocks) worked out in Python, so the Jinja templates
only have to loop and interpolate — no type-dispatch logic lives in the
templates themselves.
"""
from __future__ import annotations

import re
from dataclasses import dataclass

from schema_ir import MethodIR, NamedType, NotificationIR, ProtocolIR, TypeIR


def _snake(s: str) -> str:
    return re.sub(r"(?<!^)(?=[A-Z])", "_", s).lower()


def method_py_name(method_name: str) -> str:
    # "catalog.listTracks" -> "catalog_list_tracks"
    namespace, _, tail = method_name.partition(".")
    return f"{namespace}_{_snake(tail)}"


def notification_py_name(notif_name: str) -> str:
    # "track/streamReady" -> "track_stream_ready"; "error" -> "error"
    namespace, sep, tail = notif_name.partition("/")
    if not sep:
        return _snake(namespace)
    return f"{namespace}_{_snake(tail)}"

_PRIMITIVE_ANN = {"string": "str", "integer": "int", "number": "float", "boolean": "bool"}

_BODY_INDENT = 8  # inside `def from_dict(cls, d):` (4 for def body + 4 for method-in-class)


def _annotation(t: TypeIR) -> str:
    if t.kind in _PRIMITIVE_ANN:
        return _PRIMITIVE_ANN[t.kind]
    if t.kind == "object_ref":
        return t.ref_name
    if t.kind == "array":
        return f"list[{_annotation(t.item)}]"
    if t.kind == "map":
        return f"dict[str, {_annotation(t.map_value)}]"
    if t.kind == "any":
        return "Any"
    raise ValueError(f"no Python annotation for kind {t.kind!r}")


def _check_expr(t: TypeIR, var: str, path: str, depth: int) -> tuple[list[str], str]:
    """Returns (validation_lines, value_expr), lines with no baked-in
    indentation (relative nesting only) — caller re-indents as a block."""
    if t.kind == "string":
        lines = [
            f"if not isinstance({var}, str):",
            f'    raise ProtocolValidationError("{path}: expected string, got " + type({var}).__name__)',
        ]
        if t.enum:
            lines += [
                f"if {var} not in {t.enum!r}:",
                f'    raise ProtocolValidationError(f"{path}: {{{var}!r}} is not one of {t.enum!r}")',
            ]
        return lines, var
    if t.kind == "integer":
        return (
            [
                f"if not isinstance({var}, int) or isinstance({var}, bool):",
                f'    raise ProtocolValidationError("{path}: expected integer, got " + type({var}).__name__)',
            ],
            var,
        )
    if t.kind == "number":
        return (
            [
                f"if not isinstance({var}, (int, float)) or isinstance({var}, bool):",
                f'    raise ProtocolValidationError("{path}: expected number, got " + type({var}).__name__)',
            ],
            var,
        )
    if t.kind == "boolean":
        return (
            [
                f"if not isinstance({var}, bool):",
                f'    raise ProtocolValidationError("{path}: expected boolean, got " + type({var}).__name__)',
            ],
            var,
        )
    if t.kind == "object_ref":
        lines = [
            f"if not isinstance({var}, dict):",
            f'    raise ProtocolValidationError("{path}: expected object, got " + type({var}).__name__)',
        ]
        return lines, f"{t.ref_name}.from_dict({var})"
    if t.kind == "any":
        return [], var
    if t.kind == "array":
        item_var = f"_item{depth}"
        out_var = f"_list{depth}"
        item_lines, item_expr = _check_expr(t.item, item_var, path + "[]", depth + 1)
        lines = [
            f"if not isinstance({var}, list):",
            f'    raise ProtocolValidationError("{path}: expected array, got " + type({var}).__name__)',
            f"{out_var} = []",
            f"for {item_var} in {var}:",
        ]
        lines += [f"    {line}" for line in item_lines]
        lines.append(f"    {out_var}.append({item_expr})")
        return lines, out_var
    if t.kind == "map":
        val_var = f"_mval{depth}"
        out_var = f"_map{depth}"
        val_lines, val_expr = _check_expr(t.map_value, val_var, path + "{}", depth + 1)
        lines = [
            f"if not isinstance({var}, dict):",
            f'    raise ProtocolValidationError("{path}: expected object, got " + type({var}).__name__)',
            f"{out_var} = {{}}",
            f"for _mkey{depth}, {val_var} in {var}.items():",
        ]
        lines += [f"    {line}" for line in val_lines]
        lines.append(f"    {out_var}[_mkey{depth}] = {val_expr}")
        return lines, out_var
    raise ValueError(f"no Python validation for kind {t.kind!r}")


def _to_dict_expr(t: TypeIR, expr: str, depth: int = 0) -> str:
    if t.kind in _PRIMITIVE_ANN or t.kind == "any" or t.kind == "map":
        return expr
    if t.kind == "object_ref":
        return f"{expr}.to_dict()"
    if t.kind == "array":
        item_var = f"_x{depth}"
        return f"[{_to_dict_expr(t.item, item_var, depth + 1)} for {item_var} in {expr}]"
    raise ValueError(f"no to_dict expr for kind {t.kind!r}")


def _indent_block(lines: list[str], indent: int) -> str:
    pad = " " * indent
    return "\n".join(pad + line for line in lines)


@dataclass
class FieldRender:
    name: str
    decl_line: str  # e.g. "coverUrl: str | None = None"
    to_dict_line: str  # e.g. 'd["coverUrl"] = self.coverUrl if self.coverUrl is not None else None'
    from_dict_block: str  # fully indented statements assigning a local _v_<name>


@dataclass
class ClassRender:
    name: str
    fields: list[FieldRender]


def render_class(nt: NamedType) -> ClassRender:
    # Dataclasses require fields without defaults before fields with
    # defaults: required fields must come first.
    ordered = sorted(nt.fields, key=lambda f: not f.required)
    fields: list[FieldRender] = []
    for f in ordered:
        ann = _annotation(f.type)
        path = f"{nt.name}.{f.name}"
        name_repr = repr(f.name)

        if f.required:
            decl_line = f"{f.name}: {ann}"
            to_dict_line = f'd[{name_repr}] = {_to_dict_expr(f.type, f"self.{f.name}")}'
            check_lines, value_expr = _check_expr(f.type, f"d[{name_repr}]", path, depth=0)
            body = [f"if {name_repr} not in d:", f'    raise ProtocolValidationError("{path} is required")']
            body += check_lines
            body += [f"_v_{f.name} = {value_expr}"]
        else:
            decl_line = f"{f.name}: {ann} | None = None"
            to_dict_line = (
                f'd[{name_repr}] = {_to_dict_expr(f.type, f"self.{f.name}")} '
                f"if self.{f.name} is not None else None"
            )
            check_lines, value_expr = _check_expr(f.type, f"_raw_{f.name}", path, depth=0)
            body = [f"_raw_{f.name} = d.get({name_repr})", f"if _raw_{f.name} is not None:"]
            body += [f"    {line}" for line in check_lines]
            body += [f"    _v_{f.name} = {value_expr}", "else:", f"    _v_{f.name} = None"]

        fields.append(
            FieldRender(
                name=f.name,
                decl_line=decl_line,
                to_dict_line=to_dict_line,
                from_dict_block=_indent_block(body, _BODY_INDENT),
            )
        )
    return ClassRender(name=nt.name, fields=fields)


@dataclass
class MethodRender:
    name: str
    py_name: str
    params_type: str | None
    result_type: str | None
    requires_capability: str | None
    timeout_ms: int


@dataclass
class NotificationRender:
    name: str
    py_name: str
    params_type: str | None


def render_method(m: MethodIR) -> MethodRender:
    return MethodRender(
        name=m.name,
        py_name=method_py_name(m.name),
        params_type=m.params_type,
        result_type=m.result_type,
        requires_capability=m.requires_capability,
        timeout_ms=m.timeout_ms,
    )


def render_notification(n: NotificationIR) -> NotificationRender:
    return NotificationRender(name=n.name, py_name=notification_py_name(n.name), params_type=n.params_type)


def render_context(ir: ProtocolIR) -> dict:
    classes = [render_class(nt) for nt in ir.ordered_named_types()]
    return {
        "protocol_version": ir.protocol_version,
        "classes": classes,
        "methods": [render_method(m) for m in ir.methods],
        "notifications": [render_notification(n) for n in ir.notifications],
    }
