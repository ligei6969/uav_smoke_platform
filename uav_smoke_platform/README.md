# UAV Smoke Platform

This directory is an independent product skeleton built beside the DJI Payload SDK sample tree. It does not copy or modify the existing SDK libraries, samples, tests, or root CMake project.

## Architecture

The platform uses a `C++ PSDK Adapter + Python AI Service` architecture.

- `cpp/dji_adapter`: C++ device adapter layer for future DJI PSDK integration. The first version is only a buildable skeleton and does not initialize real PSDK hardware.
- `python/app`: Python AI service for offline video reading, model inference, target geolocation, and automated testing.
- `configs`: Runtime configuration files.
- `data/videos`: Offline video inputs for local development and tests.
- `data/telemetry`: Offline telemetry inputs such as GPS, altitude, gimbal angles, and timestamps.
- `models`: Local model artifacts.
- `results`: Generated detection, geolocation, and test outputs.
- `docs`: Design notes, deployment notes, and operator documentation.

## Modes

### Offline Mode

Offline mode is the current development mode. It runs without drone hardware by reading local videos and telemetry files, then executing inference and target geolocation in Python.

Typical flow:

```text
local video + telemetry file
        -> Python video source
        -> inference module
        -> geolocation module
        -> result output
```

### Future Hardware Mode

Future hardware mode will connect the C++ DJI PSDK adapter to real payload devices and expose camera frames, telemetry, and device status to the Python AI service. Real PSDK initialization, stream acquisition, and device control are intentionally not implemented in this skeleton.

Typical flow:

```text
DJI payload hardware
        -> C++ PSDK adapter
        -> Python AI service
        -> detection and geolocation results
```
