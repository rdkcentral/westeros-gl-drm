#!/usr/bin/env python3
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).parents[1]
SOURCE = (ROOT / "westeros-gl" / "westeros-gl.c").read_text()
STATE = (ROOT / "westeros-gl" / "video-message-state.h").read_text()


class VideoMessageStateTests(unittest.TestCase):
    def test_plane_dependent_messages_are_classified(self):
        helper = re.search(
            r"static inline bool wstVideoServerDispatchMessage\(.*?\)\s*\{(.*?)\n\}",
            STATE,
            re.S,
        )
        self.assertIsNotNone(helper)
        classified = set(re.findall(r"case '([A-Z])':", helper.group(1)))
        self.assertEqual(classified, set("FHSPIAWRKE"))

    def test_flush_defensively_rejects_missing_plane(self):
        flush = re.search(
            r"static void wstVideoServerFlush\( VideoServerConnection \*conn \)\s*\{(.*?)\n\}",
            SOURCE,
            re.S,
        )
        self.assertIsNotNone(flush)
        body = flush.group(1)
        guard = body.index("if ( !conn->videoPlane )")
        dereference = body.index("conn->videoPlane->vfm")
        self.assertLess(guard, dereference)
        self.assertIn("return;", body[guard:dereference])

    def test_state_guard_precedes_video_dispatch(self):
        guard = SOURCE.index("!wstVideoServerDispatchMessage( id")
        dispatch = SOURCE.index("switch( id )", guard)
        first_dereference = SOURCE.index("conn->videoPlane->frameCount", guard)
        self.assertLess(guard, dispatch)
        self.assertLess(guard, first_dereference)
        guarded = SOURCE[guard:dispatch]
        self.assertIn("continue;", guarded)
        self.assertEqual(guarded.count("close( fd"), 3)


if __name__ == "__main__":
    unittest.main()
