#!/usr/bin/env python3
"""Regression checks for serialized VideoFrameManager queue access."""

import pathlib
import re
import unittest


SOURCE = pathlib.Path(__file__).parents[1] / "westeros-gl" / "westeros-gl.c"


def function_body(source, name):
    definition = re.search(
        rf"^static\s+[^;\n]+\b{re.escape(name)}\s*\([^;]*\)\s*\{{",
        source,
        re.MULTILINE,
    )
    if not definition:
        raise AssertionError(f"function definition not found: {name}")
    brace = source.index("{", definition.start())
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]
    raise AssertionError(f"unterminated function {name}")


class VfmQueueLockingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8")

    def test_push_serializes_capacity_check_reallocation_free_and_append(self):
        body = function_body(self.source, "wstVideoFrameManagerPushFrame")
        lock = body.index("pthread_mutex_lock( &vfm->mutex)")
        capacity_check = body.index("vfm->queueSize+1 > vfm->queueCapacity")
        old_queue_free = body.index("free( toFree )")
        append = body.index("vfm->queue[vfm->queueSize++]= *f")
        final_unlock = body.rindex("pthread_mutex_unlock( &vfm->mutex)")
        self.assertLess(lock, capacity_check)
        self.assertLess(capacity_check, old_queue_free)
        self.assertLess(old_queue_free, append)
        self.assertLess(append, final_unlock)

    def test_pop_serializes_non_avsync_queue_scan_and_mutation(self):
        body = function_body(self.source, "wstVideoFrameManagerPopFrame")
        lock = body.index("pthread_mutex_lock( &vfm->mutex)")
        scan = body.index("while ( i < vfm->queueSize )")
        dropped_write = body.index("fCheck->dropped= true")
        mutation = body.index("memmove( &vfm->queue[i]")
        unlock = body.index("pthread_mutex_unlock( &vfm->mutex)", mutation)
        self.assertLess(lock, scan)
        self.assertLess(scan, dropped_write)
        self.assertLess(dropped_write, mutation)
        self.assertLess(mutation, unlock)
        critical_section = body[lock:unlock]
        self.assertEqual(critical_section.count("pthread_mutex_lock( &vfm->mutex)"), 1)
        self.assertNotRegex(critical_section, re.escape("pthread_mutex_unlock( &vfm->mutex)"))


if __name__ == "__main__":
    unittest.main()
