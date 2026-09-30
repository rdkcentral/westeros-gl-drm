#!/usr/bin/env python3
from pathlib import Path
import re
import unittest

SOURCE=(Path(__file__).parents[1]/"westeros-gl"/"westeros-gl.c").read_text()

class OffloadOverflowTests(unittest.TestCase):
    def test_full_queue_discards_without_inline_execution(self):
        start=SOURCE.index("fullness >= OFFLOAD_QUEUE_CAPACITY - 1")
        full=SOURCE[start:SOURCE.index("pCur=", start)]
        self.assertIn("wstOffloadMsgDiscard", full)
        self.assertNotIn("wstOffloadMsgExecute", full)

    def test_discard_releases_owned_resources(self):
        discard=re.search(r"static void wstOffloadMsgDiscard\(.*?\n\}", SOURCE, re.S).group(0)
        for operation in ("wstOffloadFreeVideoFrameResources", "free(f)", "close(param_int)", "free(param_pv)"):
            self.assertIn(operation, discard)
        for sink in ("wstOffloadMsgExecute", "wstVideoServerSend", "sendmsg", "pthread_mutex_lock", "pthread_mutex_trylock"):
            self.assertNotIn(sink, discard)

    def test_overflow_returns_immediately_after_discard(self):
        start=SOURCE.index("fullness >= OFFLOAD_QUEUE_CAPACITY - 1")
        overflow=SOURCE[start:SOURCE.index("pCur=", start)]
        discard=overflow.index("wstOffloadHandleOverflow")
        self.assertIn("return;", overflow[discard:])
        for sink in ("wstOffloadMsgExecute", "wstVideoServerSend", "sendmsg", "pthread_mutex_lock"):
            self.assertNotIn(sink, overflow)

if __name__ == "__main__": unittest.main()
