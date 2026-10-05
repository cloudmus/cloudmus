"""A backend's own settings (docs/protocol.md §7.7): declared once as Field
objects, stored in the backend's config.json, and served to fronts through
settings.describe / settings.update, which build their form from the
metadata alone.

Usage::

    store = SettingsStore(
        CONFIG_FILE,
        groups=[Group("playback", "Playback")],
        fields=[
            Field.enum("streamQuality", "Stream quality", default="best",
                       options=[("best", "Best available"), ("low", "Up to 128 kbps")],
                       group="playback"),
        ],
    )
    store.register(server)          # before server.run(); adds the two methods
    store.get("streamQuality")      # stored value, or the default

Values live under a "settings" key of the same config.json the backend may
already keep other state in (an auth token), which is left untouched. Only
values that differ from their default are written, so changing a default
in code reaches everyone who never picked one.
"""
from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

from .i18n import Translator
from .server import BackendError, BackendServer

INVALID_PARAMS = -32602

_SETTINGS_KEY = "settings"


@dataclass(frozen=True)
class Group:
    id: str
    title: str
    description: str | None = None

    def to_dict(self, tr: Callable[[str], str] = str) -> dict[str, Any]:
        d: dict[str, Any] = {"id": self.id, "title": tr(self.title)}
        if self.description:
            d["description"] = tr(self.description)
        return d


@dataclass(frozen=True)
class Field:
    key: str
    type: str
    label: str
    default: Any
    description: str | None = None
    group: str | None = None
    min: int | None = None
    max: int | None = None
    options: tuple[tuple[str, str], ...] = ()
    path_kind: str | None = None
    placeholder: str | None = None
    restart_required: bool = False

    @classmethod
    def boolean(cls, key: str, label: str, default: bool = False, **kw: Any) -> "Field":
        return cls(key, "boolean", label, default, **kw)

    @classmethod
    def integer(cls, key: str, label: str, default: int = 0, **kw: Any) -> "Field":
        return cls(key, "integer", label, default, **kw)

    @classmethod
    def string(cls, key: str, label: str, default: str = "", **kw: Any) -> "Field":
        return cls(key, "string", label, default, **kw)

    @classmethod
    def secret(cls, key: str, label: str, **kw: Any) -> "Field":
        return cls(key, "secret", label, "", **kw)

    @classmethod
    def enum(cls, key: str, label: str, default: str, options: list[tuple[str, str]], **kw: Any) -> "Field":
        return cls(key, "enum", label, default, options=tuple(options), **kw)

    @classmethod
    def path(cls, key: str, label: str, default: str = "", kind: str = "directory", **kw: Any) -> "Field":
        return cls(key, "path", label, default, path_kind=kind, **kw)

    def validate(self, value: Any, check_path_exists: bool = True, tr: Translator | None = None) -> str | None:
        """None if `value` is acceptable, else a message for the user.
        `check_path_exists=False` skips whether a path points at anything —
        for reading back what was once accepted: a folder on a drive that
        isn't mounted right now is still the user's choice. `tr` translates
        the message (English without one)."""
        t = (tr or Translator()).tr
        if self.type == "boolean":
            return None if isinstance(value, bool) else t("expected true or false")
        if self.type == "integer":
            # bool is an int subclass in Python; not a valid integer here.
            if not isinstance(value, int) or isinstance(value, bool):
                return t("expected a whole number")
            if self.min is not None and value < self.min:
                return t("must be at least {n}", n=self.min)
            if self.max is not None and value > self.max:
                return t("must be at most {n}", n=self.max)
            return None
        if not isinstance(value, str):
            return t("expected text")
        if self.type == "enum" and value not in {v for v, _ in self.options}:
            return t("must be one of: {options}", options=", ".join(v for v, _ in self.options))
        if self.type == "path" and value and check_path_exists:
            path = Path(value).expanduser()
            if self.path_kind == "directory" and not path.is_dir():
                return t("no such folder")
            if self.path_kind == "file" and not path.is_file():
                return t("no such file")
        return None

    def to_dict(self, value: Any, is_set: bool, tr: Callable[[str], str] = str) -> dict[str, Any]:
        d: dict[str, Any] = {
            "key": self.key,
            "label": tr(self.label),
            "type": self.type,
            "default": self.default,
            # A secret never leaves the backend — isSet says whether there is one.
            "value": "" if self.type == "secret" else value,
        }
        optional = {
            "group": self.group,
            "description": tr(self.description) if self.description is not None else None,
            "min": self.min,
            "max": self.max,
            "pathKind": self.path_kind,
            "placeholder": tr(self.placeholder) if self.placeholder is not None else None,
        }
        d.update({k: v for k, v in optional.items() if v is not None})
        if self.options:
            d["options"] = [{"value": v, "label": tr(label)} for v, label in self.options]
        if self.type == "secret":
            d["isSet"] = is_set
        if self.restart_required:
            d["restartRequired"] = True
        return d


