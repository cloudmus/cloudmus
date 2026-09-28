"""One keep-alive HTTP session for every call to Yandex.

yandex_music's Request._request_wrapper calls the module-level
requests.request(), which builds a throwaway Session per call: every API
request pays a fresh DNS lookup, TCP connect and TLS handshake. That is
~0.7-1.4s per request on Windows, and resolving one stream takes four of
them in a row. Routing the library through a single pooled Session reuses
the connection instead.

The wrapper does `import requests` inside the function, so swapping the
module it sees isn't possible; install() replaces the method itself with a
copy that differs only in using the shared session.
"""
from __future__ import annotations

import threading
from typing import Any

import requests
from requests.adapters import HTTPAdapter

# Stream resolution, radio feedback and catalog calls run concurrently
# (each in its own asyncio.to_thread worker); the default pool of 10 per
# host would make the extra ones wait for a free connection.
POOL_SIZE = 16

_session: requests.Session | None = None
_lock = threading.Lock()
_installed = False


def session() -> requests.Session:
    """The process-wide session (thread-safe for concurrent requests)."""
    global _session
    with _lock:
        if _session is None:
            _session = requests.Session()
            adapter = HTTPAdapter(pool_connections=4, pool_maxsize=POOL_SIZE)
            _session.mount("https://", adapter)
            _session.mount("http://", adapter)
        return _session


def install() -> None:
    """Idempotent. Must run before http_logging.install(), so the logging
    wrapper wraps this one."""
    global _installed
    if _installed:
        return
    _installed = True

    from yandex_music.exceptions import NetworkError, TimedOutError
    from yandex_music.utils.request import Request

    # Mirrors yandex_music's own Request._request_wrapper, with
    # session().request in place of requests.request.
    def session_request_wrapper(self: Request, *args: Any, **kwargs: Any) -> bytes:
        kwargs = self._prepare_kwargs(kwargs)
        try:
            resp = session().request(*args, **kwargs)
        except requests.Timeout as e:
            raise TimedOutError from e
        except requests.RequestException as e:
            raise NetworkError(e) from e

        if 200 <= resp.status_code <= 299:
            return resp.content

        self._handle_error_response(resp.status_code, resp.content)
        return None

    Request._request_wrapper = session_request_wrapper  # type: ignore[method-assign]
