"""Measure TCP + queue + durable ACK latency. This is NOT a GPU FPS test."""

import argparse
import concurrent.futures
import json
import math
import time
import uuid
from collections import Counter
from pathlib import Path

from client import Client


def percentile(samples, percent):
    if not samples:
        return None
    ordered = sorted(samples)
    position = (len(ordered) - 1) * percent / 100.0
    lower = math.floor(position)
    upper = math.ceil(position)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def run_client(args, client_id, run_id):
    counts, latencies = Counter(), []
    source = "{}-{}".format(run_id, client_id)
    try:
        with Client(args.host, args.port, args.timeout) as client:
            for sequence in range(1, args.requests + 1):
                line = "DETECT {} {} {} 0.9 10 20 200 300 1920 1080".format(
                    source, sequence, sequence * 33
                )
                started = time.perf_counter()
                result = client.request(line)
                latencies.append((time.perf_counter() - started) * 1000.0)
                counts[result.get("status", "unknown")] += 1
    except (OSError, ValueError) as exc:
        counts["client_error"] += 1
        return counts, latencies, str(exc)
    return counts, latencies, None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--clients", type=int, default=32)
    parser.add_argument("--requests", type=int, default=200)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not 1 <= args.clients <= 4096 or not 1 <= args.requests <= 1000000:
        parser.error("clients must be 1..4096; requests must be 1..1000000")
    if args.clients * args.requests > 2000000:
        parser.error("at most 2 million samples per run (bounded client memory)")
    run_id = uuid.uuid4().hex[:16]
    counts, latencies, errors = Counter(), [], []
    started = time.perf_counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.clients) as executor:
        futures = [executor.submit(run_client, args, i, run_id) for i in range(args.clients)]
        for future in concurrent.futures.as_completed(futures):
            local_counts, local_latencies, error = future.result()
            counts.update(local_counts)
            latencies.extend(local_latencies)
            if error:
                errors.append(error)
    elapsed = time.perf_counter() - started
    with Client(args.host, args.port, args.timeout) as client:
        stats = client.request("STATS")
    result = {
        "scope": "TCP detection metadata + durable ACK; excludes decode/GPU inference",
        "clients": args.clients, "requests_per_client": args.requests,
        "planned_requests": args.clients * args.requests,
        "responses": counts, "elapsed_sec": elapsed,
        "acknowledgments_per_sec": len(latencies) / elapsed,
        "accepted_per_sec": counts["accepted"] / elapsed,
        "latency_ms": {
            "p50": percentile(latencies, 50), "p95": percentile(latencies, 95),
            "p99": percentile(latencies, 99), "max": max(latencies, default=None),
        },
        "stats": stats, "errors": errors,
    }
    encoded = json.dumps(result, indent=2)
    print(encoded)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded + "\n", encoding="utf-8")
    # Overload must be visible to callers, not counted as successful throughput.
    if counts["accepted"] != args.clients * args.requests:
        raise SystemExit(2)


if __name__ == "__main__":
    main()
