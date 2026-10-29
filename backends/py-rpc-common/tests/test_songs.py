import pytest

from rpc_common.songs import song_key


@pytest.mark.parametrize(
    "version",
    [
        "MONTAGEM ACCORDION - Sped Up",
        "Montagem Accordion (Sped Up)",
        "MONTAGEM  ACCORDION (Slowed + Reverb)",
        "Montagem Accordion [Official Music Video]",
        "Montagem Accordion (Sped Up) [Official Audio]",
        "Montagem Accordion - 2011 Remaster",
        "Montagem Accordion (feat. Someone)",
        "Montagem Accordion ft. Someone",
        "LXNGVX - Montagem Accordion (Official Video)",
    ],
)
def test_versions_of_a_song_share_its_key(version):
    assert song_key(version, ["LXNGVX"]) == song_key("MONTAGEM ACCORDION", ["LXNGVX"])


@pytest.mark.parametrize(
    "other",
    [
        "Montagem Accordion (Remix)",
        "Montagem Accordion - Live",
        "Montagem Accordion (Acoustic Version)",
        "Montagem Accordion 2",
    ],
)
def test_a_different_recording_keeps_its_own_key(other):
    assert song_key(other, ["LXNGVX"]) != song_key("Montagem Accordion", ["LXNGVX"])


def test_the_same_title_by_another_artist_is_another_song():
    assert song_key("Intro", ["A"]) != song_key("Intro", ["B"])


def test_a_title_that_is_all_version_mark_is_kept():
    assert song_key("(Radio Edit)", ["A"]) != song_key("", ["A"])
