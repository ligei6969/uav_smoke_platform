"""FastAPI wrapper around the existing real-video inference function."""

import shutil
import uuid
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any, Dict, Optional

from fastapi import FastAPI, File, HTTPException, UploadFile, status
from fastapi.responses import FileResponse

from ai_service.analyze_video import analyze_video
from ai_service.api_config import ApiSettings
from ai_service.task_manager import (
    COMPLETED,
    FAILED,
    AnalysisTask,
    AnalysisTaskManager,
)


UPLOAD_CHUNK_SIZE = 1024 * 1024
ALLOWED_CONTENT_TYPES = {"video/mp4", "application/octet-stream"}


def _safe_original_filename(filename: Optional[str]) -> str:
    if filename is None or "\x00" in filename:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="A valid filename is required",
        )
    normalized = filename.replace("\\", "/")
    safe_name = Path(normalized).name.strip()
    if not safe_name:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="A valid filename is required",
        )
    if Path(safe_name).suffix.lower() != ".mp4":
        raise HTTPException(
            status_code=status.HTTP_415_UNSUPPORTED_MEDIA_TYPE,
            detail="Only .mp4 files are accepted",
        )
    if len(safe_name) > 255:
        safe_name = "{}.mp4".format(Path(safe_name).stem[:251])
    return safe_name


async def _save_upload(
    upload: UploadFile,
    destination: Path,
    max_size_bytes: int,
) -> int:
    size = 0
    destination.parent.mkdir(parents=True, exist_ok=False)
    try:
        with destination.open("xb") as stream:
            while True:
                chunk = await upload.read(UPLOAD_CHUNK_SIZE)
                if not chunk:
                    break
                size += len(chunk)
                if size > max_size_bytes:
                    raise HTTPException(
                        status_code=status.HTTP_413_REQUEST_ENTITY_TOO_LARGE,
                        detail="Uploaded file exceeds the configured size limit",
                    )
                stream.write(chunk)
    finally:
        await upload.close()

    if size == 0:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="Uploaded file must not be empty",
        )
    return size


def _task_status_payload(task: AnalysisTask) -> Dict[str, Any]:
    return {
        "taskId": task.task_id,
        "status": task.status,
        "originalFilename": task.original_filename,
        "errorMessage": task.error_message,
    }


def _relative_artifact_path(output_dir: Path, path: Path) -> str:
    try:
        return path.resolve().relative_to(output_dir.resolve()).as_posix()
    except ValueError:
        raise RuntimeError("Analysis artifact is outside the task output directory")


def _summary_payload(summary: Dict[str, Any]) -> Dict[str, Any]:
    return {
        "smokeDetected": bool(summary.get("smoke_detected", False)),
        "fps": float(summary.get("fps", 0.0)),
        "frameCount": int(summary.get("frame_count", 0)),
        "durationSec": float(summary.get("duration_sec", 0.0)),
        "positiveFrameCount": int(
            summary.get("positive_frame_count", 0)
        ),
        "positiveFrameRatio": float(
            summary.get("positive_frame_ratio", 0.0)
        ),
        "firstDetectionSec": summary.get("first_detection_sec"),
        "lastDetectionSec": summary.get("last_detection_sec"),
        "maxConfidence": summary.get("max_confidence"),
        "meanConfidence": summary.get("mean_confidence"),
    }


def _result_payload(
    settings: ApiSettings,
    task: AnalysisTask,
) -> Dict[str, Any]:
    if task.summary is None:
        raise RuntimeError("Completed task has no summary")
    output_dir = task.output_dir
    keyframes = [
        {
            "type": item["type"],
            "path": _relative_artifact_path(
                output_dir,
                output_dir / item["file"],
            ),
        }
        for item in task.summary.get("keyframes", [])
    ]
    return {
        "taskId": task.task_id,
        "status": task.status,
        "videoSummary": _summary_payload(task.summary),
        "artifacts": {
            "annotatedVideo": _relative_artifact_path(
                output_dir, output_dir / "annotated_video.mp4"
            ),
            "detectionResults": _relative_artifact_path(
                output_dir, output_dir / "detection_results.json"
            ),
            "keyframes": keyframes,
        },
    }


