"""Per-backend translation of the strings a backend sends to a front
(docs/protocol.md §7.9).

Each backend owns its dictionaries: a Translator is built from
{language: {english text: translated text}} and handed to the
BackendServer (and the SettingsStore). The English text itself is the
key, so a missing entry — or an unsupported language — falls back to
English rather than failing. rpc_common ships no translations of its
own; the few strings it generates (settings validation messages) go
through the backend's Translator, so the backend's dictionaries cover
them.

The front's language arrives in initialize (`locale`) and later through
localization.setLanguage.
"""
from __future__ import annotations

from typing import Mapping

DEFAULT_LOCALE = "en"


def normalize_locale(locale: str) -> str:
    """'ru_RU.UTF-8' -> 'ru-RU'; anything unusable -> ''."""
    tag = locale.split(".", 1)[0].split("@", 1)[0].replace("_", "-").strip()
    parts = [p for p in tag.split("-") if p]
    if not parts or not parts[0].isalpha():
        return ""
    return "-".join([parts[0].lower(), *(p.upper() if len(p) == 2 else p.title() for p in parts[1:])])


class Translator:
    def __init__(self, catalogs: Mapping[str, Mapping[str, str]] | None = None):
        self._catalogs = {normalize_locale(lang): dict(c) for lang, c in (catalogs or {}).items()}
        self._locale = DEFAULT_LOCALE
        self._catalog: Mapping[str, str] = {}

    @property
    def locale(self) -> str:
        """The language in effect: a catalog's key, or 'en'."""
        return self._locale

    def locales(self) -> list[str]:
        """Languages this backend can answer in, English included."""
        return [DEFAULT_LOCALE, *sorted(self._catalogs)]

    def set_locale(self, locale: str | None) -> str:
        """Picks the best catalog for `locale` ('pt-BR' tries 'pt-BR', then
        'pt') and returns the language now in effect."""
        tag = normalize_locale(locale or "")
        for candidate in (tag, tag.split("-", 1)[0]):
            if candidate in self._catalogs:
                self._locale = candidate
                self._catalog = self._catalogs[candidate]
                return candidate
        self._locale = DEFAULT_LOCALE
        self._catalog = {}
        return DEFAULT_LOCALE

    def tr(self, text: str, **fmt: object) -> str:
        translated = self._catalog.get(text, text)
        return translated.format(**fmt) if fmt else translated
