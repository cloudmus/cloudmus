"""Signing in to YouTube Music (docs/protocol.md §10): the session is the
browser's own cookies.

Two methods are offered. `browser`: the front opens Google's sign-in page in
a built-in browser and returns the cookies it ends up with. `form`: the user
pastes the request headers copied from a browser, for a front that has none.
Either way ytmusicapi's setup_browser() validates the headers and writes
them down, and one library call checks YouTube takes them.

There is no device-code (OAuth) method: such a login succeeds, but every
data call made with its token fails with HTTP 400 "Request contains an
invalid argument", a change on Google's side
(https://github.com/sigma67/ytmusicapi/issues/676).
"""
from __future__ import annotations

import asyncio
import logging
import shlex
from pathlib import Path
from typing import Any, Awaitable, Callable

from ytmusicapi import YTMusic
from ytmusicapi.auth.browser import setup_browser

from rpc_common.generated.methods import emit_auth_prompt, emit_auth_status_changed
from rpc_common.generated.models import (
    AuthField,
    AuthMethod,
    PromptParams,
    StatusChangedParams,
    SubmitParams,
)

from . import client as client_module
from . import config
from .i18n import tr

logger = logging.getLogger(__name__)

NotifyFn = Callable[[str, dict[str, Any]], Awaitable[None]]

MUSIC_URL = "https://music.youtube.com"
# Google's own sign-in page, coming back to YouTube Music, which is what puts
# the cookies the session is made of on youtube.com.
SIGN_IN_URL = "https://accounts.google.com/ServiceLogin?continue=https%3A%2F%2Fmusic.youtube.com%2F"
# What ytmusicapi signs its requests with: there once the sign-in is through.
SESSION_COOKIE = "__Secure-3PAPISID"


def curl_to_headers(text: str) -> str:
    """Turn a pasted `curl ...` command (Chrome's "Copy as cURL (bash)") into
    the "Name: value" lines setup_browser() expects; anything that doesn't
    start with `curl` is returned unchanged, so Firefox's raw header block
    keeps working.

    Chrome has no raw-headers copy, only this — hence the second input form.
    """
    stripped = text.strip()
    if not stripped.startswith("curl"):
        return text
    # Chrome splits the command over lines with a trailing backslash.
    try:
        tokens = shlex.split(stripped.replace("\\\n", " "))
    except ValueError:
        return text
    lines: list[str] = []
    i = 0
    while i < len(tokens):
        token = tokens[i]
        if token in ("-H", "--header", "-b", "--cookie") and i + 1 < len(tokens):
            value = tokens[i + 1]
            lines.append(value if token in ("-H", "--header") else f"cookie: {value}")
            i += 1
        i += 1
    return "\n".join(lines)


def with_authorization_marker(raw: str) -> str:
    """ytmusicapi picks browser auth over OAuth only when the saved headers
    carry an `authorization: SAPISIDHASH ...` header, else it demands OAuth
    credentials. Not every request has one (e.g. Firefox's/Chrome's
    `ptracking` calls don't), but its value is recomputed from the cookie on
    every call, so a bare marker is enough."""
    if any(line.partition(":")[0].strip().lower() == "authorization" for line in raw.splitlines()):
        return raw
    return raw.rstrip("\n") + "\nauthorization: SAPISIDHASH placeholder\n"


def _verify_login(path: Path) -> None:
    """Asks YouTube about the just-saved session: headers can parse fine yet
    be rejected (expired or foreign cookie, wrong account index), which would
    otherwise only show up as an empty library later. A rejected session's
    library call doesn't always fail, so YouTube is also asked who it takes
    the session for. Raises if it isn't taken."""
    client = YTMusic(auth=str(path))
    if client_module.says_signed_out(client):
        raise RuntimeError(tr("YouTube Music has signed this session out. Sign in again."))
    client.get_library_playlists(limit=1)


def browser_to_headers(cookies: dict[str, str]) -> str:
    """The "Name: value" lines setup_browser() expects, from what a built-in
    browser ended up with. Its User-Agent isn't passed on: setup_browser()
    puts ytmusicapi's own in its place. Account 0, the one just signed in to:
    the browser started with no session."""
    return f"cookie: {cookies.get(MUSIC_URL, '')}\nx-goog-authuser: 0\n"


