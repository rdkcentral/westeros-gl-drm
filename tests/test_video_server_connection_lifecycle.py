#!/usr/bin/env python3
"""Regression coverage for video connection admission and thread ownership."""

from pathlib import Path
import re
import unittest


SOURCE = (Path(__file__).resolve().parents[1] / "westeros-gl" / "westeros-gl.c").read_text()


def function_body(name):
    match = re.search(r"static\s+[^\n]+\b" + re.escape(name) + r"\s*\([^)]*\)\s*\{", SOURCE)
    if not match:
        raise AssertionError("function not found: " + name)
    start = SOURCE.index("{", match.start())
    depth = 0
    for offset, character in enumerate(SOURCE[start:], start=start):
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[start + 1:offset]
    raise AssertionError("unterminated function: " + name)


class VideoServerConnectionLifecycleTest(unittest.TestCase):
    def test_connection_is_admitted_before_its_thread_starts(self):
        body = function_body("wstVideoServerThread")
        capacity_check = body.index("i >= MAX_VIDEO_CONNECTIONS")
        create = body.index("wstCreateVideoServerConnection")
        publish = body.index("server->connections[i]= conn")
        start = body.index("wstStartVideoServerConnection")
        self.assertLess(capacity_check, create)
        self.assertLess(create, publish)
        self.assertLess(publish, start)

    def test_overflow_rejection_never_creates_or_destroys_connection(self):
        body = function_body("wstVideoServerThread")
        rejection = re.search(
            r"if\s*\(\s*i\s*>=\s*MAX_VIDEO_CONNECTIONS\s*\)\s*\{(?P<body>.*?)\}",
            body,
            re.DOTALL,
        )
        self.assertIsNotNone(rejection)
        rejection_body = rejection.group("body")
        self.assertIn("close( fd )", rejection_body)
        self.assertNotIn("wstCreateVideoServerConnection", rejection_body)
        self.assertNotIn("wstDestroyVideoServerConnection", rejection_body)

    def test_allocation_does_not_start_an_unpublished_thread(self):
        body = function_body("wstCreateVideoServerConnection")
        self.assertNotIn("pthread_create", body)
        self.assertIn("conn->server= server", body)

    def test_thread_is_marked_active_before_creation(self):
        body = function_body("wstStartVideoServerConnection")
        mark_active = body.index("conn->threadStarted= true")
        create = body.index("pthread_create(")
        create_failure = body.index("if ( rc )", create)
        clear_active = body.index("conn->threadStarted= false", create_failure)
        self.assertLess(mark_active, create)
        self.assertLess(create, clear_active)

    def test_start_failure_removes_slot_before_destroy(self):
        body = function_body("wstVideoServerThread")
        failure = re.search(
            r"if\s*\(\s*!wstStartVideoServerConnection\(\s*conn\s*\)\s*\)\s*\{(?P<body>.*?)\}",
            body,
            re.DOTALL,
        )
        self.assertIsNotNone(failure)
        failure_body = failure.group("body")
        remove = failure_body.index("server->connections[i]= 0")
        unlock = failure_body.index("pthread_mutex_unlock")
        destroy = failure_body.index("wstDestroyVideoServerConnection")
        self.assertLess(remove, unlock)
        self.assertLess(unlock, destroy)


if __name__ == "__main__":
    unittest.main()
