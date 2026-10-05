"""Every string this backend's settings and server expose has an entry in
each language's dictionary (a missing one would silently show English)."""
import pytest

from cloudmus_backend_local import config, server
from cloudmus_backend_local.i18n import translator
from cloudmus_backend_local.locales import CATALOGS


def _settings_strings():
    store = config.settings_store()
    for g in store.groups:
        yield g.title
        if g.description:
            yield g.description
    for f in store.fields:
        for text in (f.label, f.description, f.placeholder):
            if text:
                yield text
        for _, label in f.options:
            yield label


@pytest.mark.parametrize("lang", sorted(CATALOGS))
def test_dictionary_covers_settings_and_description(lang):
    built = server.build_server()
    expected = [built.source_description, *_settings_strings()]
    missing = [text for text in expected if text not in CATALOGS[lang]]
    assert missing == []


def test_languages_have_the_same_entries():
    assert {lang: set(c) for lang, c in CATALOGS.items()} == {
        lang: set(CATALOGS["ru"]) for lang in CATALOGS
    }
    assert sorted(CATALOGS) == ["be", "de", "es", "fr", "it", "ru"]


def test_server_switches_language():
    built = server.build_server()
    handler = built._handlers["localization.setLanguage"]
    assert handler({"locale": "ru"}, 1)["locale"] == "ru"
    assert built.translator is translator
    assert translator.tr("unknown setting") == CATALOGS["ru"]["unknown setting"]
    handler({"locale": "en"}, 2)
