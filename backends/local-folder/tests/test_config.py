from pathlib import Path

import pytest

from cloudmus_backend_local import config


@pytest.fixture
def home(tmp_path, monkeypatch):
    monkeypatch.setenv("HOME", str(tmp_path))
    monkeypatch.delenv("XDG_CONFIG_HOME", raising=False)
    monkeypatch.delenv("CLOUDMUS_LOCAL_FOLDER_MUSIC_DIR", raising=False)
    monkeypatch.setattr(config, "CONFIG_FILE", tmp_path / "no-such-config.json")
    (tmp_path / ".config").mkdir()
    return tmp_path


def _write_user_dirs(home: Path, music_line: str) -> None:
    (home / ".config" / "user-dirs.dirs").write_text(
        f'XDG_DESKTOP_DIR="$HOME/Рабочий стол"\n{music_line}\n'
    )


def test_defaults_to_the_xdg_music_dir(home):
    _write_user_dirs(home, 'XDG_MUSIC_DIR="$HOME/Музыка"')
    assert config.get_music_dir() == home / "Музыка"


def test_xdg_music_dir_may_be_absolute(home):
    _write_user_dirs(home, 'XDG_MUSIC_DIR="/srv/music"')
    assert config.get_music_dir() == Path("/srv/music")


def test_falls_back_to_home_music_without_user_dirs(home):
    assert config.get_music_dir() == home / "Music"


def test_xdg_music_dir_set_to_home_means_disabled(home):
    _write_user_dirs(home, 'XDG_MUSIC_DIR="$HOME/"')
    assert config.get_music_dir() == home / "Music"


def test_explicit_music_dir_wins_over_xdg(home, monkeypatch):
    _write_user_dirs(home, 'XDG_MUSIC_DIR="$HOME/Музыка"')
    config_file = home / "config.json"
    config_file.write_text('{"musicDir": "~/Songs"}')
    monkeypatch.setattr(config, "CONFIG_FILE", config_file)
    assert config.get_music_dir() == home / "Songs"
