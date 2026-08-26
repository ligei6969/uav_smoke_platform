# UAV Smoke Detection and Recheck Mission Planning Platform

## 1. Project Background

This project is a hardware-free offline validation platform derived from prior real-world DJI M350 + Jetson Nano forest fire smoke detection development experience.

The earlier real hardware work included DJI M350 payload development, Jetson Nano edge deployment, video stream acquisition and processing, FFmpeg decoding, OpenCV image preprocessing, YOLO smoke detection inference, gimbal tracking, and target geolocation using UAV GPS, altitude, gimbal attitude, and camera parameters.

The current `uav_smoke_platform` version does not claim to reconnect the real UAV or Jetson hardware. Its goal is to preserve and refactor that workflow into a runnable, testable, and demonstrable offline platform for smoke target geolocation and recheck mission planning when UAV hardware is not available.

## 2. Current Version

Current version: `V0.5`

The project has progressed from an offline mission-planning proof of concept to
a three-part platform:

- `V0.1`: complete offline C++ smoke geolocation and recheck mission-planning
  workflow, with JSON output and Python visualization.
- `V0.2`: real MP4 frame-by-frame smoke analysis using Ultralytics YOLO, with
  annotated video, detection records, summary statistics, and keyframes.
- `V0.3`: Spring Boot video-management web MVP backed by MySQL, including
  upload, list, detail, edit, delete, and HTTP Range video playback.
- `V0.4`: FastAPI asynchronous smoke-analysis task service, including upload,
  task status, result retrieval, validation, and isolated task artifacts.
- `V0.5`: Spring Boot and FastAPI integration, including persisted analysis
  attempts, browser-side status polling, result review, retry, and proxied
  annotated video and keyframes.

The automated suites currently contain 15 Python tests and 21 Spring Boot
tests. The earlier real-model `11.mp4` verification is recorded in
[`python/V0.4_VERIFICATION.md`](python/V0.4_VERIFICATION.md), and the complete
web-to-inference V0.5 verification is recorded in
[`web/springboot/uav-video-platform/docs/v0.5-verification.md`](web/springboot/uav-video-platform/docs/v0.5-verification.md).

## 3. Current Business Workflow

```text
Operator -> Spring Boot video web -> MySQL + local video storage
         -> start/status/retry API -> persisted analysis attempt
         -> FastAPI analysis task -> YOLO video inference
         -> Spring Boot media proxy -> annotated video + summary + keyframes

Mock telemetry + detection -> C++ target geolocation
                           -> recheck mission planning
                           -> mission_plan.json -> 2D visualization
```

The browser communicates only with Spring Boot. FastAPI addresses and local
artifact paths are not exposed to the operator-facing page.

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

- Real video smoke analysis:
  - Reads MP4 videos frame by frame with OpenCV.
  - Runs Ultralytics YOLO inference on each decodable frame.
  - Writes an annotated MP4, positive-frame detections, summary statistics,
    and first/strongest/last detection keyframes.

- Spring Boot video-management web application:
  - Stores video metadata in MySQL 8.4.
  - Supports upload, list, detail, edit, logical delete, and local file cleanup.
  - Serves MP4 content with HTTP Range support for browser playback.

- FastAPI analysis task service:
  - `POST /api/v1/analysis-tasks` creates an asynchronous analysis task.
  - `GET /api/v1/analysis-tasks/{taskId}` returns task status.
  - `GET /api/v1/analysis-tasks/{taskId}/result` returns the completed summary
    and artifact paths.
  - Validates file type, upload size, task state, and output isolation.

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
    ai_service/
      analyze_video.py
      api.py
      api_config.py
      task_manager.py
    sim/
      visualize_mission.py
    tests/
  web/
    springboot/
      uav-video-platform/
  results/
    api_tasks/
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

V0.5 is still a hardware-free validation platform. It intentionally does not
claim to reconnect the full production UAV stack.

Current limitations:

- Analysis task state is in process memory and is not recovered after a FastAPI
  restart. Spring Boot persists submitted task IDs and completed result metadata,
  but an in-progress task lost by FastAPI is marked failed and must be retried.
- Video bytes are transferred from Spring Boot to FastAPI for each analysis
  attempt, so both services temporarily keep a copy of the source video.
- This refactored version does not connect to real DJI PSDK hardware.
- It does not reacquire a real UAV video stream.
- The C++ geolocation workflow still receives a mock `DetectionResult`; real
  YOLO detections are not yet converted into geolocated smoke events.
- There is no real flight-controller waypoint mission upload.
- There is no dynamic obstacle avoidance.
- There is no A* or RRT path planning.
- There is no PX4 or Gazebo simulation.
- `MissionPlanner` is only a rule-based recheck waypoint generator.

## 12. V0.5 Web and Analysis Integration

V0.5 delivers the complete operator workflow:

```text
Upload video -> start analysis -> track progress -> review detections
             -> play annotated video -> inspect summary and keyframes
```

Implemented work:

1. Added a separate analysis-attempt table containing
   task ID, status, error message, timestamps, and result metadata.
2. Added a Spring Boot FastAPI client with configurable service URL, timeouts,
   clear error mapping, and no hard-coded environment-specific paths.
3. Added backend endpoints and service methods to start analysis, query status,
   and obtain results without requiring the browser to call FastAPI directly.
4. Extended the video detail page with a start/retry action, progress state,
   failure feedback, smoke summary, keyframes, and annotated-video playback.
5. Added polling with terminal states (`COMPLETED` and `FAILED`) and duplicate
   active-task prevention.
6. Added Spring Boot service/client tests and FastAPI artifact and HTTP Range
   contract tests.
7. Updated deployment and operating documentation for running MySQL, Spring
   Boot, FastAPI, the YOLO model, and result storage together.

V0.5 acceptance criteria:

- An operator can complete the entire analysis workflow from the browser.
- Analysis status and result references survive a Spring Boot restart.
- Failed analysis can be diagnosed and retried without re-uploading the video.
- Existing C++, Python, and Spring Boot automated tests remain passing.
- Offline development remains possible without DJI hardware.

## 13. Next Milestone: V0.6 Geolocation Integration

The next direction is to connect real detection frames with synchronized
telemetry for target geolocation and recheck mission generation. Real DJI PSDK
and flight-controller integration should remain behind the existing adapters
until hardware is available for safe validation.
