"""Two auth sessions live here:

- BrowserAuthSession — the currently ACTIVE flow (see server.py's
  CAPABILITIES), driven by auth.start/auth.submit (docs/protocol.md §10.2).
  Collects pasted browser request-headers, which ytmusicapi's own
  setup_browser() parses/validates/writes — see that class's docstring.
- DeviceOAuthSession — the ORIGINAL flow, left in place but unused. OAuth
  login itself still succeeds, but every actual data call made with the
  resulting token now 400s with "Request contains an invalid argument" —
  confirmed against the real API this session (auth.getStatus reports
  authenticated, the token passes Google's own tokeninfo introspection with
  the correct client_id/scope, yet search/get_home/get_library_playlists/
  get_liked_songs all fail identically, while anonymous calls succeed).
  This matches sigma67/ytmusicapi#676 exactly, labeled "yt-update" by the
  maintainers (their convention for "a server-side change on Google's end
  broke this") — not a CloudMus or user-configuration problem. Flipping
  CAPABILITIES["auth"]["flow"] back to "deviceCode" in server.py and
  swapping which session build_server() instantiates is the entire revert
  once upstream fixes it.

Structurally, DeviceOAuthSession mirrors cloudmus_backend_yandex/auth.py's
DeviceAuthSession, but ytmusicapi has no single blocking call like
yandex_music's Client().device_auth() that internally polls and invokes an
on_code callback — OAuthCredentials only exposes get_code() (one-shot) and
token_from_code() (one-shot, returns a "still pending" error shape rather
than raising while the user hasn't finished signing in yet). So this drives
the poll loop itself, entirely inside the asyncio task rather than a
background thread — there's no long blocking call to run via
asyncio.to_thread here, only short individual HTTP round-trips.

Note: auth/prompt is sent directly via notify(), not through the generated
emit_auth_prompt helper, for the same reason as yandex's auth.py: the
generated PromptParams type only carries {flow}, and neither deviceCode's
nor usernamePassword's payload fits that (deviceCode needs url/code/
expiresInSec; usernamePassword needs fields).
"""
from __future__ import annotations

import asyncio
import functools
import logging
import time
from typing import Any, Awaitable, Callable

import requests
from ytmusicapi import OAuthCredentials
from ytmusicapi.auth.browser import setup_browser
from ytmusicapi.auth.oauth import RefreshingToken

from rpc_common.generated.methods import emit_auth_status_changed
from rpc_common.generated.models import StatusChangedParams

from . import client as client_module
from . import config

logger = logging.getLogger(__name__)

NotifyFn = Callable[[str, dict[str, Any]], Awaitable[None]]

# Google's device-code endpoint is expected to echo back a poll interval
# (typically 5s); fall back to this if it doesn't.
DEFAULT_POLL_INTERVAL_SEC = 5

# OAuthCredentials builds a plain requests.Session() with no timeout at all
# when none is given (unlike ytmusicapi.YTMusic's own default session, which
# has a 30s timeout baked in) — a stalled connection to Google's endpoints
# would otherwise hang _run() forever, past even asyncio.to_thread's return,
# with nothing ever reaching notify()/_fail() and the front just sitting on
# a dead Retry click with no error to show. See _timeout_session().
_OAUTH_REQUEST_TIMEOUT_SEC = 15.0


def _timeout_session() -> requests.Session:
    session = requests.Session()
    session.request = functools.partial(session.request, timeout=_OAUTH_REQUEST_TIMEOUT_SEC)
    return session

# authorization_pending: user hasn't finished the browser step yet, keep polling.
# slow_down: polling too fast, back off but keep polling (we already poll at
# the server-provided interval, so just treat it the same as pending).
_PENDING_ERRORS = {"authorization_pending", "slow_down"}


class BrowserAuthSession:
    """Collects browser request-headers pasted via auth.submit, the
    currently-active flow (module docstring explains why OAuth is dormant).

    Unlike DeviceOAuthSession, this has no background polling task —
    auth.start just emits the auth/prompt asking for the paste, and
    auth.submit does the (synchronous, no-network) work of validating and
    saving it. cancel() is a no-op for the same reason: there's nothing
    in-flight between start() and submit() to cancel.
    """

    def __init__(self) -> None:
        self.status: str = "unauthenticated"
        self.error_message: str | None = None

    def get_status(self) -> dict[str, Any]:
        if config.has_browser_auth():
            return {"status": "authenticated"}
        if self.status == "error":
            return {"status": "error", "detail": self.error_message or ""}
        return {"status": self.status}

    async def start(self, notify: NotifyFn) -> None:
        self.status = "pending"
        # Awaited directly, not asyncio.create_task()'d: there's no actual
        # async work to defer here (unlike DeviceOAuthSession's start(),
        # which kicks off a real multi-minute polling flow) — a scheduled-
        # but-unawaited task for a single immediate notify() has no
        # ordering guarantee relative to whatever request the front sends
        # next, which showed up as a real bug when testing this by hand:
        # the auth/prompt notification arrived after several subsequent
        # requests' responses instead of before them.
        await notify(
            "auth/prompt",
            {
                "flow": "usernamePassword",
                "fields": [{"name": "headers", "secret": False, "multiline": True}],
            },
        )

    def cancel(self) -> None:
        pass

    async def submit(self, fields: dict[str, str], notify: NotifyFn) -> None:
        try:
            path = config.browser_headers_path()
            # setup_browser() parses the pasted "Header: value" lines,
            # validates cookie/x-goog-authuser are present, strips
            # sec-*/host/content-length/accept-encoding, merges in default
            # headers, and writes the result as JSON to `path` itself —
            # reusing ytmusicapi's own tested logic rather than
            # reimplementing header parsing/validation here. Raises
            # YTMusicUserError (missing required header) or YTMusicError
            # (couldn't parse the pasted text at all) — both handled the
            # same way below, the message is informative either way.
            setup_browser(str(path), fields.get("headers", ""))
            path.chmod(0o600)
        except Exception as e:
            logger.debug("browser auth submit failed: %s", e)
            self.status = "error"
            self.error_message = str(e)
            await emit_auth_status_changed(notify, StatusChangedParams(status="error", message=str(e)))
            return

        client_module.reset_client()
        self.status = "authenticated"
        await emit_auth_status_changed(notify, StatusChangedParams(status="authenticated"))

    def logout(self) -> None:
        config.clear_browser_auth()
        client_module.reset_client()
        self.status = "unauthenticated"


