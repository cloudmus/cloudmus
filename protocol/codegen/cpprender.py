"""Precomputes everything templates/cpp/*.jinja need. Generated fromJson/
toJson bodies are single calls (qualified as Rpc::...) into the hand-written
helper templates in fronts/qt/src/Rpc/JsonValidation.h
(Rpc::requiredField<T>/Rpc::optionalField<T>/Rpc::requiredArray<T>/
Rpc::requiredMap<T>/... — see that header) rather than inlined per-field
validation blocks, to keep generated output small; the actual validation
logic lives once, in that header, not repeated per field/struct.
"""
from __future__ import annotations

import re
from dataclasses import dataclass

from schema_ir import NamedType, ProtocolIR, TypeIR

_PRIMITIVE_CPP = {"string": "QString", "integer": "int", "number": "double", "boolean": "bool"}

# C++20 keywords/reserved identifiers that collide with a protocol field name
# ("explicit" is the only real one in this protocol, but keep the check
# general so a future schema addition doesn't silently generate invalid C++).
_CPP_KEYWORDS = {
    "alignas", "alignof", "and", "and_eq", "asm", "atomic_cancel", "atomic_commit",
    "atomic_noexcept", "auto", "bitand", "bitor", "bool", "break", "case", "catch",
    "char", "char8_t", "char16_t", "char32_t", "class", "compl", "concept", "const",
    "consteval", "constexpr", "constinit", "const_cast", "continue", "co_await",
    "co_return", "co_yield", "decltype", "default", "delete", "do", "double",
    "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "float",
    "for", "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace",
    "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq",
    "private", "protected", "public", "reflexpr", "register", "reinterpret_cast",
    "requires", "return", "short", "signed", "sizeof", "static", "static_assert",
    "static_cast", "struct", "switch", "synchronized", "template", "this",
    "thread_local", "throw", "true", "try", "typedef", "typeid", "typename",
    "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t",
    "while", "xor", "xor_eq",
}


def member_name(field_name: str) -> str:
    """The protocol field name doubles as the JSON key everywhere except when
    it collides with a C++ keyword (only "explicit" in this protocol today);
    in that case the struct member gets a trailing underscore, and fromJson/
    toJson still key off the original wire name."""
    return f"{field_name}_" if field_name in _CPP_KEYWORDS else field_name


def _cpp_type(t: TypeIR) -> str:
    if t.kind in _PRIMITIVE_CPP:
        return _PRIMITIVE_CPP[t.kind]
    if t.kind == "object_ref":
        return t.ref_name
    if t.kind == "array":
        # QList, not std::vector: this codebase is Qt-only (see AGENTS.md),
        # and QList integrates directly with the rest of the hand-written
        # Qt code (models, signals/slots) without extra conversions.
        return f"QList<{_cpp_type(t.item)}>"
    if t.kind == "map":
        return f"QMap<QString, {_cpp_type(t.map_value)}>"
    if t.kind == "any":
        return "QJsonValue"
    raise ValueError(f"no C++ type for kind {t.kind!r}")


def _from_json_call(t: TypeIR, class_name: str, wire_name: str, required: bool) -> str:
    prefix = "required" if required else "optional"
    if t.kind == "string" and t.enum:
        allowed = ", ".join(f'"{v}"' for v in t.enum)
        fn = f"Rpc::{prefix}EnumField"
        return f'{fn}(obj, "{class_name}", "{wire_name}", {{{allowed}}})'
    if t.kind == "array":
        if not required:
            raise ValueError(f"optional array field not supported by JsonValidation.h yet: {class_name}.{wire_name}")
        return f'Rpc::requiredArray<{_cpp_type(t.item)}>(obj, "{class_name}", "{wire_name}")'
    if t.kind == "map":
        fn = f"Rpc::{prefix}Map"
        return f'{fn}<{_cpp_type(t.map_value)}>(obj, "{class_name}", "{wire_name}")'
    # string/integer/number/boolean/object_ref/any all funnel through the
    # same JsonField<T>-driven helper.
    fn = f"Rpc::{prefix}Field"
    return f'{fn}<{_cpp_type(t)}>(obj, "{class_name}", "{wire_name}")'


