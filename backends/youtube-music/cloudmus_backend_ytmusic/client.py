from ytmusicapi import OAuthCredentials, YTMusic

from rpc_common import errors
from rpc_common.server import BackendError

from . import config

_client: YTMusic | None = None


def _oauth_credentials() -> OAuthCredentials:
    creds = config.get_client_credentials()
    if creds is None:
        raise BackendError(
            errors.AUTH_NOT_AUTHENTICATED,
            f"YouTube Music OAuth client not configured: create a Google Cloud OAuth "
            f'client ("TVs and Limited Input devices" type, YouTube Data API v3 enabled) '
            f"and write {{\"clientId\": ..., \"clientSecret\": ...}} to {config.CLIENT_FILE}",
            errors.app_error_data(retryable=False),
        )
    client_id, client_secret = creds
    return OAuthCredentials(client_id=client_id, client_secret=client_secret)


def get_client() -> YTMusic:
    """Returns the cached authenticated YTMusic client, or raises BackendError
    (auth-not-authenticated) if neither auth mechanism has been set up yet.

    Browser-header auth is tried first: it's the currently-active flow (see
    auth.py's BrowserAuthSession) since device-code OAuth's data calls are
    presently broken upstream (sigma67/ytmusicapi#676) even though OAuth
    login itself still succeeds. Falling back to a leftover oauth_token.json
    from before that break is harmless — it just won't actually work until
    upstream fixes it, same as it doesn't today.
    """
    global _client
    if _client is not None:
        return _client

    if config.has_browser_auth():
        _client = YTMusic(auth=str(config.BROWSER_HEADERS_FILE))
        return _client

    if config.has_token():
        # auth=str(path) (not a dict) so ytmusicapi's RefreshingToken keeps
        # local_cache pointed at this file and auto-persists refreshed
        # tokens back to it on every access_token read past expiry.
        _client = YTMusic(auth=str(config.TOKEN_FILE), oauth_credentials=_oauth_credentials())
        return _client

    raise BackendError(
        errors.AUTH_NOT_AUTHENTICATED,
        "Not authenticated: call auth.start first",
        errors.app_error_data(retryable=False),
    )


def reset_client() -> None:
    """Drops the cached client so the next get_client() re-reads the token
    (used after a fresh login or a logout)."""
    global _client
    _client = None
