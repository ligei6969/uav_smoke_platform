"""Replay real analyze_video.py detection JSON into the event gateway."""

import argparse
import json
from collections import Counter
from pathlib import Path

from client import Client, detection_lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--detections", required=True, type=Path)
    parser.add_argument("--source", required=True, help="stable ID for this video/run")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    with args.detections.open(encoding="utf-8") as stream:
        payload = json.load(stream)
    counts = Counter()
    with Client(args.host, args.port) as client:
        for line in detection_lines(payload, args.source):
            result = client.request(line)
            status = result.get("status", "unknown")
            counts[status] += 1
            if status not in {"accepted", "duplicate", "stale"}:
                raise RuntimeError("replay stopped: {}".format(result))
        print(json.dumps({"responses": counts, "stats": client.request("STATS")}, indent=2))


if __name__ == "__main__":
    main()