def _to_json_line(t: TypeIR, member_expr: str, wire_name: str, required: bool) -> str:
    if required:
        if t.kind == "array":
            return f'out.insert("{wire_name}", Rpc::toJsonArray({member_expr}));'
        if t.kind == "map":
            return f'out.insert("{wire_name}", Rpc::toJsonMap({member_expr}));'
        return f'out.insert("{wire_name}", Rpc::toJsonValue({member_expr}));'
    if t.kind == "map":
        return f'Rpc::insertOptionalMap(out, "{wire_name}", {member_expr});'
    if t.kind == "array":
        raise ValueError(f"optional array field not supported by JsonValidation.h yet ({wire_name})")
    return f'Rpc::insertOptional(out, "{wire_name}", {member_expr});'


@dataclass
class FieldRender:
    name: str  # wire name (JSON key)
    member: str  # C++ member name (may differ if `name` is a keyword)
    decl_line: str
    from_json_line: str  # single statement assigning out.<member>
    to_json_line: str  # single statement inserting into `out`


@dataclass
class StructRender:
    name: str
    fields: list[FieldRender]


def render_struct(nt: NamedType) -> StructRender:
    fields: list[FieldRender] = []
    for f in nt.fields:
        cpp_t = _cpp_type(f.type)
        member = member_name(f.name)

        if f.required:
            decl_line = f"{cpp_t} {member};"
        else:
            decl_line = f"std::optional<{cpp_t}> {member};"

        from_json_call = _from_json_call(f.type, nt.name, f.name, f.required)
        from_json_line = f"out.{member} = {from_json_call};"

        to_json_line = _to_json_line(f.type, f"value.{member}", f.name, f.required)

        fields.append(
            FieldRender(
                name=f.name,
                member=member,
                decl_line=decl_line,
                from_json_line=from_json_line,
                to_json_line=to_json_line,
            )
        )
    return StructRender(name=nt.name, fields=fields)


def _pascal(s: str) -> str:
    return "".join(p[:1].upper() + p[1:] for p in re.split(r"[_\s]+", s) if p)


def _method_cpp_name(method_name: str) -> str:
    # "catalog.listTracks" -> "catalogListTracks"; "auth.cancel" -> "authCancel"
    # (namespace-qualified to avoid collisions, e.g. playback.cancel vs auth.cancel)
    namespace, _, tail = method_name.partition(".")
    return namespace + _pascal(tail)


def _notification_struct_name(notif_name: str) -> str:
    # "track/streamReady" -> "TrackStreamReady"; "error" -> "Error"
    namespace, sep, tail = notif_name.partition("/")
    if not sep:
        return _pascal(namespace)
    return _pascal(namespace) + _pascal(tail)


@dataclass
class MethodRender:
    name: str
    cpp_name: str
    params_type: str | None
    result_type: str | None
    requires_capability: str | None
    timeout_ms: int


@dataclass
class NotificationRender:
    name: str
    struct_name: str  # PascalCase identifier derived from the notification name
    params_type: str | None


def render_context(ir: ProtocolIR) -> dict:
    structs = [render_struct(nt) for nt in ir.ordered_named_types()]
    methods = [
        MethodRender(
            name=m.name,
            cpp_name=_method_cpp_name(m.name),
            params_type=m.params_type,
            result_type=m.result_type,
            requires_capability=m.requires_capability,
            timeout_ms=m.timeout_ms,
        )
        for m in ir.methods
    ]
    notifications = [
        NotificationRender(name=n.name, struct_name=_notification_struct_name(n.name), params_type=n.params_type)
        for n in ir.notifications
    ]
    return {
        "protocol_version": ir.protocol_version,
        "structs": structs,
        "methods": methods,
        "notifications": notifications,
    }
