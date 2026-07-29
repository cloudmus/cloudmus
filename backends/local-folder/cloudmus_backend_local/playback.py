from __future__ import annotations

from pathlib import Path

from rpc_common.models import StreamDescriptor

from . import scanner


def resolve_stream(root: Path, track_id: str) -> StreamDescriptor | None:
    path = scanner.resolve_track_path(root, track_id)
    if path is None:
        return None
    return StreamDescriptor(kind="url", url=path.as_uri(), mimeType=scanner.guess_mime_type(path))
