import json
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

from fastapi.testclient import TestClient


PYTHON_DIR = Path(__file__).resolve().parents[1]
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))
TEST_TEMP_ROOT = PYTHON_DIR.parent / "results" / "test_api_tmp"

from ai_service.api import create_app
from ai_service.api_config import ApiSettings


def successful_summary():
    return {
        "video": "original.mp4",
        "smoke_detected": True,
        "fps": 30.0,
        "frame_count": 300,
        "width": 1280,
        "height": 720,
        "duration_sec": 10.0,
        "positive_frame_count": 30,
        "positive_frame_ratio": 0.1,
        "first_detection_sec": 1.0,
        "last_detection_sec": 9.0,
        "max_confidence": 0.9,
        "mean_confidence": 0.8,
        "keyframes": [
            {
                "type": "first_detection",
                "frame_id": 30,
                "timestamp_sec": 1.0,
                "file": "keyframes/first_detection.jpg",
            }
        ],
    }


def write_success_artifacts(output_dir, summary=None):
    output_dir.mkdir(parents=True, exist_ok=True)
    keyframe_dir = output_dir / "keyframes"
    keyframe_dir.mkdir(parents=True, exist_ok=True)
    result = summary or successful_summary()
    (output_dir / "annotated_video.mp4").write_bytes(b"mock-video")
    (output_dir / "detection_results.json").write_text(
        '{"detections": []}\n',
        encoding="utf-8",
    )
    (output_dir / "video_summary.json").write_text(
        json.dumps(result),
        encoding="utf-8",
    )
    (keyframe_dir / "first_detection.jpg").write_bytes(b"mock-image")
    return result


