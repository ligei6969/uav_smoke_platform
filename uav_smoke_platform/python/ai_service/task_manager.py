"""Thread-safe in-memory task management for GPU video analysis."""

import logging
import threading
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Any, Callable, Dict, Optional

from ai_service.api_config import ApiSettings


LOGGER = logging.getLogger(__name__)

PENDING = "PENDING"
RUNNING = "RUNNING"
COMPLETED = "COMPLETED"
FAILED = "FAILED"
TERMINAL_STATUSES = {COMPLETED, FAILED}

AnalyzeFunction = Callable[..., Dict[str, Any]]


@dataclass
class AnalysisTask:
    task_id: str
    status: str
    original_filename: str
    input_path: Path
    output_dir: Path
    error_message: Optional[str] = None
    summary: Optional[Dict[str, Any]] = None


class AnalysisTaskManager:
    """Runs analysis jobs sequentially and protects mutable task state."""

    def __init__(
        self,
        settings: ApiSettings,
        analyze_function: AnalyzeFunction,
    ) -> None:
        self.settings = settings
        self._analyze_function = analyze_function
        self._tasks: Dict[str, AnalysisTask] = {}
        self._tasks_lock = threading.RLock()
        self._gpu_lock = threading.Lock()
        self._executor = ThreadPoolExecutor(
            max_workers=1,
            thread_name_prefix="smoke-gpu-analysis",
        )

    def submit(
        self,
        task_id: str,
        original_filename: str,
        input_path: Path,
        output_dir: Path,
    ) -> AnalysisTask:
        task = AnalysisTask(
            task_id=task_id,
            status=PENDING,
            original_filename=original_filename,
            input_path=input_path,
            output_dir=output_dir,
        )
        with self._tasks_lock:
            if task_id in self._tasks:
                raise ValueError("Task already exists: {}".format(task_id))
            self._tasks[task_id] = task
            self._executor.submit(self._execute, task_id)
        return replace(task)

    def get(self, task_id: str) -> Optional[AnalysisTask]:
        with self._tasks_lock:
            task = self._tasks.get(task_id)
            return replace(task) if task is not None else None

    def shutdown(self, wait: bool = True) -> None:
        self._executor.shutdown(wait=wait, cancel_futures=False)

    def _execute(self, task_id: str) -> None:
        try:
            with self._gpu_lock:
                self._set_status(task_id, RUNNING)
                task = self._require_task(task_id)
                summary = self._analyze_function(
                    video_path=task.input_path,
                    model_path=self.settings.model_path,
                    output_dir=task.output_dir,
                    device=self.settings.device,
                    confidence=self.settings.confidence,
                )
                self._validate_artifacts(task.output_dir, summary)
                with self._tasks_lock:
                    stored = self._tasks[task_id]
                    stored.summary = summary
                    stored.error_message = None
                    stored.status = COMPLETED
        except Exception as exc:  # noqa: BLE001 - task must retain real errors
            LOGGER.exception("Analysis task %s failed", task_id)
            message = str(exc).strip() or exc.__class__.__name__
            with self._tasks_lock:
                stored = self._tasks.get(task_id)
                if stored is not None:
                    stored.status = FAILED
                    stored.error_message = message

    def _set_status(self, task_id: str, status: str) -> None:
        with self._tasks_lock:
            self._tasks[task_id].status = status

    def _require_task(self, task_id: str) -> AnalysisTask:
        with self._tasks_lock:
            return replace(self._tasks[task_id])

    def _validate_artifacts(
        self,
        output_dir: Path,
        summary: Dict[str, Any],
    ) -> None:
        required = (
            output_dir / "annotated_video.mp4",
            output_dir / "detection_results.json",
            output_dir / "video_summary.json",
        )
        for artifact in required:
            if not artifact.is_file() or artifact.stat().st_size <= 0:
                raise RuntimeError(
                    "Expected analysis artifact is missing or empty: {}".format(
                        artifact.name
                    )
                )

        for keyframe in summary.get("keyframes", []):
            relative_file = keyframe.get("file")
            if not relative_file:
                raise RuntimeError("Keyframe metadata is missing a file path")
            keyframe_path = (output_dir / relative_file).resolve()
            if (
                not keyframe_path.is_relative_to(output_dir.resolve())
                or not keyframe_path.is_file()
                or keyframe_path.stat().st_size <= 0
            ):
                raise RuntimeError(
                    "Expected keyframe is missing or empty: {}".format(
                        relative_file
                    )
                )
