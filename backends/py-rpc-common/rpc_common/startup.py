"""Where a backend's start goes, into its log: a phone's takes seconds, and
which part of it can't be seen from outside."""

import logging
import os
import sys
import time

logger = logging.getLogger(__name__)


def _process_started() -> float | None:
    """When this process started, in CLOCK_BOOTTIME's seconds. Linux only
    (Android with it): elsewhere nothing is told."""
    try:
        with open("/proc/self/stat", encoding="ascii") as stat:
            # The 22nd field; the 2nd, the name in brackets, may hold spaces.
            ticks = int(stat.read().rsplit(")", 1)[1].split()[19])
    except (OSError, ValueError, IndexError):
        return None
    try:
        per_second = os.sysconf("SC_CLK_TCK")
    except (OSError, ValueError, AttributeError):
        per_second = 100  # Android's Python knows no names; Linux's is 100
    return ticks / per_second if hasattr(time, "CLOCK_BOOTTIME") else None


_STARTED = _process_started()


def mark(step: str) -> None:
    """Logs `step` with how long after the process started it was reached."""
    if _STARTED is None:
        return
    age = time.clock_gettime(time.CLOCK_BOOTTIME) - _STARTED
    # The processor's share of it: far under means waiting for one, or for
    # the disk.
    try:
        with open("/proc/self/cgroup", encoding="ascii") as groups:
            cpuset = next((line.strip().split(":", 2)[2] for line in groups if ":cpuset:" in line), "?")
    except OSError:
        cpuset = "?"
    logger.info("startup: %s after %d ms, %d ms of processor time, cpuset %s", step, age * 1000,
                time.process_time() * 1000, cpuset)


def report_bytecode() -> None:
    """Logs whether this start had compiled modules to load, or compiled them
    itself: the difference between a quick start and one of seconds."""
    if _STARTED is None:
        return
    cached = sys.modules[__package__].__spec__.cached
    try:
        written = os.stat(cached).st_mtime if cached else None
    except OSError:
        written = None
    if written is None:
        state = "none"
    elif written < time.time() - (time.clock_gettime(time.CLOCK_BOOTTIME) - _STARTED) - 1:
        state = "from before"
    else:
        state = "written by this start"
    logger.info("startup: compiled modules: %s, writing them %s", state, "off" if sys.dont_write_bytecode else "on")