@dataclass
class SettingsStore:
    config_file: Path
    fields: list[Field]
    groups: list[Group] = field(default_factory=list)
    # Called after settings.update saved, with {key: new value} for the
    # keys that changed — for applying what can be applied live.
    on_change: Callable[[dict[str, Any]], None] | None = None
    # Replaced by the server's own in register(), so labels and messages
    # follow the language the front asked for.
    translator: Translator = field(default_factory=Translator)

    def __post_init__(self) -> None:
        self._by_key = {f.key: f for f in self.fields}
        group_ids = {g.id for g in self.groups}
        for f in self.fields:
            if f.group is not None and f.group not in group_ids:
                raise ValueError(f"setting {f.key!r} names unknown group {f.group!r}")
            if f.validate(f.default) is not None and f.type != "path":
                raise ValueError(f"setting {f.key!r} has an invalid default {f.default!r}")

    def _load_config(self) -> dict[str, Any]:
        try:
            return json.loads(self.config_file.read_text(encoding="utf-8"))
        except FileNotFoundError:
            return {}

    def _stored(self) -> dict[str, Any]:
        stored = self._load_config().get(_SETTINGS_KEY)
        return stored if isinstance(stored, dict) else {}

    def get(self, key: str) -> Any:
        f = self._by_key[key]
        stored = self._stored()
        # A stored value that no longer validates (an option since removed,
        # a hand-edited wrong type) falls back to the default.
        if key in stored and f.validate(stored[key], check_path_exists=False) is None:
            return stored[key]
        return f.default

    def is_set(self, key: str) -> bool:
        return key in self._stored()

    def describe(self) -> dict[str, Any]:
        return {
            "groups": [g.to_dict(self.translator.tr) for g in self.groups],
            "fields": [f.to_dict(self.get(f.key), self.is_set(f.key), self.translator.tr) for f in self.fields],
        }

    def update(self, values: dict[str, Any]) -> dict[str, Any]:
        """Validates every value first, then saves them all — or raises
        BackendError(-32602) naming the first bad key and saves nothing.
        Returns {key: value} of what actually changed."""
        for key, value in values.items():
            f = self._by_key.get(key)
            if f is None:
                raise BackendError(
                    INVALID_PARAMS,
                    self.translator.tr("unknown setting: {key}", key=key),
                    {"key": key, "message": self.translator.tr("unknown setting")},
                )
            problem = f.validate(value, tr=self.translator)
            if problem is not None:
                raise BackendError(
                    INVALID_PARAMS, f"{self.translator.tr(f.label)}: {problem}", {"key": key, "message": problem}
                )

        config = self._load_config()
        stored = config.get(_SETTINGS_KEY)
        if not isinstance(stored, dict):
            stored = {}
        changed: dict[str, Any] = {}
        for key, value in values.items():
            f = self._by_key[key]
            if value != self.get(key) or f.type == "secret":
                changed[key] = value
            # A secret is removed by sending "", and other values equal to
            # the default aren't pinned (see the module docstring).
            if value == f.default:
                stored.pop(key, None)
            else:
                stored[key] = value
        config[_SETTINGS_KEY] = stored
        self.config_file.parent.mkdir(parents=True, exist_ok=True)
        self.config_file.write_text(json.dumps(config, indent=2, ensure_ascii=False), encoding="utf-8")
        # May hold secrets, like the tokens backends keep in the same file.
        self.config_file.chmod(0o600)

        if changed and self.on_change is not None:
            self.on_change(changed)
        return changed

    def register(self, server: BackendServer) -> None:
        server.capabilities["settings"] = True
        self.translator = server.translator

        @server.method("settings.describe")
        def handle_describe(params: dict, request_id: int) -> dict:
            return self.describe()

        @server.method("settings.update")
        def handle_update(params: dict, request_id: int) -> dict:
            values = params.get("values")
            if not isinstance(values, dict):
                raise BackendError(INVALID_PARAMS, self.translator.tr("values: expected an object"))
            self.update(values)
            return {}
