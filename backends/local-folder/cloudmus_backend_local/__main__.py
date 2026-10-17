import sys

_rpc_out = sys.stdout
sys.stdout = sys.stderr

import logging

logging.basicConfig(stream=sys.stderr, level=logging.INFO)

from rpc_common.server import serve

from cloudmus_backend_local.server import build_server


def main() -> None:
    server = build_server()
    serve(server, _rpc_out)


if __name__ == "__main__":
    main()