class BrowserAuthSession:
    """One sign-in at a time: auth.start emits the auth/prompt, auth.submit
    validates and saves what came of it. Nothing runs in between, so cancel()
    only drops the "pending" status.
    """

    def __init__(self) -> None:
        self.status: str = "unauthenticated"
        self.error_message: str | None = None
        # The saved headers are there, but YouTube no longer takes them.
        self.expired = False

    def get_status(self) -> dict[str, Any]:
        if config.has_browser_auth() and not self.expired:
            return {"status": "authenticated"}
        if self.status == "error":
            return {"status": "error", "detail": self.error_message or ""}
        return {"status": self.status}

    async def start(self, notify: NotifyFn) -> None:
        self.status = "pending"
        # Awaited directly, not asyncio.create_task()'d: a scheduled-but-
        # unawaited notification has no ordering guarantee relative to the
        # responses that follow, and arrived after several of them.
        await emit_auth_prompt(
            notify,
            PromptParams(
                methods=[
                    AuthMethod(
                        kind="browser",
                        url=SIGN_IN_URL,
                        cookieUrls=[MUSIC_URL],
                        doneCookies=[SESSION_COOKIE],
                        # A session of its own: Google replaces a session's
                        # cookies as it's used, and two users of one session
                        # soon leave one of them signed out.
                        freshSession=True,
                    ),
                    AuthMethod(
                        kind="form",
                        # docs/protocol.md §10: shown by the front above the
                        # field, which on its own can't tell what's expected.
                        message=tr(
                            'Open <a href="https://music.youtube.com">music.youtube.com</a> in your browser '
                            "while signed in, open <b>DevTools → Network</b>, click any request to "
                            "<code>music.youtube.com</code>, and paste its <b>Request Headers</b> (Firefox) or "
                            "<b>Copy as cURL (bash)</b> (Chrome) below."
                        )
                        # Google replaces the session's cookies as the browser
                        # goes on using it, and the copied ones die with that.
                        + " "
                        + tr(
                            "Do this in a private window and close it afterwards without signing out: "
                            "a browser that keeps using the same session soon makes these headers stop working."
                        ),
                        fields=[AuthField(name="headers", secret=False, multiline=True)],
                    ),
                ]
            ),
        )

    def cancel(self) -> None:
        # Left "pending", the front's next auth.getStatus would show a
        # sign-in that no longer exists, with no way to start another.
        if self.status == "pending":
            self.status = "unauthenticated"

    async def mark_expired(self, notify: NotifyFn) -> str:
        """YouTube has signed the saved session out (see
        client.signed_out()): told to the front once, as a sign-in error,
        so that it asks the user to sign in again. Returns the message."""
        message = tr("YouTube Music has signed this session out. Sign in again.")
        if not self.expired:
            self.expired = True
            self.status = "error"
            self.error_message = message
            await emit_auth_status_changed(notify, StatusChangedParams(status="error", message=message))
        return message

    async def submit(self, params: SubmitParams, notify: NotifyFn) -> None:
        try:
            if params.browser is not None:
                raw = browser_to_headers(params.browser.cookies)
            else:
                raw = curl_to_headers((params.form or {}).get("headers", ""))
            path = config.browser_headers_path()
            # setup_browser() parses the "Header: value" lines, validates
            # cookie/x-goog-authuser are present, strips sec-*/host/
            # content-length/accept-encoding, merges in default headers, and
            # writes the result as JSON to `path` itself. Raises
            # YTMusicUserError (missing required header) or YTMusicError
            # (couldn't parse the pasted text at all) — both handled the same
            # way below, the message is informative either way.
            setup_browser(str(path), with_authorization_marker(raw))
            path.chmod(0o600)
            await asyncio.to_thread(_verify_login, path)
        except Exception as e:
            logger.debug("browser auth submit failed: %s", e)
            # Not left behind: has_browser_auth() would report a rejected
            # sign-in as signed in.
            config.browser_headers_path().unlink(missing_ok=True)
            self.status = "error"
            self.error_message = str(e)
            await emit_auth_status_changed(notify, StatusChangedParams(status="error", message=str(e)))
            return

        client_module.reset_client()
        self.status = "authenticated"
        self.expired = False
        await emit_auth_status_changed(notify, StatusChangedParams(status="authenticated"))

    def logout(self) -> None:
        self.expired = False
        config.clear_browser_auth()
        client_module.reset_client()
        self.status = "unauthenticated"
