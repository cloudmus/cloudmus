import sys

from yandex_music import Client

from . import config

_client: Client | None = None


def get_client() -> Client:
    global _client
    if _client is not None:
        return _client

    token = config.get_token()
    if not token:
        print("No token found. Run `ym auth` first.", file=sys.stderr)
        sys.exit(1)

    _client = Client(token).init()
    return _client
