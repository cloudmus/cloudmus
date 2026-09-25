import json
import stat

import pytest

from rpc_common import models
from rpc_common.server import BackendError, BackendServer
from rpc_common.settings import Field, Group, SettingsStore


def make_store(tmp_path, on_change=None):
    return SettingsStore(
        tmp_path / "config.json",
        groups=[Group("playback", "Playback")],
        fields=[
            Field.enum(
                "quality", "Quality", default="best",
                options=[("best", "Best"), ("low", "Low")], group="playback",
            ),
            Field.integer("volume", "Volume", default=50, min=0, max=100),
            Field.boolean("gapless", "Gapless", default=True),
            Field.path("musicDir", "Music folder", default=str(tmp_path)),
            Field.secret("apiKey", "API key"),
        ],
        on_change=on_change,
    )


def test_describe_matches_the_protocol_schema(tmp_path):
    description = make_store(tmp_path).describe()
    parsed = models.SettingsDescription.from_dict(description)
    assert [g.id for g in parsed.groups] == ["playback"]
    quality = parsed.fields[0]
    assert quality.type == "enum" and quality.value == "best" and quality.group == "playback"
    assert [o.value for o in quality.options] == ["best", "low"]


def test_update_stores_only_non_defaults_next_to_other_config(tmp_path):
    config_file = tmp_path / "config.json"
    config_file.write_text(json.dumps({"token": "t"}))
    store = make_store(tmp_path)

    assert store.update({"quality": "low", "volume": 50}) == {"quality": "low"}
    assert store.get("quality") == "low"
    saved = json.loads(config_file.read_text())
    assert saved == {"token": "t", "settings": {"quality": "low"}}
    assert stat.S_IMODE(config_file.stat().st_mode) == 0o600

    store.update({"quality": "best"})  # back to the default: unpinned
    assert json.loads(config_file.read_text())["settings"] == {}


@pytest.mark.parametrize(
    "values",
    [
        {"quality": "medium"},
        {"volume": 101},
        {"volume": True},
        {"gapless": "yes"},
        {"musicDir": "/no/such/folder"},
        {"nope": 1},
    ],
)
def test_update_rejects_bad_values(tmp_path, values):
    with pytest.raises(BackendError) as info:
        make_store(tmp_path).update(values)
    assert info.value.code == -32602
    assert info.value.data["key"] == next(iter(values))


def test_update_is_all_or_nothing(tmp_path):
    store = make_store(tmp_path)
    with pytest.raises(BackendError):
        store.update({"quality": "low", "volume": -1})
    assert store.get("quality") == "best"
    assert not (tmp_path / "config.json").exists()


def test_secret_never_leaves_the_store(tmp_path):
    store = make_store(tmp_path)
    store.update({"apiKey": "s3cret"})
    field = next(f for f in store.describe()["fields"] if f["key"] == "apiKey")
    assert field["value"] == "" and field["isSet"] is True
    assert store.get("apiKey") == "s3cret"

    store.update({"apiKey": ""})  # clearing it
    field = next(f for f in store.describe()["fields"] if f["key"] == "apiKey")
    assert field["isSet"] is False


def test_on_change_gets_only_changed_keys(tmp_path):
    seen = []
    store = make_store(tmp_path, on_change=seen.append)
    store.update({"gapless": True, "volume": 70})
    assert seen == [{"volume": 70}]


def test_stale_stored_value_falls_back_to_default(tmp_path):
    (tmp_path / "config.json").write_text(json.dumps({"settings": {"quality": "removed-option"}}))
    assert make_store(tmp_path).get("quality") == "best"


def test_register_declares_the_capability(tmp_path):
    server = BackendServer("id", "Name", "0", "", capabilities={})
    make_store(tmp_path).register(server)
    assert server.capabilities["settings"] is True
    assert {"settings.describe", "settings.update"} <= server._handlers.keys()


def test_unknown_group_is_a_programming_error(tmp_path):
    with pytest.raises(ValueError):
        SettingsStore(tmp_path / "c.json", fields=[Field.boolean("x", "X", group="missing")])