class AnalysisApiTests(unittest.TestCase):
    def setUp(self):
        TEST_TEMP_ROOT.mkdir(parents=True, exist_ok=True)
        self.temporary_directory = tempfile.TemporaryDirectory(
            dir=str(TEST_TEMP_ROOT)
        )
        self.product_root = Path(self.temporary_directory.name)
        self.model_path = self.product_root / "models" / "smoke.pt"
        self.model_path.parent.mkdir(parents=True)
        self.model_path.write_bytes(b"mock-model")
        self.settings = ApiSettings(
            product_root=self.product_root,
            model_path=self.model_path,
            results_root=self.product_root / "results" / "api_tasks",
            device="0",
            confidence=0.25,
            max_upload_size_bytes=1024 * 1024,
        )

    def tearDown(self):
        self.temporary_directory.cleanup()

    def test_create_task_returns_202_and_task_id(self):
        release = threading.Event()

        def analyzer(**kwargs):
            release.wait(timeout=2)
            return write_success_artifacts(kwargs["output_dir"])

        application = create_app(self.settings, analyzer)
        with TestClient(application) as client:
            try:
                response = self._create_task(client)
                self.assertEqual(response.status_code, 202)
                payload = response.json()
                self.assertTrue(payload["taskId"])
                self.assertEqual(payload["status"], "PENDING")
            finally:
                release.set()

    def test_non_mp4_upload_is_rejected(self):
        application = create_app(self.settings, write_success_artifacts)
        with TestClient(application) as client:
            response = client.post(
                "/api/v1/analysis-tasks",
                files={"video": ("notes.txt", b"text", "text/plain")},
            )
        self.assertEqual(response.status_code, 415)
        self.assertIn("Only .mp4", response.json()["detail"])

    def test_empty_upload_is_rejected(self):
        application = create_app(self.settings, write_success_artifacts)
        with TestClient(application) as client:
            response = client.post(
                "/api/v1/analysis-tasks",
                files={"video": ("empty.mp4", b"", "video/mp4")},
            )
        self.assertEqual(response.status_code, 400)
        self.assertIn("must not be empty", response.json()["detail"])

    def test_missing_task_returns_404(self):
        application = create_app(self.settings, write_success_artifacts)
        with TestClient(application) as client:
            response = client.get(
                "/api/v1/analysis-tasks/not-a-real-task"
            )
        self.assertEqual(response.status_code, 404)

    def test_pending_and_running_tasks_cannot_read_result(self):
        running = threading.Event()
        release = threading.Event()

        def analyzer(**kwargs):
            running.set()
            release.wait(timeout=3)
            return write_success_artifacts(kwargs["output_dir"])

        application = create_app(self.settings, analyzer)
        with TestClient(application) as client:
            try:
                first_id = self._create_task(client).json()["taskId"]
                self.assertTrue(running.wait(timeout=1))
                second_id = self._create_task(client).json()["taskId"]

                first_status = client.get(
                    "/api/v1/analysis-tasks/{}".format(first_id)
                ).json()["status"]
                second_status = client.get(
                    "/api/v1/analysis-tasks/{}".format(second_id)
                ).json()["status"]
                self.assertEqual(first_status, "RUNNING")
                self.assertEqual(second_status, "PENDING")
                self.assertEqual(
                    client.get(
                        "/api/v1/analysis-tasks/{}/result".format(first_id)
                    ).status_code,
                    409,
                )
                self.assertEqual(
                    client.get(
                        "/api/v1/analysis-tasks/{}/result".format(second_id)
                    ).status_code,
                    409,
                )
            finally:
                release.set()

    def test_completed_task_returns_summary_and_artifact_paths(self):
        def analyzer(**kwargs):
            return write_success_artifacts(kwargs["output_dir"])

        application = create_app(self.settings, analyzer)
        with TestClient(application) as client:
            task_id = self._create_task(client).json()["taskId"]
            terminal = self._wait_for_terminal(client, task_id)
            self.assertEqual(terminal["status"], "COMPLETED")

            response = client.get(
                "/api/v1/analysis-tasks/{}/result".format(task_id)
            )
            self.assertEqual(response.status_code, 200)
            payload = response.json()
            self.assertEqual(payload["taskId"], task_id)
            self.assertTrue(payload["videoSummary"]["smokeDetected"])
            self.assertEqual(payload["videoSummary"]["frameCount"], 300)
            self.assertAlmostEqual(
                payload["videoSummary"]["positiveFrameRatio"], 0.1
            )
            self.assertTrue(
                payload["artifacts"]["detectionResults"].endswith(
                    "detection_results.json"
                )
            )
            self.assertNotIn("detections", payload)

    def test_failed_task_exposes_real_error_message(self):
        def analyzer(**_kwargs):
            raise RuntimeError("mock GPU inference failed")

        application = create_app(self.settings, analyzer)
        with TestClient(application) as client:
            task_id = self._create_task(client).json()["taskId"]
            terminal = self._wait_for_terminal(client, task_id)
            self.assertEqual(terminal["status"], "FAILED")
            self.assertEqual(
                terminal["errorMessage"], "mock GPU inference failed"
            )
            result_response = client.get(
                "/api/v1/analysis-tasks/{}/result".format(task_id)
            )
            self.assertEqual(result_response.status_code, 409)
            self.assertIn(
                "mock GPU inference failed",
                result_response.json()["detail"],
            )

    def test_multiple_tasks_never_run_gpu_analysis_concurrently(self):
        counter_lock = threading.Lock()
        active = 0
        maximum_active = 0

        def analyzer(**kwargs):
            nonlocal active, maximum_active
            with counter_lock:
                active += 1
                maximum_active = max(maximum_active, active)
            try:
                time.sleep(0.1)
                return write_success_artifacts(kwargs["output_dir"])
            finally:
                with counter_lock:
                    active -= 1

        application = create_app(self.settings, analyzer)
        with TestClient(application) as client:
            first_id = self._create_task(client).json()["taskId"]
            second_id = self._create_task(client).json()["taskId"]
            self.assertEqual(
                self._wait_for_terminal(client, first_id)["status"],
                "COMPLETED",
            )
            self.assertEqual(
                self._wait_for_terminal(client, second_id)["status"],
                "COMPLETED",
            )

        self.assertEqual(maximum_active, 1)

    def _create_task(self, client):
        return client.post(
            "/api/v1/analysis-tasks",
            files={"video": ("flight.mp4", b"mock-mp4", "video/mp4")},
        )

    def _wait_for_terminal(self, client, task_id, timeout=3.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            payload = client.get(
                "/api/v1/analysis-tasks/{}".format(task_id)
            ).json()
            if payload["status"] in {"COMPLETED", "FAILED"}:
                return payload
            time.sleep(0.02)
        self.fail("Task did not reach a terminal state")


if __name__ == "__main__":
    unittest.main()
