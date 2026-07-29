import sys

_rpc_out = sys.stdout
sys.stdout = sys.stderr

import asyncio
import logging

logging.basicConfig(stream=sys.stderr, level=logging.INFO)

from cloudmus_backend_local.server import build_server


def main() -> None:
    server = build_server()
    asyncio.run(server.run(_rpc_out))


if __name__ == "__main__":
    main()
