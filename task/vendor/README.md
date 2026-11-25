# Reference provenance

`tools/snapshot_vendor.ps1` copies a curated list from the explicitly supplied SDK root,
preserving their original notices, and writes `source_manifest.json` with SHA256.
`reference/` is a local inspection snapshot, not part of the CMake source list.
It contains known old defects and is never compiled into `smoke_replay`.

New, reviewed implementations live in `src/`. `dji_telemetry.cpp` uses the
original PSDK 3.10.0 field definitions directly through `PSDK_ROOT`.
This iteration does not claim that the original 58-object sample application,
USB HAL, decoder, or real-model inference has been migrated or validated.

DJI headers/static libraries and model weights remain external controlled
dependencies. The snapshot tool does not copy app-info headers, keys, model
weights, static libraries, videos, or device serial numbers. Do not publish
reference files without checking their original EULA/license.
