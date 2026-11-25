"""Bounded, sequential-per-source client for the C++ detection gateway."""

import json
import socket


class Client:
    def __init__(self, host="127.0.0.1", port=8765, timeout=5.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.buffer = b""

    def request(self, line):
        if "\n" in line or "\r" in line:
            raise ValueError("one line per request")
        self.sock.sendall((line + "\n").encode("ascii"))
        while b"\n" not in self.buffer:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("server disconnected before response")
            self.buffer += chunk
            if len(self.buffer) > 65536:
                raise ValueError("response exceeds size limit")
        response, self.buffer = self.buffer.split(b"\n", 1)
        return json.loads(response)

    def close(self):
        self.sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        self.close()


def detection_lines(payload, source):
    """Adapt the existing Step 1 JSON. One strongest smoke box per frame.

    Frame order is kept, and IDs are stable on retries. No inference results
    or missing telemetry are fabricated by this bridge.
    """
    if not source or len(source) > 64 or not all(
        c.isascii() and (c.isalnum() or c in "_.-") for c in source
    ):
        raise ValueError("invalid source identifier")
    width, height = int(payload["width"]), int(payload["height"])
    previous = -1
    for frame in payload["detections"]:
        frame_id = int(frame["frame_id"])
        if frame_id <= previous or frame_id < 0:
            raise ValueError("frame IDs must increase strictly")
        previous = frame_id
        boxes = frame["boxes"]
        if not boxes:
            continue
        if any("smoke" not in str(b.get("class_name", "")).lower() for b in boxes):
            raise ValueError("bridge accepts smoke detections only")
        strongest = max(boxes, key=lambda b: float(b["confidence"]))
        values = [
            "DETECT", source, str(frame_id + 1),
            str(round(float(frame["timestamp_sec"]) * 1000)),
            format(float(strongest["confidence"]), ".17g"),
            *(format(float(x), ".17g") for x in strongest["bbox"]),
            str(width), str(height),
        ]
        yield " ".join(values)
