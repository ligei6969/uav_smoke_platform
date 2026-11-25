# Smoke Event Gateway

Independent, hardware-free C++ TCP gateway that receives smoke detection
metadata events, durably journals accepted events, and answers idempotently.
It is the network boundary between an offline/online detector (for example an
`analyze_video.py` JSON output replayed by `tools/replay.py`) and the later
mission planning chain in `uav_smoke_platform`. It never requires DJI
hardware and never fabricates detections.

## Protocol

Newline-delimited ASCII, one request per line, one JSON object per response:

```text
DETECT <source> <frame_id> <timestamp_ms> <confidence> <x> <y> <w> <h> <frame_w> <frame_h>
PING
STATS
```

Response `status` values: `ok` (PING/STATS), `accepted`, `duplicate`
(same `source:frame_id` already journaled), `stale` (timestamp older than the
source's last accepted event), `busy` (bounded queue full — caller retries),
`invalid` (parse failure, NaN/Inf, out-of-range confidence, oversized line).

Guarantees:

- **Durable ACK.** An event is appended and flushed to the journal *before*
  its `accepted` response is sent. A kill -9 between the two replays as
  `duplicate`, never as a lost event.
- **Idempotent retries.** `source:frame_id` is the dedupe key; concurrent
  submissions of the same event yield exactly one `accepted`.
- **Single owner.** A lock file beside the journal records the owner PID; a
  second process on the same journal exits non-zero. A lock left by a dead
  owner (crash, kill -9) is reclaimed automatically.
- **Bounded memory.** Fixed-size worker queue; overflow is refused with
  `busy` instead of growing the process. `--max-clients` caps connections.

## Build and test

```powershell
cmake -S smoke_gateway -B smoke_gateway\build-mingw -G "MinGW Makefiles"
cmake --build smoke_gateway\build-mingw
ctest --test-dir smoke_gateway\build-mingw --output-on-failure
```

Three CTest suites: `core` (protocol, journal, dedupe, backpressure unit
tests), `network` (black-box TCP tests: fragmentation, malformed input, idle
partial requests, concurrent retries, crash durability, journal ownership,
POSIX SIGTERM drain), `client` (Python bridge and percentile tests).

Note for Windows/MSYS2: run tests with the MSYS2 UCRT64 bin directory first
in `PATH` so the freshly built executables resolve the matching MinGW runtime
DLLs, for example
`$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"` in PowerShell.

## Run

```powershell
smoke_gateway.exe --bind 127.0.0.1 --port 8765 --journal events.journal `
  --workers 4 --queue-capacity 4096 --max-clients 64 --idle-ms 30000
```

`--port 0` binds an ephemeral port and, with `--ready-file <path>`, writes
`{"port": <n>}` after `listen()` succeeds — that is how the network tests
launch the server.

## Tools

- `tools/client.py` — bounded, sequential-per-source line-protocol client,
  plus `detection_lines()` adapting Step 1 `analyze_video.py` JSON (strongest
  smoke box per frame, frame order preserved, no fabricated results).
- `tools/replay.py` — replay a detection JSON into a running gateway.
- `tools/load_test.py` — multi-client latency/throughput measurement of the
  TCP + queue + durable-ACK path (not a GPU FPS test); exits non-zero when
  any request was not `accepted`, so overload is never reported as success.

Example load result (16 clients x 200 requests, local MinGW build):
about 1000 durable ACKs/second, p50 ~15 ms, p99 ~30 ms, zero errors.

## Offline verification status

All verification in this checkout was done without DJI hardware: CTest
(5 consecutive clean runs), the load test above, and a kill -9 durability
check (3201 journal entries replayed, resended event answered `duplicate`).
Real-aircraft behavior remains untested by design.