def _artifact_output_dir(
    settings: ApiSettings,
    manager: AnalysisTaskManager,
    task_id: str,
) -> Path:
    task = manager.get(task_id)
    if task is not None:
        if task.status != COMPLETED:
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT,
                detail="Analysis artifacts are not ready; status={}".format(
                    task.status
                ),
            )
        return task.output_dir.resolve()

    results_root = settings.results_root.resolve()
    output_dir = (results_root / task_id / "output").resolve()
    if not output_dir.is_relative_to(results_root) or not output_dir.is_dir():
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Analysis task was not found",
        )
    return output_dir


def _artifact_path(output_dir: Path, artifact_path: str) -> Path:
    output_dir = output_dir.resolve()
    candidate = (output_dir / artifact_path).resolve()
    if not candidate.is_relative_to(output_dir) or not candidate.is_file():
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="Analysis artifact was not found",
        )
    return candidate


def create_app(
    settings: Optional[ApiSettings] = None,
    analyze_function=analyze_video,
) -> FastAPI:
    api_settings = settings or ApiSettings.from_environment()
    manager = AnalysisTaskManager(api_settings, analyze_function)

    @asynccontextmanager
    async def lifespan(_application: FastAPI):
        yield
        manager.shutdown(wait=True)

    application = FastAPI(
        title="UAV Smoke Video Analysis API",
        version="0.4.0",
        lifespan=lifespan,
    )
    application.state.settings = api_settings
    application.state.task_manager = manager

    @application.post(
        "/api/v1/analysis-tasks",
        status_code=status.HTTP_202_ACCEPTED,
    )
    async def create_analysis_task(
        video: UploadFile = File(...),
    ) -> Dict[str, str]:
        original_filename = _safe_original_filename(video.filename)
        if (
            video.content_type
            and video.content_type.lower() not in ALLOWED_CONTENT_TYPES
        ):
            await video.close()
            raise HTTPException(
                status_code=status.HTTP_415_UNSUPPORTED_MEDIA_TYPE,
                detail="The uploaded content type must be video/mp4",
            )

        task_id = str(uuid.uuid4())
        task_root = api_settings.results_root / task_id
        input_path = task_root / "input" / "original.mp4"
        output_dir = task_root / "output"
        try:
            await _save_upload(
                video,
                input_path,
                api_settings.max_upload_size_bytes,
            )
            output_dir.mkdir(parents=True, exist_ok=False)
            manager.submit(
                task_id=task_id,
                original_filename=original_filename,
                input_path=input_path,
                output_dir=output_dir,
            )
        except HTTPException:
            shutil.rmtree(task_root, ignore_errors=True)
            raise
        except Exception as exc:
            shutil.rmtree(task_root, ignore_errors=True)
            raise HTTPException(
                status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
                detail="Unable to create analysis task: {}".format(exc),
            ) from exc

        return {"taskId": task_id, "status": "PENDING"}

    @application.get("/api/v1/analysis-tasks/{task_id}")
    def get_analysis_task(task_id: str) -> Dict[str, Any]:
        task = manager.get(task_id)
        if task is None:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail="Analysis task was not found",
            )
        return _task_status_payload(task)

    @application.get("/api/v1/analysis-tasks/{task_id}/result")
    def get_analysis_result(task_id: str) -> Dict[str, Any]:
        task = manager.get(task_id)
        if task is None:
            raise HTTPException(
                status_code=status.HTTP_404_NOT_FOUND,
                detail="Analysis task was not found",
            )
        if task.status != COMPLETED:
            detail = "Analysis result is not ready; status={}".format(
                task.status
            )
            if task.status == FAILED and task.error_message:
                detail = "{}; error={}".format(detail, task.error_message)
            raise HTTPException(
                status_code=status.HTTP_409_CONFLICT,
                detail=detail,
            )
        return _result_payload(api_settings, task)

    @application.get(
        "/api/v1/analysis-tasks/{task_id}/artifacts/{artifact_path:path}"
    )
    def get_analysis_artifact(
        task_id: str,
        artifact_path: str,
    ) -> FileResponse:
        output_dir = _artifact_output_dir(
            api_settings,
            manager,
            task_id,
        )
        return FileResponse(_artifact_path(output_dir, artifact_path))

    return application


app = create_app()
