from pathlib import Path

import click

from . import auth as auth_module
from . import downloader, resolver
from .client import get_client


@click.group()
def cli():
    """ym-player — a console client for Yandex Music (personal use)."""


@cli.command()
def auth():
    """Authorize via Device Auth Flow and save the token."""
    auth_module.login()


@cli.group()
def download():
    """Download tracks and playlists to disk."""


@download.command("track")
@click.argument("track")
@click.option(
    "--out", "out_dir", default="./downloads", type=click.Path(file_okay=False),
    help="Destination folder (default: ./downloads)",
)
def download_track_cmd(track: str, out_dir: str):
    """Download a single track by music.yandex.ru link or ID."""
    client = get_client()
    t = resolver.resolve_track(client, track)
    downloader.download_track(t, Path(out_dir))


@download.command("playlist")
@click.argument("playlist")
@click.option(
    "--out", "out_dir", default="./downloads", type=click.Path(file_okay=False),
    help="Destination folder (default: ./downloads)",
)
def download_playlist_cmd(playlist: str, out_dir: str):
    """Download a playlist by music.yandex.ru link, UUID, or number (kind)."""
    client = get_client()
    pl = resolver.resolve_playlist(client, playlist)
    downloader.download_playlist(pl, Path(out_dir))


@cli.command()
def tui():
    """Launch the TUI player."""
    from . import tui as tui_module

    tui_module.run()


@cli.command()
def wave():
    """Play "My Wave" straight in the console — no TUI, for debugging the player."""
    from .player import Player, WAVE_STATION

    client = get_client()

    def on_track_change(track):
        if track is None:
            click.echo("Queue finished")
            return
        artists = ", ".join(track.artists_name()) if track.artists_name() else "?"
        click.echo(f"▶ {artists} — {track.title}  (ID: {track.track_id})")

    def on_error(message: str):
        click.secho(f"[error] {message}", fg="red", err=True)

    player = Player(client, on_track_change=on_track_change, on_error=on_error)

    click.echo("Starting the wave...")
    player.start_wave(WAVE_STATION)

    click.echo("Commands: n=next, p=prev, s=save track, Enter=pause/resume, q=quit (or Ctrl+C)")
    try:
        while True:
            cmd = input().strip().lower()
            if cmd == "n":
                player.next()
            elif cmd == "p":
                player.prev()
            elif cmd == "s":
                track = player.current()
                if track is None:
                    click.echo("Nothing is playing")
                    continue
                try:
                    downloader.download_track(track, Path("./downloads"), on_progress=click.echo)
                except Exception as e:
                    click.secho(f"[error] Failed to save the track: {e}", fg="red", err=True)
            elif cmd == "q":
                break
            else:
                player.toggle_pause()
    except (KeyboardInterrupt, EOFError):
        pass
    finally:
        player.shutdown()
        click.echo("Stopped")


def main():
    cli()


if __name__ == "__main__":
    main()
