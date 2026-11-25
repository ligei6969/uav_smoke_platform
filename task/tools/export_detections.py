"""Convert an existing analyze_video JSON into bounded replay metadata (no inference)."""

import argparse
import json
import math
import re
from pathlib import Path


def rows(payload, source, lens, epoch=0):
    if not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", source):
        raise ValueError("invalid source")
    if lens not in {"unknown", "wide", "zoom", "ir", "fpv"}:
        raise ValueError("invalid lens")
    if not isinstance(epoch, int) or isinstance(epoch, bool) or not 0 <= epoch < 2**64:
        raise ValueError("invalid epoch")
    width, height = payload["width"], payload["height"]
    if any(not isinstance(v, int) or isinstance(v, bool) or not 2 <= v <= 16384
           for v in (width, height)):
        raise ValueError("invalid dimensions")
    previous = -1
    previous_time = -1.0
    for frame in payload["detections"]:
        frame_id = frame["frame_id"]
        if not isinstance(frame_id, int) or isinstance(frame_id, bool) or not 0 <= frame_id < 2**64 or frame_id <= previous:
            raise ValueError("frame IDs must be uint64 and strictly increasing")
        previous = frame_id
        timestamp = float(frame["timestamp_sec"])
        if not math.isfinite(timestamp) or timestamp < 0 or timestamp < previous_time:
            raise ValueError("invalid or decreasing timestamp")
        previous_time = timestamp
        boxes = frame["boxes"]
        if not boxes:
            continue
        if any(str(b.get("class_name", "")).lower() not in {"smoke", "烟雾"} for b in boxes):
            raise ValueError("this bridge only accepts explicit smoke labels")
        strongest = None
        for box in boxes:
            bbox = [float(v) for v in box["bbox"]]
            confidence = float(box["confidence"])
            if len(bbox) != 4 or not all(math.isfinite(v) for v in bbox + [confidence]):
                raise ValueError("non-finite box or confidence")
            x1, y1, x2, y2 = bbox
            if not (0 <= x1 < x2 <= width and 0 <= y1 < y2 <= height and 0 <= confidence <= 1):
                raise ValueError("box outside image or invalid confidence")
            if strongest is None or confidence > strongest[0]:
                strongest = (confidence, bbox)
        confidence, bbox = strongest
        values = [source, str(epoch), str(frame_id), format(timestamp, ".17g"), lens,
                  str(width), str(height), *(format(v, ".17g") for v in bbox), format(confidence, ".17g")]
        yield ",".join(values)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--detections", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--lens", choices=["unknown", "wide", "zoom", "ir", "fpv"], default="unknown")
    parser.add_argument("--epoch", type=int, default=0,
                        help="session epoch stamped into every CSV row")
    args = parser.parse_args()
    if args.detections.stat().st_size > 64 * 1024 * 1024:
        raise ValueError("metadata JSON exceeds 64MiB")
    payload = json.loads(args.detections.read_text(encoding="utf-8"))
    count = 0
    # 'x' ensures input/report files are never silently replaced.
    with args.output.open("x", encoding="utf-8", newline="\n") as output:
        output.write("# recorded detection metadata; strongest smoke box per positive frame; no telemetry fabricated\n")
        for row in rows(payload, args.source, args.lens, args.epoch):
            output.write(row + "\n")
            count += 1
    print(json.dumps({"mode": "metadata_export", "records": count,
                      "input_model": payload.get("model"), "lens": args.lens,
                      "epoch": args.epoch}, ensure_ascii=False))


if __name__ == "__main__":
    main()