class DeviceOAuthSession:
    """Tracks one in-flight (or completed) device-code login attempt."""

    def __init__(self) -> None:
        self._cancel_event = asyncio.Event()
        self._task: asyncio.Task | None = None
        self.status: str = "unauthenticated"
        self.error_message: str | None = None

    def get_status(self) -> dict[str, Any]:
        if config.has_token():
            return {"status": "authenticated"}
        if self.status == "error":
            return {"status": "error", "detail": self.error_message or ""}
        return {"status": self.status}

    async def start(self, notify: NotifyFn) -> None:
        if self._task is not None and not self._task.done():
            return  # already in progress
        self._cancel_event = asyncio.Event()
        self.status = "pending"
        # Not awaited here — the flow itself (device-code polling, up to
        # expiresInSec) can take minutes; start() only kicks it off. async
        # def purely so this and BrowserAuthSession.start() share one
        # `await auth_session.start(...)` call site in server.py.
        self._task = asyncio.create_task(self._run(notify))

    def cancel(self) -> None:
        self._cancel_event.set()

    async def _run(self, notify: NotifyFn) -> None:
        creds_pair = config.get_client_credentials()
        if creds_pair is None:
            await self._fail(
                notify,
                f"YouTube Music OAuth client not configured. Create a Google Cloud OAuth "
                f'client ("TVs and Limited Input devices" type, YouTube Data API v3 enabled) '
                f"and write {{\"clientId\": ..., \"clientSecret\": ...}} to {config.CLIENT_FILE}.",
            )
            return

        credentials = OAuthCredentials(
            client_id=creds_pair[0], client_secret=creds_pair[1], session=_timeout_session()
        )

        try:
            code = await asyncio.to_thread(credentials.get_code)
        except Exception as e:
            logger.debug("device code request failed: %s", e)
            await self._fail(notify, str(e))
            return

        await notify(
            "auth/prompt",
            {
                "flow": "deviceCode",
                "url": code["verification_url"],
                "code": code["user_code"],
                "expiresInSec": code["expires_in"],
            },
        )

        try:
            raw_token = await self._poll_until_authorized(credentials, code)
        except _Cancelled:
            self.status = "unauthenticated"
            return
        except Exception as e:
            logger.debug("device auth failed: %s", e)
            await self._fail(notify, str(e))
            return

        _save_token(credentials, raw_token)
        client_module.reset_client()
        self.status = "authenticated"
        await emit_auth_status_changed(notify, StatusChangedParams(status="authenticated"))

    async def _poll_until_authorized(self, credentials: OAuthCredentials, code: dict) -> dict:
        interval = code.get("interval") or DEFAULT_POLL_INTERVAL_SEC
        deadline = time.monotonic() + code["expires_in"]

        while True:
            if time.monotonic() > deadline:
                raise TimeoutError("device code expired before sign-in was completed")

            try:
                await asyncio.wait_for(self._cancel_event.wait(), timeout=interval)
                raise _Cancelled  # cancel_event was set during the poll-interval wait
            except asyncio.TimeoutError:
                pass

            response = await asyncio.to_thread(credentials.token_from_code, code["device_code"])
            if "access_token" in response:
                return response
            error = response.get("error")
            if error not in _PENDING_ERRORS:
                raise RuntimeError(f"device authorization failed: {error or response}")

    async def _fail(self, notify: NotifyFn, message: str) -> None:
        self.status = "error"
        self.error_message = message
        await emit_auth_status_changed(notify, StatusChangedParams(status="error", message=message))

    def logout(self) -> None:
        config.clear_token()
        client_module.reset_client()
        self.status = "unauthenticated"


class _Cancelled(Exception):
    pass


def _save_token(credentials: OAuthCredentials, raw_token: dict) -> None:
    # Mirrors ytmusicapi.setup.setup_oauth()'s post-processing of
    # token_from_code()'s response (see RefreshingToken.prompt_for_token) —
    # refresh_token_expires_in isn't always present, expires_in is.
    refresh_token_expires_in = raw_token.get("refresh_token_expires_in", raw_token["expires_in"])
    token = RefreshingToken(
        credentials=credentials,
        access_token=raw_token["access_token"],
        refresh_token=raw_token["refresh_token"],
        scope=raw_token["scope"],
        token_type=raw_token["token_type"],
        expires_in=refresh_token_expires_in,
    )
    token.update(raw_token)
    path = config.token_path()
    token.local_cache = path  # triggers store_token(), writing the file
    path.chmod(0o600)
