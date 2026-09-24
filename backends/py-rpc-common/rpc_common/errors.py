"""Application error code ranges. See docs/protocol.md §9.

Standard JSON-RPC 2.0 codes (-32700..-32600 range) are used as-is via
jsonrpc.make_error and are not redefined here.
"""
from __future__ import annotations

from typing import Any

# 1000-1099: auth errors
AUTH_NOT_AUTHENTICATED = 1000
AUTH_EXPIRED = 1001
AUTH_FLOW_FAILED = 1002

# 1100-1199: capability errors
CAPABILITY_NOT_SUPPORTED = 1100

# 1200-1299: upstream/network errors
UPSTREAM_UNREACHABLE = 1200
UPSTREAM_RATE_LIMITED = 1201

# 1300-1399: resource errors
RESOURCE_NOT_FOUND = 1300

# 1400-1499: state errors
STATE_INVALID = 1400


def app_error_data(retryable: bool, detail: str | None = None) -> dict[str, Any]:
    data: dict[str, Any] = {"retryable": retryable}
    if detail is not None:
        data["detail"] = detail
    return data


def in_range(code: int, low: int, high: int) -> bool:
    return low <= code <= high


class ProtocolValidationError(ValueError):
    """Raised by generated rpc_common.generated.models.*.from_dict() when a
    message doesn't match protocol/schema/*.yaml — a required field is
    missing, or a field's runtime type/enum value doesn't match. Carries a
    field path (e.g. "Track.durationMs") and the mismatch in its message."""
