"""Every string this backend's server exposes has an entry in
each language's dictionary (a missing one would silently show English)."""
import pytest

from cloudmus_backend_ytmusic import server
from cloudmus_backend_ytmusic.i18n import translator
from cloudmus_backend_ytmusic.locales import CATALOGS


@pytest.mark.parametrize("lang", sorted(CATALOGS))
def test_dictionary_covers_description_and_titles(lang):
    built = server.build_server()
    expected = [built.source_description, "Liked Songs"]
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
    assert handler({"locale": "ru"}, 1) == {"locale": "ru"}
    assert built.translator is translator
    assert translator.tr("unknown setting") == CATALOGS["ru"]["unknown setting"]
    handler({"locale": "en"}, 2)
