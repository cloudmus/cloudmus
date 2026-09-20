from __future__ import annotations

import asyncio

from ytmusicapi import LikeStatus

from rpc_common import errors
from rpc_common.generated.methods import emit_track_stream_ready
from rpc_common.generated.models import StreamReadyParams
from rpc_common.server import BackendError, BackendServer

from . import catalog, client as client_module, playback
from .auth import BrowserAuthSession

CAPABILITIES = {
    "playback": {
        "providesStream": True,
        "selfPlayback": False,
        "controls": {"pause": False, "seek": False, "volume": False},
    },
    "browse": {"playlists": True, "likedTracks": True, "radio": False, "search": False},
    "feedback": {"like": True, "dislike": True, "skip": False},
    "download": False,
    # usernamePassword, not deviceCode: device-code OAuth login itself still
    # works, but every data call made with the resulting token currently
    # 400s due to a confirmed upstream break (see auth.py's module
    # docstring, sigma67/ytmusicapi#676) — repurposed here to collect
    # pasted browser request-headers instead of a username/password pair.
    # Flip back to "deviceCode" (and instantiate DeviceOAuthSession below
    # instead) once that's fixed upstream.
    "auth": {"required": True, "flow": "usernamePassword"},
}


def build_server() -> BackendServer:
    server = BackendServer(
        source_id="youtube-music",
        source_name="YouTube Music",
        source_version="0.1.0",
        source_description="YouTube Music streaming service",
        capabilities=CAPABILITIES,
    )

    auth_session = BrowserAuthSession()
    inflight_plays: dict[int, tuple[asyncio.Task, asyncio.Event]] = {}

    # --- auth ---

    @server.method("auth.getStatus")
    def handle_auth_get_status(params: dict, request_id: int) -> dict:
        return auth_session.get_status()

    @server.method("auth.start")
    async def handle_auth_start(params: dict, request_id: int) -> dict:
        await auth_session.start(server.notify)
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
    async def handle_auth_submit(params: dict, request_id: int) -> dict:
        await auth_session.submit(params["fields"], server.notify)
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

    # --- playback ---

    @server.method("playback.play")
    def handle_play(params: dict, request_id: int) -> dict:
        track_id = params["trackId"]
        cancel_event = asyncio.Event()

        async def resolve() -> None:
            try:
                stream = await playback.resolve_stream_with_retry(track_id, cancel_event)
            except asyncio.CancelledError:
                return
            except LookupError as e:
                await server.notify(
                    "error",
                    {
                        "code": errors.RESOURCE_NOT_FOUND,
                        "message": f"Track not playable: {e}",
                        "data": errors.app_error_data(retryable=False),
                    },
                )
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
        await asyncio.to_thread(client_module.get_client().rate_song, params["trackId"], LikeStatus.LIKE)
        return {}

    @server.method("feedback.dislike")
    async def handle_dislike(params: dict, request_id: int) -> dict:
        await asyncio.to_thread(client_module.get_client().rate_song, params["trackId"], LikeStatus.DISLIKE)
        return {}

    # Rating is a single tri-state field on this backend (LIKE/DISLIKE/
    # INDIFFERENT) — unlike and undislike both just clear it back to
    # INDIFFERENT, there's no separate "remove" call.
    @server.method("feedback.unlike")
    async def handle_unlike(params: dict, request_id: int) -> dict:
        await asyncio.to_thread(client_module.get_client().rate_song, params["trackId"], LikeStatus.INDIFFERENT)
        return {}

    @server.method("feedback.undislike")
    async def handle_undislike(params: dict, request_id: int) -> dict:
        await asyncio.to_thread(client_module.get_client().rate_song, params["trackId"], LikeStatus.INDIFFERENT)
        return {}

    return server
