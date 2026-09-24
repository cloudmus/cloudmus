from yandex_music import Client

from rpc_common import errors
from rpc_common.server import BackendError

from . import config

_client: Client | None = None


def get_client() -> Client:
    """Returns the cached authenticated Client, or raises BackendError
    (auth-not-authenticated) if no token is saved yet."""
    global _client
    if _client is not None:
        return _client

    token = config.get_token()
    if not token:
        raise BackendError(
            errors.AUTH_NOT_AUTHENTICATED,
            "Not authenticated: call auth.start first",
            errors.app_error_data(retryable=False),
        )

    _client = Client(token).init()
    return _client


def reset_client() -> None:
    """Drops the cached client so the next get_client() re-reads the token
    (used after a fresh login or a logout)."""
    global _client
    _client = None
