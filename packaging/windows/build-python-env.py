#!/usr/bin/env python3
"""Assemble the Windows Python runtime for the installer.

Installs every backend and its dependencies into a win_amd64 --target
directory using Windows wheels only. Some dependencies (e.g. yandex-music)
publish sdists, not win_amd64 wheels; for those the script builds a
py3-none-any wheel here on Linux (they are pure Python) and retries.

Usage: build-python-env.py TARGET WHEEL_DIR [--extra-wheel-dir DIR]...
"""

import re
import subprocess
import sys
from pathlib import Path

BACKENDS = [
    "cloudmus-rpc-common",
    "cloudmus-backend-local-folder",
    "cloudmus-backend-yandex-music",
    "cloudmus-backend-youtube-music",
]


def pip(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, "-m", "pip", *args], text=True, capture_output=True)


def main() -> int:
    target = Path(sys.argv[1])
    own_wheels = Path(sys.argv[2])
    extra_wheel_dirs = [Path(a) for a in sys.argv[3:]]

    find_links = [f"--find-links={dir_}" for dir_ in (own_wheels, *extra_wheel_dirs)]
    base = ["install", "--ignore-installed", "--no-compile", "--target", str(target),
            "--platform", "win_amd64", "--python-version", "3.13",
            "--implementation", "cp", "--abi", "cp313", "--only-binary=:all:", *find_links]

    built: set[str] = set()
    for attempt in range(6):
        result = pip(*base, *BACKENDS)
        if result.returncode == 0:
            return 0
        missing = {
            name for name in re.findall(
                r"Could not find a version that satisfies the requirement "
                r"(?P<name>[A-Za-z0-9_.-]+)", result.stderr)
            if name not in built
        }
        if not missing:
            print(result.stdout, end="")
            print(result.stderr, end="")
            return result.returncode
        # The missing distributions are pure Python (nothing else ships win
        # wheels); building them here yields py3-none-any wheels, which pip
        # accepts despite the win_amd64 constraints above.
        for name in sorted(missing):
            print(f"==> Building {name} (no win_amd64 wheel)", file=sys.stderr)
            wheel = pip("wheel", "--no-deps", "--wheel-dir", str(own_wheels), name)
            if wheel.returncode != 0:
                print(wheel.stdout, end="")
                print(wheel.stderr, end="", file=sys.stderr)
                return wheel.returncode
            built.add(name)
    print("Too many retries assembling the Python environment", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())