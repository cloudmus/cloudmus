import sys

_rpc_out = sys.stdout
sys.stdout = sys.stderr

import asyncio
import logging

logging.basicConfig(stream=sys.stderr, level=logging.INFO)
# yandex_music/requests may otherwise emit warnings to stdout by default;
# they're stdlib logging-based, so basicConfig(stream=sys.stderr) above
# already routes them correctly.

from cloudmus_backend_yandex.server import build_server


def main() -> None:
    server = build_server()
    asyncio.run(server.run(_rpc_out))


if __name__ == "__main__":
    main()
