# UAV Smoke Detection and Recheck Mission Planning Platform

## 1. Project Background

This project is a hardware-free offline validation platform derived from prior real-world DJI M350 + Jetson Nano forest fire smoke detection development experience.

The earlier real hardware work included DJI M350 payload development, Jetson Nano edge deployment, video stream acquisition and processing, FFmpeg decoding, OpenCV image preprocessing, YOLO smoke detection inference, gimbal tracking, and target geolocation using UAV GPS, altitude, gimbal attitude, and camera parameters.

The current `uav_smoke_platform` version does not claim to reconnect the real UAV or Jetson hardware. Its goal is to preserve and refactor that workflow into a runnable, testable, and demonstrable offline platform for smoke target geolocation and recheck mission planning when UAV hardware is not available.

## 2. Current Version

Current version: `V0.1`

V0.1 provides a complete offline C++ business chain and a Python visualization script:

- Mock UAV telemetry and mock smoke detection input.
- Ground target geolocation from image point and UAV pose.
- Smoke target event construction.
- Rule-based recheck waypoint generation.
- JSON mission output.
- 2D mission visualization.

## 3. Business Workflow

```text
MockTelemetrySource
+ Mock DetectionResult
        |
        v
SmokeEventBuilder
        |
        v
CalculateGroundTarget
        |
        v
SmokeTargetEvent
        |
        v
MissionPlanner
        |
        v
MissionPlan
        |
        v
MissionPlanWriter
        |
        v
mission_plan.json
        |
        v
visualize_mission.py
        |
        v
results/mission_plan.png
```

## 4. Implemented Features

- Common data structures:
  - `VideoPacket`
  - `TelemetryPacket`
  - `DetectionResult`

- Data source abstractions:
  - `VideoSource`
  - `TelemetrySource`

- Mock data sources:
  - `MockVideoSource`
  - `MockTelemetrySource`

- Smoke target geolocation:
  - `CalculateGroundTarget`
  - Computes a ground target position from UAV GPS, altitude, AGL height, gimbal pitch/roll/yaw, camera horizontal field of view, and detection center pixel.

- Smoke event construction:
  - `SmokeEventBuilder`
  - Converts `DetectionResult + TelemetryPacket` into `SmokeTargetEvent`.

- Recheck waypoint planning:
  - `MissionPlanner`
  - Generates four observation waypoints about 50 meters north, east, south, and west of the smoke target.
  - Current flight altitude is fixed at 120 meters.

- JSON output:
  - `MissionPlanWriter`
  - Writes `mission_plan.json` with the smoke target and mission waypoints.

- Complete offline demo:
  - `smoke_mission_demo`
  - Runs mock telemetry, mock detection, target geolocation, mission planning, and JSON output.

- Python visualization:
  - `python/sim/visualize_mission.py`
  - Reads `mission_plan.json`, converts waypoint latitude/longitude to local East/North meter offsets, and writes `results/mission_plan.png`.

- Automated C++ tests:
  - `target_geolocation_test`
  - `smoke_event_builder_test`
  - `mission_planner_test`
  - `mission_plan_writer_test`

## 5. Project Structure

```text
uav_smoke_platform/
  README.md
  AGENTS.md
  cpp/
    dji_adapter/
      CMakeLists.txt
      demos/
        smoke_mission_demo.cpp
      include/uav/
        mission_plan.hpp
        mission_plan_writer.hpp
        mission_planner.hpp
        mock_telemetry_source.hpp
        mock_video_source.hpp
        packets.hpp
        smoke_event.hpp
        smoke_event_builder.hpp
        target_geolocation.hpp
        telemetry_source.hpp
        video_source.hpp
      src/
        mission_plan_writer.cpp
        mission_planner.cpp
        mock_adapter_demo.cpp
        mock_telemetry_source.cpp
        mock_video_source.cpp
        smoke_event_builder.cpp
        target_geolocation.cpp
      tests/
        mission_plan_writer_test.cpp
        mission_planner_test.cpp
        smoke_event_builder_test.cpp
        target_geolocation_test.cpp
      mission_plan.json
  python/
    requirements.txt
    app/
    sim/
      visualize_mission.py
  results/
    mission_plan.png
```

Generated build directories such as `build-mingw` are local build outputs.

## 6. Build

Run from the repository root:

