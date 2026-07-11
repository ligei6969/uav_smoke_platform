# Repository Guidelines

## Project Structure & Module Organization

`uav_smoke_platform` is an independent product skeleton located beside the DJI Payload SDK sample tree. Keep changes in this subtree unless the task explicitly requires SDK-level integration.

- `cpp/dji_adapter`: C++ adapter layer for future DJI PSDK integration. It currently contains mock telemetry/video sources, geolocation logic, a demo executable, and standalone tests.
- `cpp/dji_adapter/include/uav`: Public C++ adapter headers.
- `cpp/dji_adapter/src`: C++ implementation files.
- `cpp/dji_adapter/tests`: C++ standalone tests registered with CTest.
- `python/app`: Python AI service package skeleton for sources, inference, geolocation, and output modules.
- `python/tests`: Python tests for the AI service.
- `configs`: Runtime configuration files.
- `data/videos` and `data/telemetry`: Local offline development inputs.
- `models`: Local model artifacts.
- `results`: Generated detection, geolocation, and test outputs.
- `docs`: Design, deployment, and operator notes.

## Build, Test, and Development Commands

Use out-of-source builds for the C++ adapter:

```powershell
cmake -S uav_smoke_platform\cpp\dji_adapter -B uav_smoke_platform\build\dji_adapter
cmake --build uav_smoke_platform\build\dji_adapter
ctest --test-dir uav_smoke_platform\build\dji_adapter --output-on-failure
```

Run the mock adapter demo after building:

```powershell
.\uav_smoke_platform\build\dji_adapter\mock_adapter_demo.exe
```

Install Python dependencies from the Python subproject:

```powershell
python -m pip install -r uav_smoke_platform\python\requirements.txt
pytest uav_smoke_platform\python\tests
```

## Coding Style & Naming Conventions

For C++, follow the style already used in `cpp/dji_adapter`: C++11, 4-space indentation, lower_snake_case namespaces, PascalCase structs, and descriptive source/header names. Keep public interfaces under `include/uav` and implementation details under `src`.

For Python, keep importable service code under `python/app` and tests under `python/tests`. Prefer small modules that match the existing domain split: `sources`, `inference`, `geolocation`, and `output`.

## Testing Guidelines

Add focused tests near the code they validate. C++ tests should live in `cpp/dji_adapter/tests`, be added to `CMakeLists.txt`, and be runnable through CTest. Python tests should live in `python/tests` and run with pytest.

Offline mode must remain runnable without DJI hardware. Guard or mock any future hardware, camera, network, or PSDK integration so local builds and tests do not require connected devices.

## Data, Models, and Generated Outputs

Do not commit large videos, telemetry dumps, trained model weights, generated detection outputs, logs, or hardware-specific artifacts unless the task explicitly calls for sanitized sample assets. Treat `data`, `models`, and `results` as local working areas by default.

## Integration Notes

This subtree should not modify or duplicate the root DJI Payload SDK libraries, headers, samples, or root CMake project unless the user explicitly asks for integration. Future real PSDK support should be added behind adapter interfaces so mock/offline workflows continue to work.
