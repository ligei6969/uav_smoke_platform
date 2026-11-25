"""Black-box TCP regression tests. Run with --server /absolute/path/to/server."""

import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import tempfile
import threading
import time
import unittest


SERVER_PATH = None


def detection(source="drone-1", sequence=1, confidence="0.9"):
    return "DETECT {} {} {} {} 10 20 110 120 1920 1080\n".format(
        source, sequence, 1000 + sequence, confidence
    ).encode("ascii")


def receive_json(connection, pending=b""):
    """Read a bounded newline-delimited JSON response without over-reading."""
    data = bytearray(pending)
    while b"\n" not in data:
        chunk = connection.recv(4096)
        if not chunk:
            raise EOFError("Server closed the connection before a complete response")
        data.extend(chunk)
        if len(data) > 65536:
            raise ValueError("Response exceeded 64 KiB")
    line, remainder = bytes(data).split(b"\n", 1)
    return json.loads(line), remainder


class ServerProcess:
    def __init__(self, directory):
        self.directory = Path(directory)
        self.journal = self.directory / "events.journal"
        self.ready = self.directory / "ready.json"
        self.log = self.directory / "server.log"
        self.process = None
        self.log_stream = None
        self.port = None

    def command(self, ready=None):
        return [
            str(SERVER_PATH),
            "--bind", "127.0.0.1", "--port", "0",
            "--journal", str(self.journal),
            "--ready-file", str(ready or self.ready),
            "--workers", "2", "--queue-capacity", "8",
            "--max-clients", "32", "--idle-ms", "300",
        ]

    def start(self):
        self.ready.unlink(missing_ok=True)
        self.log_stream = self.log.open("ab")
        self.process = subprocess.Popen(
            self.command(), stdout=self.log_stream, stderr=subprocess.STDOUT
        )
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError("Server startup failed: " + self.logs())
            try:
                ready = json.loads(self.ready.read_text(encoding="utf-8"))
                self.port = int(ready["port"])
                if 0 < self.port <= 65535:
                    return self
            except (OSError, ValueError, KeyError):
                pass
            time.sleep(0.02)
        raise TimeoutError("Server was not ready within 10 seconds: " + self.logs())

    def logs(self):
        if self.log_stream is not None:
            self.log_stream.flush()
        return self.log.read_text(encoding="utf-8", errors="replace")[-8000:]

    def connect(self):
        connection = socket.create_connection(("127.0.0.1", self.port), timeout=2.0)
        connection.settimeout(2.0)
        return connection

    def request(self, command):
        with self.connect() as connection:
            connection.sendall(command)
            response, unused = receive_json(connection)
            if unused:
                raise AssertionError("Unexpected additional response bytes")
            return response

    def stop(self, kill=False):
        process = self.process
        try:
            if process is not None and process.poll() is None:
                if kill:
                    process.kill()
                else:
                    process.terminate()
                try:
                    process.wait(timeout=5.0)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5.0)
        finally:
            if self.log_stream is not None:
                self.log_stream.close()
                self.log_stream = None


class NetworkTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="smoke-gateway-test-")
        self.addCleanup(self.temp.cleanup)
        self.server = ServerProcess(self.temp.name)
        self.addCleanup(self.server.stop)
        self.server.start()

    def test_ping_stats_and_coalesced_fragmented_requests(self):
        with self.server.connect() as connection:
            connection.sendall(b"P")
            connection.sendall(b"ING\nPING\n")
            first, pending = receive_json(connection)
            second, pending = receive_json(connection, pending)
            self.assertEqual(first["status"], "ok")
            self.assertEqual(second["status"], "ok")
            self.assertEqual(pending, b"")

            command = detection()
            connection.sendall(command[:12])
            connection.sendall(command[12:27])
            connection.sendall(command[27:])
            accepted, unused = receive_json(connection)
            self.assertEqual(accepted["status"], "accepted")
            self.assertEqual(unused, b"")

        stats = self.server.request(b"STATS\n")
        self.assertIsInstance(stats, dict)
        self.assertNotIn(stats.get("status"), ("invalid", "error"))

    def test_invalid_nan_and_oversized_input_are_contained(self):
        for command in (b"UNKNOWN\n", detection(confidence="nan"), b"DETECT\n"):
            with self.subTest(command=command):
                self.assertEqual(self.server.request(command)["status"], "invalid")

        with self.server.connect() as connection:
            try:
                connection.sendall(b"X" * 65536 + b"\n")
                response, unused = receive_json(connection)
                self.assertEqual(response["status"], "invalid")
                self.assertEqual(unused, b"")
            except (EOFError, ConnectionResetError, BrokenPipeError):
                # An over-limit request may be rejected by closing its connection.
                pass
        self.assertEqual(self.server.request(b"PING\n")["status"], "ok")

    def test_idle_partial_request_does_not_block_other_clients(self):
        with self.server.connect() as slow:
            slow.sendall(b"DET")
            self.assertEqual(self.server.request(b"PING\n")["status"], "ok")
            time.sleep(0.7)
            try:
                self.assertEqual(slow.recv(1), b"")
            except ConnectionResetError:
                pass
        self.assertEqual(self.server.request(b"PING\n")["status"], "ok")

    def test_concurrent_retries_are_idempotent(self):
        clients = 12
        barrier = threading.Barrier(clients, timeout=5.0)

        def submit_same_event():
            barrier.wait()
            deadline = time.monotonic() + 5.0
            while True:
                response = self.server.request(detection("same-drone", 10))
                status = response["status"]
                if status != "busy" or time.monotonic() >= deadline:
                    return status
                time.sleep(0.01)

        with concurrent.futures.ThreadPoolExecutor(max_workers=clients) as executor:
            futures = [executor.submit(submit_same_event) for unused in range(clients)]
            statuses = [future.result(timeout=8.0) for future in futures]
        self.assertEqual(statuses.count("accepted"), 1, statuses)
        self.assertEqual(statuses.count("duplicate"), clients - 1, statuses)

    def test_acknowledged_event_survives_process_kill(self):
        command = detection("durable-drone", 42)
        self.assertEqual(self.server.request(command)["status"], "accepted")
        self.server.stop(kill=True)
        self.server.start()
        self.assertEqual(self.server.request(command)["status"], "duplicate")

    def test_second_journal_owner_is_rejected(self):
        second_ready = Path(self.temp.name) / "second-ready.json"
        second = subprocess.Popen(
            self.server.command(ready=second_ready),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        try:
            try:
                returncode = second.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                self.fail("A second process using the same journal did not exit")
            self.assertNotEqual(returncode, 0)
            self.assertFalse(second_ready.exists())
        finally:
            if second.poll() is None:
                second.kill()
                second.wait(timeout=5.0)
        self.assertEqual(self.server.request(b"PING\n")["status"], "ok")

    @unittest.skipUnless(os.name == "posix", "POSIX SIGTERM semantics")
    def test_sigterm_drains_and_exits_cleanly(self):
        self.assertEqual(
            self.server.request(detection("shutdown-drone", 1))["status"],
            "accepted",
        )
        self.server.process.send_signal(signal.SIGTERM)
        returncode = self.server.process.wait(timeout=5.0)
        self.assertEqual(returncode, 0, self.server.logs())
        self.server.stop()
        self.server.start()
        self.assertEqual(
            self.server.request(detection("shutdown-drone", 1))["status"],
            "duplicate",
        )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, type=Path)
    arguments, unittest_args = parser.parse_known_args()
    SERVER_PATH = arguments.server.resolve()
    if not SERVER_PATH.is_file():
        parser.error("server executable does not exist: {}".format(SERVER_PATH))
    unittest.main(argv=[__file__] + unittest_args)
