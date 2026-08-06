from __future__ import annotations

import asyncio

from rpc_common import errors
from rpc_common.generated.methods import emit_track_stream_ready
from rpc_common.generated.models import StreamReadyParams
from rpc_common.server import BackendError, BackendServer

from . import catalog, client as client_module, download, playback
from .auth import DeviceAuthSession
from .radio import RadioSession

CAPABILITIES = {
    "playback": {
        "providesStream": True,
        "selfPlayback": False,
        "controls": {"pause": False, "seek": False, "volume": False},
    },
    "browse": {"playlists": True, "likedTracks": True, "radio": True, "search": False},
    "feedback": {"like": True, "dislike": True, "skip": True},
    "download": True,
    "auth": {"required": True, "flow": "deviceCode"},
}


def build_server() -> BackendServer:
    server = BackendServer(
        source_id="yandex-music",
        source_name="Yandex Music",
        source_version="0.1.0",
        source_description="Yandex Music streaming service",
        capabilities=CAPABILITIES,
    )

    auth_session = DeviceAuthSession()
    radio_session: RadioSession | None = None
    inflight_plays: dict[int, asyncio.Task] = {}

    def get_radio_session() -> RadioSession:
        nonlocal radio_session
        client = client_module.get_client()
        if radio_session is None or radio_session.client is not client:
            radio_session = RadioSession(client, server.notify)
        return radio_session

    # --- auth ---

    @server.method("auth.getStatus")
    def handle_auth_get_status(params: dict, request_id: int) -> dict:
        return auth_session.get_status()

    @server.method("auth.start")
    def handle_auth_start(params: dict, request_id: int) -> dict:
        auth_session.start(server.notify)
        return {}

    @server.method("auth.cancel")
    def handle_auth_cancel(params: dict, request_id: int) -> dict:
        auth_session.cancel()
        return {}

    @server.method("auth.logout")
    def handle_auth_logout(params: dict, request_id: int) -> dict:
        auth_session.logout()
        return {}

    @server.method("auth.submit")
    def handle_auth_submit(params: dict, request_id: int) -> dict:
        # deviceCode flow never uses auth.submit; nothing to do.
        return {}

    # --- catalog ---

    @server.method("catalog.listPlaylists")
    async def handle_list_playlists(params: dict, request_id: int) -> dict:
        return await catalog.list_playlists(client_module.get_client())

    @server.method("catalog.listTracks")
    async def handle_list_tracks(params: dict, request_id: int) -> dict:
        try:
            return await catalog.list_tracks(client_module.get_client(), params["playlistId"])
        except LookupError:
            raise BackendError(
                errors.RESOURCE_NOT_FOUND,
                "Playlist not found",
                errors.app_error_data(retryable=False, detail=f"playlistId={params['playlistId']}"),
            )

    @server.method("catalog.listLiked")
    async def handle_list_liked(params: dict, request_id: int) -> dict:
        return await catalog.list_liked(client_module.get_client())

    @server.method("catalog.startRadio")
    async def handle_start_radio(params: dict, request_id: int) -> dict:
        return await get_radio_session().start(params.get("seed"))

    @server.method("catalog.downloadTrack")
    async def handle_download_track(params: dict, request_id: int) -> dict:
        try:
            return await download.download_track(
                client_module.get_client(), params["trackId"], params["destDir"]
            )
        except LookupError:
            raise BackendError(
                errors.RESOURCE_NOT_FOUND,
                "Track not found",
                errors.app_error_data(retryable=False, detail=f"trackId={params['trackId']}"),
            )

    # --- playback ---

    @server.method("playback.play")
    def handle_play(params: dict, request_id: int) -> dict:
        track_id = params["trackId"]
        client = client_module.get_client()
        cancel_event = asyncio.Event()

        async def resolve() -> None:
            try:
                stream = await playback.resolve_stream_with_retry(client, track_id, cancel_event)
            except asyncio.CancelledError:
                return
            except Exception as e:
                await server.notify(
                    "error",
                    {
                        "code": errors.UPSTREAM_UNREACHABLE,
                        "message": f"Failed to resolve stream for {track_id}: {e}",
                        "data": errors.app_error_data(retryable=True),
                    },
                )
                return
            await emit_track_stream_ready(
                server.notify,
                StreamReadyParams(requestId=request_id, trackId=track_id, stream=stream),
            )

        task = asyncio.create_task(resolve())
        inflight_plays[request_id] = (task, cancel_event)
        task.add_done_callback(lambda t: inflight_plays.pop(request_id, None))
        return {"accepted": True}

    @server.method("playback.cancel")
    def handle_cancel(params: dict, request_id: int) -> dict:
        entry = inflight_plays.get(params["requestId"])
        if entry is not None:
            _, cancel_event = entry
            cancel_event.set()
        return {}

    # --- feedback ---

    @server.method("feedback.like")
    async def handle_like(params: dict, request_id: int) -> dict:
        await asyncio.to_thread(client_module.get_client().users_likes_tracks_add, params["trackId"])
        return {}

    @server.method("feedback.dislike")
    async def handle_dislike(params: dict, request_id: int) -> dict:
        await asyncio.to_thread(client_module.get_client().users_dislikes_tracks_add, params["trackId"])
        return {}

    @server.method("feedback.trackStarted")
    async def handle_track_started(params: dict, request_id: int) -> dict:
        await get_radio_session().track_started(params["trackId"])
        return {}

    @server.method("feedback.trackFinished")
    async def handle_track_finished(params: dict, request_id: int) -> dict:
        await get_radio_session().track_finished(params["trackId"], params["playedMs"])
        return {}

    @server.method("feedback.skip")
    async def handle_skip(params: dict, request_id: int) -> dict:
        await get_radio_session().skip(params["trackId"], params["playedMs"])
        return {}

    return server
