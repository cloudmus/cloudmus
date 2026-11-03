"""What Developer tools' Python probe runs: can the backends live on this device?"""

import importlib
import json
import os
import ssl
import subprocess
import sys
import time

BACKENDS = ["cloudmus_backend_local", "cloudmus_backend_yandex", "cloudmus_backend_ytmusic"]


def say(*args):
    print(*args, flush=True)


def rss_mb(pid="self"):
    with open(f"/proc/{pid}/status") as status:
        for line in status:
            if line.startswith("VmRSS"):
                return int(line.split()[1]) // 1024
    return -1


say("python", sys.version, sys.platform, "executable", sys.executable)
say("openssl", ssl.OPENSSL_VERSION, "SSL_CERT_FILE", os.environ.get("SSL_CERT_FILE"))

for name in ["sqlite3", "ctypes", "zlib", "hashlib", "socket", "requests", "mutagen", "yandex_music",
             "ytmusicapi", "yt_dlp", "rpc_common"]:
    started = time.monotonic()
    try:
        importlib.import_module(name)
        say(f"import {name}: ok, {int((time.monotonic() - started) * 1000)} ms")
    except Exception as error:
        say(f"import {name}: FAILED {error!r}")
say("memory after the imports:", rss_mb(), "MB")

try:
    import requests

    started = time.monotonic()
    reply = requests.head("https://music.yandex.ru/", timeout=10)
    say(f"https: {reply.status_code}, {int((time.monotonic() - started) * 1000)} ms")
except Exception as error:
    say(f"https: FAILED {error!r}")

initialize = {
    "jsonrpc": "2.0", "id": 1, "method": "initialize",
    "params": {"protocolVersion": "1.18", "front": {"name": "probe", "version": "0"}, "locale": "en"},
}
for module in BACKENDS:
    started = time.monotonic()
    try:
        process = subprocess.Popen([sys.executable, "-m", module], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        process.stdin.write(json.dumps(initialize) + "\n")
        process.stdin.flush()
        line = process.stdout.readline()
        took = int((time.monotonic() - started) * 1000)
        source = json.loads(line).get("result", {}).get("source", {}).get("id") if line else None
        say(f"backend {module}: {source or 'NO ANSWER'}, {took} ms, {rss_mb(process.pid)} MB")
        process.stdin.close()
        process.wait(timeout=5)
    except Exception as error:
        say(f"backend {module}: FAILED {error!r}")

say("PROBE DONE")