```powershell
cd uav_smoke_platform/cpp/dji_adapter
cmake -S . -B build-mingw -G "MinGW Makefiles"
cmake --build build-mingw
```

The commands above build:

- `mock_adapter_demo.exe`
- `smoke_mission_demo.exe`
- `target_geolocation_test.exe`
- `smoke_event_builder_test.exe`
- `mission_planner_test.exe`
- `mission_plan_writer_test.exe`

## 7. Run Tests

Run from `uav_smoke_platform/cpp/dji_adapter`:

```powershell
ctest --test-dir build-mingw --output-on-failure
```

Expected V0.1 result:

```text
100% tests passed, 0 tests failed out of 4
```

## 8. Run Demo

Run from `uav_smoke_platform/cpp/dji_adapter`:

```powershell
.\build-mingw\smoke_mission_demo.exe
```

The demo writes:

```text
mission_plan.json
```

to the current working directory, which should be `uav_smoke_platform/cpp/dji_adapter` when using the command above.

## 9. Visualize Mission

The Python visualization uses the `psdk` conda environment.

Install Python dependencies if needed:

```powershell
conda run -n psdk python -m pip install -r uav_smoke_platform/python/requirements.txt
```

Run from the repository root:

```powershell
conda run -n psdk python uav_smoke_platform/python/sim/visualize_mission.py uav_smoke_platform/cpp/dji_adapter/mission_plan.json
```

The script writes:

```text
uav_smoke_platform/results/mission_plan.png
```

The plot uses the smoke target as the local origin:

```text
East = 0 m
North = 0 m
```

Each waypoint is converted from latitude/longitude into local meter offsets using a WGS84 local approximation. Longitude scaling includes the cosine of the target latitude.

## 10. Output Example

Example `smoke_mission_demo` console output:

```text
Smoke mission demo started.

Telemetry:
- drone latitude: 30
- drone longitude: 120
- altitude / AGL: 100 / 100
- gimbal pitch / roll / yaw: -90 / 0 / 0

Detection:
- confidence: 0.85
- center pixel: 960, 540

Smoke target:
- target latitude: 30
- target longitude: 120
- north offset: 6.12323e-15
- east offset: 0
- horizontal distance: 6.12323e-15

Mission:
- waypoint count: 4

Output:
- mission_plan.json path: mission_plan.json
```

Example JSON shape:

```json
{
  "event_type": "smoke_detected",
  "timestamp_ms": 0,
  "confidence": 0.85,
  "target": {
    "latitude_deg": 30,
    "longitude_deg": 120,
    "altitude_m": 0,
    "north_offset_m": 6.12323399573677e-15,
    "east_offset_m": 0,
    "horizontal_distance_m": 6.12323399573677e-15
  },
  "waypoints": [
    {
      "latitude_deg": 30.0004510500524,
      "longitude_deg": 120,
      "altitude_m": 120,
      "yaw_deg": 180
    }
  ]
}
```

The real V0.1 demo output contains four waypoints.

Example visualization output:

```text
Input JSON: C:\Users\jb\Desktop\Payload-SDK-3.10.0-task\uav_smoke_platform\cpp\dji_adapter\mission_plan.json
Waypoint count: 4
Output image: C:\Users\jb\Desktop\Payload-SDK-3.10.0-task\uav_smoke_platform\results\mission_plan.png
```

## 11. Current Limitations

V0.1 is an offline validation platform. It intentionally does not implement or reconnect the full hardware stack.

Current limitations:

- This refactored version does not connect to real DJI PSDK hardware.
- It does not reacquire a real UAV video stream.
- `DetectionResult` is currently a mock smoke detection result.
- There is no Python YOLO online inference service in V0.1.
- There is no real flight-controller waypoint mission upload.
- There is no dynamic obstacle avoidance.
- There is no A* or RRT path planning.
- There is no PX4 or Gazebo simulation.
- `MissionPlanner` is only a rule-based recheck waypoint generator.

## 12. Future Work

Possible next steps:

- Replace mock detections with a real offline or online smoke detector.
- Add video-file input and frame-level detection result generation.
- Add a hardware adapter behind the existing source interfaces when DJI hardware is available.
- Add operator-facing result review tools.
- Add mission export formats compatible with later real flight integration.

Future work should keep offline tests runnable without UAV hardware.
