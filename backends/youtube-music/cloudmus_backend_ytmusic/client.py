from ytmusicapi import YTMusic

from rpc_common import errors
from rpc_common.server import BackendError

from . import config
from .i18n import tr

_client: YTMusic | None = None


def get_client() -> YTMusic:
    """Returns the cached authenticated YTMusic client, or raises BackendError
    (auth-not-authenticated) if nobody has signed in yet (see auth.py).
    """
    global _client
    if _client is not None:
        return _client

    if config.has_browser_auth():
        _client = YTMusic(auth=str(config.BROWSER_HEADERS_FILE))
        return _client

    raise BackendError(
        errors.AUTH_NOT_AUTHENTICATED,
        tr("Not authenticated: call auth.start first"),
        errors.app_error_data(retryable=False),
    )


def says_signed_out(client: YTMusic) -> bool:
    """YouTube's own word that it answers this session as nobody's. Raises
    if it can't be asked."""
    response = client._send_request("account/account_menu", {})
    for service in response.get("responseContext", {}).get("serviceTrackingParams", []):
        for param in service.get("params", []):
            if param.get("key") == "logged_in":
                return param.get("value") == "0"
    return False


def signed_out() -> bool:
    """True when YouTube answers the saved session as nobody's. Cookies
    copied from a browser die once Google replaces them there, and the
    calls then fail in ytmusicapi's parsing (a "Sign in" page isn't what it
    expects) or come back empty — neither says why. Asked only after such
    a failure; anything short of YouTube's own word (no network, no saved
    session, an answer without the flag) is not a sign-out."""
    try:
        return says_signed_out(get_client())
    except Exception:
        return False


def reset_client() -> None:
    """Drops the cached client so the next get_client() re-reads the saved
    session (used after a fresh login or a logout)."""
    global _client
    _client = None
