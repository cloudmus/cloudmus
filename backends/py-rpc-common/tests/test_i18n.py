import pytest

from rpc_common.i18n import Translator, normalize_locale
from rpc_common.server import BackendError, BackendServer
from rpc_common.settings import Field, Group, SettingsStore

CATALOGS = {
    "ru": {"Playback": "Воспроизведение", "Quality": "Качество", "must be at most {n}": "не больше {n}"},
    "pt": {"Playback": "Reprodução"},
}


@pytest.mark.parametrize(
    "raw, expected",
    [("ru_RU.UTF-8", "ru-RU"), ("PT-br", "pt-BR"), ("de@euro", "de"), ("", ""), ("C", "c"), ("1x", "")],
)
def test_normalize_locale(raw, expected):
    assert normalize_locale(raw) == expected


def test_fallback_chain_and_english_default():
    tr = Translator(CATALOGS)
    assert tr.tr("Playback") == "Playback"
    assert tr.set_locale("pt-BR") == "pt"
    assert tr.tr("Playback") == "Reprodução"
    assert tr.set_locale("ru_RU.UTF-8") == "ru"
    assert tr.tr("Unlisted") == "Unlisted"  # missing entry: English
    assert tr.tr("must be at most {n}", n=5) == "не больше 5"
    assert tr.set_locale("ja") == "en" and tr.tr("Playback") == "Playback"
    assert tr.locales() == ["en", "pt", "ru"]


def make_server(translator):
    return BackendServer("s", "S", "0", "A source", {"auth": {"required": False}}, translator)


def test_server_advertises_localization_and_switches():
    server = make_server(Translator(CATALOGS))
    assert server.capabilities["localization"] == {"locales": ["en", "pt", "ru"]}
    handler = server._handlers["localization.setLanguage"]
    assert handler({"locale": "ru"}, 1)["locale"] == "ru"
    assert handler({"locale": "xx"}, 2)["locale"] == "en"
    with pytest.raises(BackendError):
        handler({}, 3)


def test_set_language_returns_the_description_in_that_language():
    server = make_server(Translator({"ru": {"A source": "Источник"}}))
    handler = server._handlers["localization.setLanguage"]
    assert handler({"locale": "ru"}, 1)["description"] == "Источник"
    assert handler({"locale": "en"}, 2)["description"] == "A source"


def test_initialize_applies_locale_and_translates_description():
    server = make_server(Translator({"ru": {"A source": "Источник"}}))
    assert server._handle_initialize({"locale": "ru"}, 1)["source"]["description"] == "Источник"
    assert server._handle_initialize({}, 2)["source"]["description"] == "A source"


def test_settings_follow_the_servers_language(tmp_path):
    server = make_server(Translator(CATALOGS))
    store = SettingsStore(
        tmp_path / "c.json",
        groups=[Group("playback", "Playback")],
        fields=[Field.integer("volume", "Quality", default=1, max=9, group="playback")],
    )
    store.register(server)
    server.translator.set_locale("ru")
    description = store.describe()
    assert description["groups"][0]["title"] == "Воспроизведение"
    assert description["fields"][0]["label"] == "Качество"
    with pytest.raises(BackendError) as err:
        store.update({"volume": 10})
    assert err.value.message == "Качество: не больше 9"
    assert err.value.data["message"] == "не больше 9"
