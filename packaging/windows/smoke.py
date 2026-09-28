"""Exercise each backend through its packaged Windows Python and stdio pipes."""

import json
import pathlib
import subprocess
import sys


def main(install_dir: pathlib.Path) -> None:
    python = install_dir / "python" / "python.exe"
    for backend_id in ("local-folder", "yandex-music", "youtube-music"):
        manifest = json.loads((install_dir / "backends" / f"{backend_id}.json").read_text(encoding="utf-8"))
        argv = manifest["argv"]
        argv[0] = str((install_dir / "backends" / argv[0]).resolve())
        assert pathlib.Path(argv[0]).resolve() == python.resolve(), argv
        request = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {"protocolVersion": "1.8", "front": {"name": "windows-smoke", "version": "0.0.0"}},
        }
        proc = subprocess.Popen(
            argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            creationflags=subprocess.CREATE_NO_WINDOW,
        )
        errors = b""
        try:
            output, errors = proc.communicate(json.dumps(request).encode("utf-8") + b"\n", timeout=30)
            messages = [json.loads(line) for line in output.decode("utf-8").splitlines()]
            message = messages[0] if messages else {}
            assert message.get("id") == 1 and "result" in message, (backend_id, message)
            print(f"{backend_id}: initialize OK")
        except Exception:
            print(errors.decode("utf-8", "replace"), file=sys.stderr)
            raise
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.communicate(timeout=10)


if __name__ == "__main__":
    main(pathlib.Path(sys.argv[1]))
