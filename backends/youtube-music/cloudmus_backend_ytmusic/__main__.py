import sys

_rpc_out = sys.stdout
sys.stdout = sys.stderr

import logging

logging.basicConfig(stream=sys.stderr, level=logging.INFO)

from rpc_common import startup
from rpc_common.server import serve

startup.mark("the protocol imported")

from cloudmus_backend_ytmusic.server import build_server

startup.mark("the source imported")


def main() -> None:
    server = build_server()
    startup.mark("ready")
    startup.report_bytecode()
    serve(server, _rpc_out)


if __name__ == "__main__":
    main()
