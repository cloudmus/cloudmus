from __future__ import annotations

import asyncio

from rpc_common import errors
from rpc_common.generated.methods import emit_track_stream_ready
from rpc_common.generated.models import StreamReadyParams
from rpc_common.server import BackendError, BackendServer

from . import catalog, config, playback

CAPABILITIES = {
    "playback": {
        "providesStream": True,
        "selfPlayback": False,
        "controls": {"pause": False, "seek": False, "volume": False},
    },
    "browse": {"playlists": True, "likedTracks": False, "radio": False, "search": False},
    "feedback": {"like": False, "dislike": False, "skip": False},
    "download": False,
    "auth": {"required": False, "flow": "none"},
}


def build_server() -> BackendServer:
    server = BackendServer(
        source_id="local-folder",
        source_name="Local Folder",
        source_version="0.1.0",
        source_description="Music files from a folder on this computer",
        capabilities=CAPABILITIES,
    )

    @server.method("catalog.listPlaylists")
    def handle_list_playlists(params: dict, request_id: int) -> dict:
        return catalog.list_playlists(config.get_music_dir())

    @server.method("catalog.listTracks")
    def handle_list_tracks(params: dict, request_id: int) -> dict:
        try:
            return catalog.list_tracks(config.get_music_dir(), params["playlistId"])
        except KeyError:
            raise BackendError(
                errors.RESOURCE_NOT_FOUND,
                "Playlist not found",
                errors.app_error_data(retryable=False, detail=f"playlistId={params['playlistId']}"),
            )

    @server.method("playback.play")
    def handle_play(params: dict, request_id: int) -> dict:
        track_id = params["trackId"]
        stream = playback.resolve_stream(config.get_music_dir(), track_id)
        if stream is None:
            raise BackendError(
                errors.RESOURCE_NOT_FOUND,
                "Track not found",
                errors.app_error_data(retryable=False, detail=f"trackId={track_id}"),
            )
        asyncio.create_task(
            emit_track_stream_ready(
                server.notify,
                StreamReadyParams(requestId=request_id, trackId=track_id, stream=stream),
            )
        )
        return {"accepted": True}

    @server.method("playback.cancel")
    def handle_cancel(params: dict, request_id: int) -> dict:
        # Resolution is synchronous/local; there is never real in-flight work
        # to cancel, but the method must still exist and ack per docs/protocol.md §7.2.
        return {}

    return server
