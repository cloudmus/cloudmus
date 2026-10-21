"""The licenses of the Rust crates the Spotify backend is built from, as one
text to ship next to it (see packaging/build-spotify.sh).

Reads `cargo metadata` JSON, follows the backend's normal (linked)
dependencies only — build scripts and dev-dependencies ship nothing — and
prints each crate's name, version and license expression, followed by the
license files the crate carries (LICENSE*, LICENCE*, COPYING*, NOTICE*).
A few crates publish none; their texts are kept in spotify-licenses/
(FALLBACKS below). A crate with neither fails the build: a new
dependency's license must not go missing unnoticed.

Usage: python3 spotify-licenses.py metadata.json > THIRD_PARTY-spotify.txt
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = "cloudmus-backend-spotify"
LICENSE_PREFIXES = ("license", "licence", "copying", "notice")
FALLBACK_DIR = Path(__file__).resolve().parent / "spotify-licenses"
ASSET_LICENSE = Path(__file__).resolve().parent.parent / "backends" / "spotify" / "assets" / "fonts" / "Rubik-OFL.txt"
# Crate name (or name prefix ending in "-") -> its license text, taken from
# the crate's repository.
FALLBACKS = {
    "librespot-": "librespot.txt",
    "governor": "governor.txt",
    # winapi's import-library crates; MIT OR Apache-2.0, as winapi itself.
    "winapi-": "winapi.txt",
    # LGPL-3.0-or-later OR MPL-2.0: used under MPL-2.0.
    "priority-queue": "MPL-2.0.txt",
}


def fallback(name: str) -> Path | None:
    for key, file in FALLBACKS.items():
        if name == key or (key.endswith("-") and name.startswith(key)):
            return FALLBACK_DIR / file
    return None


def shipped(metadata: dict) -> list[dict]:
    packages = {p["id"]: p for p in metadata["packages"]}
    nodes = {n["id"]: n for n in metadata["resolve"]["nodes"]}
    root = next(pid for pid, p in packages.items() if p["name"] == ROOT)
    seen, stack = set(), [root]
    while stack:
        pid = stack.pop()
        if pid in seen:
            continue
        seen.add(pid)
        for dep in nodes[pid]["deps"]:
            if any(kind["kind"] is None for kind in dep["dep_kinds"]):
                stack.append(dep["pkg"])
    seen.discard(root)
    return sorted((packages[pid] for pid in seen), key=lambda p: (p["name"], p["version"]))


def license_files(package: dict) -> list[Path]:
    directory = Path(package["manifest_path"]).parent
    files = sorted(f for f in directory.iterdir() if f.is_file() and f.name.lower().startswith(LICENSE_PREFIXES))
    if not files and (kept := fallback(package["name"])) is not None:
        files = [kept]
    if not files:
        raise SystemExit(f"{package['name']} {package['version']} carries no license text: add one to {FALLBACK_DIR}")
    return files


def main() -> None:
    metadata = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    crates = shipped(metadata)
    # Most crates carry the same MIT/Apache texts: each text is printed once,
    # under the crates that carry it.
    texts: dict[str, list[str]] = {}
    out = [
        "The Spotify backend (cloudmus-backend-spotify) is built from these Rust",
        "crates, librespot (https://github.com/librespot-org/librespot) among",
        "them. Each with its license; the license texts the crates carry follow.",
        "",
    ]
    for package in crates:
        name = f"{package['name']} {package['version']}"
        out.append(f"{name} — {package.get('license') or 'see its license text below'}")
        for path in license_files(package):
            text = path.read_text(encoding="utf-8", errors="replace").strip()
            texts.setdefault(text, []).append(name)
    for text, names in texts.items():
        out += ["", "=" * 72, "Carried by: " + ", ".join(names), "", text]
    # Not a crate: the font inside the sign-in page (src/page.rs).
    out += [
        "",
        "=" * 72,
        "Rubik (the font of the page shown at the end of a sign-in), SIL Open Font License 1.1",
        "",
        ASSET_LICENSE.read_text(encoding="utf-8").strip(),
    ]
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
