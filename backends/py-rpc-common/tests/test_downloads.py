import asyncio

import pytest

from rpc_common import errors
from rpc_common.downloads import Cancelled, Downloads
from rpc_common.server import BackendError


class _Notes:
    def __init__(self):
        self.sent = []

    async def __call__(self, method, params):
        self.sent.append((method, params))


@pytest.mark.asyncio
async def test_progress_is_reported_throttled_with_the_final_one_always():
    notes = _Notes()
    downloads = Downloads(notes)
    with downloads.track("d1") as tracker:

        def transfer():
            for received in range(0, 1000, 100):
                tracker.progress(received, 1000)  # all within one throttle window
            tracker.progress(1000, 1000, final=True)

        await asyncio.to_thread(transfer)
        await asyncio.sleep(0.05)  # let the scheduled notifications run
    assert [m for m, _ in notes.sent] == ["download/progress", "download/progress"]
    assert notes.sent[0][1] == {"downloadId": "d1", "receivedBytes": 0, "totalBytes": 1000}
    assert notes.sent[-1][1] == {"downloadId": "d1", "receivedBytes": 1000, "totalBytes": 1000}


@pytest.mark.asyncio
async def test_an_unknown_total_is_left_out():
    notes = _Notes()
    with Downloads(notes).track("d1") as tracker:
        await asyncio.to_thread(tracker.progress, 10, None, final=True)
        await asyncio.sleep(0.05)
    assert notes.sent[0][1] == {"downloadId": "d1", "receivedBytes": 10}


@pytest.mark.asyncio
async def test_a_cancelled_download_fails_with_1410():
    downloads = Downloads(_Notes())
    with pytest.raises(BackendError) as failure:
        with downloads.track("d1") as tracker:
            downloads.cancel("d1")
            tracker.progress(1, 2)  # the transfer's next chunk
    assert failure.value.code == errors.DOWNLOAD_CANCELLED


@pytest.mark.asyncio
async def test_cancelling_an_unknown_download_does_nothing():
    Downloads(_Notes()).cancel("nope")


@pytest.mark.asyncio
async def test_without_a_download_id_nothing_is_reported():
    notes = _Notes()
    with Downloads(notes).track(None) as tracker:
        await asyncio.to_thread(tracker.progress, 10, 20, final=True)
        await asyncio.sleep(0.05)
    assert notes.sent == []
