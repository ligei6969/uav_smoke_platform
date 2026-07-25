# FastAPI smoke-analysis service

The API reuses `ai_service.analyze_video.analyze_video`; the existing CLI
remains available and no inference loop is duplicated.

From the product directory:

```powershell
$env:SMOKE_MODEL_PATH = "models/yolov8s.pt"
$env:SMOKE_DEVICE = "0"
C:\Users\jb\miniconda3\envs\psdk\python.exe -m uvicorn `
  ai_service.api:app `
  --app-dir python `
  --host 127.0.0.1 `
  --port 8000
```

Open <http://127.0.0.1:8000/docs>.

Configuration variables:

- `SMOKE_MODEL_PATH`: model path, relative to the product directory by default.
- `SMOKE_DEVICE`: Ultralytics device (`0` by default).
- `SMOKE_CONFIDENCE`: confidence threshold (`0.25` by default).
- `SMOKE_API_RESULTS_ROOT`: task output root (`results/api_tasks` by default).
- `SMOKE_MAX_UPLOAD_SIZE_BYTES`: upload limit (512 MiB by default).

Task files are written to:

```text
results/api_tasks/{taskId}/
├── input/original.mp4
└── output/
    ├── annotated_video.mp4
    ├── detection_results.json
    ├── video_summary.json
    └── keyframes/
```
