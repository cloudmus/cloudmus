import re
from pathlib import Path
from typing import Callable

from mutagen.id3 import APIC, ID3, TALB, TIT2, TPE1, TRCK
from yandex_music import Playlist, Track

Progress = Callable[[str], None]


def _noop(_: str) -> None:
    pass


def _safe_name(name: str) -> str:
    return re.sub(r'[\\/:*?"<>|]', "_", name).strip()


def _tag_file(path: Path, track: Track, track_num: int | None = None, on_progress: Progress = _noop) -> None:
    try:
        tags = ID3()
        tags["TIT2"] = TIT2(encoding=3, text=track.title or "")
        tags["TPE1"] = TPE1(encoding=3, text=track.artists_name() or [])
        if track.albums:
            tags["TALB"] = TALB(encoding=3, text=track.albums[0].title or "")
        if track_num:
            tags["TRCK"] = TRCK(encoding=3, text=str(track_num))
        cover = track.download_cover_bytes(size="400x400")
        if cover:
            tags["APIC"] = APIC(encoding=3, mime="image/jpeg", type=3, desc="Cover", data=cover)
        tags.save(path)
    except Exception as e:
        on_progress(f"  (failed to write tags: {e})")


def download_track(
    track: Track, dest_dir: Path, track_num: int | None = None, on_progress: Progress = print
) -> Path:
    dest_dir.mkdir(parents=True, exist_ok=True)
    artists = ", ".join(track.artists_name()) if track.artists_name() else "Unknown"
    filename = _safe_name(f"{artists} - {track.title}") + ".mp3"
    path = dest_dir / filename

    on_progress(f"Downloading: {artists} - {track.title}")
    track.download(str(path), codec="mp3", bitrate_in_kbps=320)
    _tag_file(path, track, track_num, on_progress=on_progress)
    return path


def download_tracks(tracks: list[Track], dest_dir: Path, on_progress: Progress = print) -> None:
    for i, track in enumerate(tracks, start=1):
        try:
            download_track(track, dest_dir, track_num=i, on_progress=on_progress)
        except Exception as e:
            on_progress(f"  error downloading '{track.title}': {e}")


def playlist_tracks(playlist: Playlist) -> list[Track]:
    # users_playlists_list() returns lightweight Playlist objects with an
    # empty `tracks` field — the actual track list has to be fetched
    # separately (a second request per playlist, via users_playlists()).
    shorts = playlist.tracks or playlist.fetch_tracks()
    tracks = []
    for short in shorts or []:
        track = short.track or short.fetch_track()
        if track is not None:
            tracks.append(track)
    return tracks


def download_playlist(playlist: Playlist, dest_root: Path, on_progress: Progress = print) -> None:
    tracks = playlist_tracks(playlist)
    dest_dir = dest_root / _safe_name(playlist.title or "playlist")
    on_progress(f"Playlist “{playlist.title}”: {len(tracks)} tracks -> {dest_dir}")
    download_tracks(tracks, dest_dir, on_progress=on_progress)
