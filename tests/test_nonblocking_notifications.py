#!/usr/bin/env python3
from pathlib import Path
import unittest

ROOT = Path(__file__).parents[1]
SOURCE = (ROOT / "westeros-gl" / "westeros-gl.c").read_text()
SEND = (ROOT / "westeros-gl" / "nonblocking-send.h").read_text()

class NotificationSendTests(unittest.TestCase):
    def test_all_socket_notifications_use_bounded_send(self):
        self.assertEqual(SOURCE.count("wstSendMessageNonBlocking( conn->socketFd, &msg, len )"), 8)
        self.assertNotIn("sendmsg( conn->socketFd", SOURCE)

    def test_send_failure_terminates_connection(self):
        self.assertIn("MSG_NOSIGNAL|MSG_DONTWAIT", SEND)
        self.assertIn("sent != (ssize_t)expected", SEND)
        self.assertIn("shutdown( fd, SHUT_RDWR )", SEND)

if __name__ == "__main__":
    unittest.main()
