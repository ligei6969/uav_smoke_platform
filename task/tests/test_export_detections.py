"""Standard-library regression tests for the recorded-detection CSV bridge."""

import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


EXPORTER = Path(__file__).resolve().parents[1] / "tools" / "export_detections.py"
SPEC = importlib.util.spec_from_file_location("export_detections_under_test", str(EXPORTER))
BRIDGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BRIDGE)


def smoke_box(confidence=0.8, bbox=None, label="smoke"):
    return {
        "class_id": 0,
        "class_name": label,
        "confidence": confidence,
        "bbox": [10.0, 20.0, 110.0, 120.0] if bbox is None else bbox,
    }


def recorded_payload():
    # This is the analyze_video detection_results.json schema, not a model stub.
    return {
        "video": "recorded-11.mp4",
        "model": "models/custom-smoke.pt",
        "fps": 30.0,
        "frame_count": 7311,
        "width": 1920,
        "height": 1080,
        "detections": [
            {"frame_id": 101, "timestamp_sec": 101.0 / 30.0,
             "boxes": [smoke_box()]},
            {"frame_id": 233, "timestamp_sec": 233.0 / 30.0,
             "boxes": [smoke_box(0.7, label="烟雾")]},
        ],
    }


class ExportDetectionsTests(unittest.TestCase):
    def export_rows(self, payload, epoch=0):
        return list(BRIDGE.rows(payload, "drone-1", "wide", epoch))

    def run_cli(self, directory, payload, output=None):
        input_file = Path(directory) / "detection_results.json"
        input_file.write_text(json.dumps(payload, ensure_ascii=False), encoding="utf-8")
        output = output or Path(directory) / "detections.csv"
        completed = subprocess.run(
            [sys.executable, str(EXPORTER), "--detections", str(input_file),
             "--output", str(output), "--source", "drone-1", "--lens", "wide"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            universal_newlines=True, timeout=10,
        )
        return completed, output

    def test_recorded_json_schema_exports_identity_and_box_without_telemetry(self):
        payload = recorded_payload()
        original = copy.deepcopy(payload)
        rows = self.export_rows(payload)
        self.assertEqual(len(rows), 2)
        first = rows[0].split(",")
        self.assertEqual(len(first), 12)
        self.assertEqual(first[:3], ["drone-1", "0", "101"])
        self.assertAlmostEqual(float(first[3]), 101.0 / 30.0)
        self.assertEqual(first[4:7], ["wide", "1920", "1080"])
        self.assertEqual([float(value) for value in first[7:11]], [10, 20, 110, 120])
        self.assertAlmostEqual(float(first[11]), 0.8)
        self.assertEqual(payload, original)

    def test_highest_confidence_smoke_box_is_selected_per_frame(self):
        payload = recorded_payload()
        payload["detections"][0]["boxes"] = [
            smoke_box(0.3, [10, 20, 30, 40]),
            smoke_box(0.95, [100, 200, 300, 400]),
            smoke_box(0.6, [200, 300, 400, 500]),
        ]
        first = self.export_rows(payload)[0].split(",")
        self.assertEqual([float(value) for value in first[7:11]], [100, 200, 300, 400])
        self.assertAlmostEqual(float(first[11]), 0.95)

    def test_non_smoke_or_missing_label_is_rejected(self):
        for label in ("person", "fire", "", None):
            with self.subTest(label=label):
                payload = recorded_payload()
                payload["detections"][0]["boxes"].append(smoke_box(0.1, label=label))
                with self.assertRaises(ValueError):
                    self.export_rows(payload)

    def test_empty_detections_and_empty_boxes_export_no_records(self):
        payload = recorded_payload()
        payload["detections"] = []
        self.assertEqual(self.export_rows(payload), [])
        payload["detections"] = [{"frame_id": 0, "timestamp_sec": 0.0, "boxes": []}]
        self.assertEqual(self.export_rows(payload), [])

    def test_non_finite_timestamp_confidence_and_bbox_are_rejected(self):
        for value in (float("nan"), float("inf"), float("-inf")):
            for field in ("timestamp_sec", "confidence", "bbox"):
                with self.subTest(value=value, field=field):
                    payload = recorded_payload()
                    frame = payload["detections"][0]
                    if field == "timestamp_sec":
                        frame[field] = value
                    elif field == "confidence":
                        frame["boxes"][0][field] = value
                    else:
                        frame["boxes"][0][field][0] = value
                    with self.assertRaises(ValueError):
                        self.export_rows(payload)

    def test_frame_ids_are_uint64_and_strictly_increasing(self):
        for value in (-1, 2 ** 64, 1.5, True, "101", None):
            with self.subTest(frame_id=value):
                payload = recorded_payload()
                payload["detections"][0]["frame_id"] = value
                with self.assertRaises(ValueError):
                    self.export_rows(payload)
        for value in (101, 100):
            with self.subTest(second_frame_id=value):
                payload = recorded_payload()
                payload["detections"][1]["frame_id"] = value
                with self.assertRaises(ValueError):
                    self.export_rows(payload)
        for value in (0, 2 ** 64 - 1):
            payload = recorded_payload()
            payload["detections"] = [payload["detections"][0]]
            payload["detections"][0]["frame_id"] = value
            self.assertEqual(self.export_rows(payload)[0].split(",")[2], str(value))

    def test_invalid_bbox_confidence_and_image_dimensions_are_rejected(self):
        for bbox in ([-1, 20, 110, 120], [10, -1, 110, 120],
                     [10, 20, 1921, 120], [10, 20, 110, 1081],
                     [10, 20, 10, 120], [110, 20, 10, 120],
                     [10, 120, 110, 20], [10, 20, 110]):
            with self.subTest(bbox=bbox):
                payload = recorded_payload()
                payload["detections"][0]["boxes"][0]["bbox"] = bbox
                with self.assertRaises(ValueError):
                    self.export_rows(payload)
        for confidence in (-0.1, 1.01):
            payload = recorded_payload()
            payload["detections"][0]["boxes"][0]["confidence"] = confidence
            with self.assertRaises(ValueError):
                self.export_rows(payload)
        for dimension in (0, 1, 16385):
            payload = recorded_payload()
            payload["width"] = dimension
            with self.assertRaises(ValueError):
                self.export_rows(payload)

    def test_epoch_is_stamped_into_rows(self):
        self.assertEqual(list(BRIDGE.rows(recorded_payload(), "drone-1", "wide", 0))[0].split(",")[1], "0")
        rows = list(BRIDGE.rows(recorded_payload(), "drone-1", "wide", 5))
        self.assertTrue(all(row.split(",")[1] == "5" for row in rows))

    def test_invalid_epoch_is_rejected(self):
        for bad in (-1, 2.5, True, "2", 2**64):
            with self.assertRaises(ValueError):
                list(BRIDGE.rows(recorded_payload(), "drone-1", "wide", bad))

    def test_cli_defaults_and_accepts_epoch(self):
        with tempfile.TemporaryDirectory(prefix="smoke-export-epoch-") as directory:
            payload = recorded_payload()
            payload["stream_epoch"] = 4
            completed, output = self.run_cli(directory, payload)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            # Without --epoch the CLI still stamps 0.
            self.assertEqual(output.read_text(encoding="utf-8").splitlines()[1].split(",")[1], "0")
            custom = Path(directory) / "detections-epoch.csv"
            completed = subprocess.run(
                [sys.executable, str(EXPORTER), "--detections",
                 str(Path(directory) / "detection_results.json"),
                 "--output", str(custom), "--source", "drone-1",
                 "--lens", "wide", "--epoch", "4"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                universal_newlines=True, timeout=10,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertEqual(custom.read_text(encoding="utf-8").splitlines()[1].split(",")[1], "4")
            self.assertEqual(json.loads(completed.stdout)["epoch"], 4)

    def test_invalid_source_is_rejected(self):
        for source in ("", "with space", "contains,comma", "a" * 65):
            with self.subTest(source=source):
                with self.assertRaises(ValueError):
                    list(BRIDGE.rows(recorded_payload(), source, "wide"))

    def test_invalid_box_is_not_hidden_by_a_stronger_valid_box(self):
        invalid_boxes = [
            smoke_box(0.1, [-1, 20, 110, 120]),
            smoke_box(0.1, [10, 20, 1921, 120]),
            smoke_box(0.1, [10, 20, float("nan"), 120]),
            smoke_box(0.1, [10, 20, float("inf"), 120]),
            smoke_box(-0.1),
            smoke_box(float("nan")),
        ]
        for invalid in invalid_boxes:
            with self.subTest(invalid_box=invalid):
                payload = recorded_payload()
                payload["detections"][0]["boxes"] = [smoke_box(0.9), invalid]
                with self.assertRaises(ValueError):
                    self.export_rows(payload)

    def test_dimensions_must_be_integers_without_lossy_coercion(self):
        for dimension in (1920.9, "1920", True, False, float("nan"), float("inf")):
            with self.subTest(width=dimension):
                payload = recorded_payload()
                payload["width"] = dimension
                with self.assertRaises((ValueError, TypeError, OverflowError)):
                    self.export_rows(payload)

    def test_timestamps_must_not_move_backwards(self):
        payload = recorded_payload()
        payload["detections"][1]["timestamp_sec"] = 1.0
        with self.assertRaises(ValueError):
            self.export_rows(payload)

    def test_public_rows_helper_rejects_unknown_or_injected_lens_values(self):
        for lens in ("", "thermal", "Wide", "wide,zoom", "wide\nforged-row"):
            with self.subTest(lens=lens):
                with self.assertRaises(ValueError):
                    list(BRIDGE.rows(recorded_payload(), "drone-1", lens))
        for lens in ("unknown", "wide", "zoom", "ir", "fpv"):
            with self.subTest(valid_lens=lens):
                row = list(BRIDGE.rows(recorded_payload(), "drone-1", lens))[0]
                self.assertEqual(row.split(",")[4], lens)

    def test_cli_exports_real_schema_json_and_reports_record_count(self):
        with tempfile.TemporaryDirectory(prefix="smoke-export-test-") as directory:
            completed, output = self.run_cli(directory, recorded_payload())
            self.assertEqual(completed.returncode, 0, completed.stderr)
            report = json.loads(completed.stdout)
            self.assertEqual(report["mode"], "metadata_export")
            self.assertEqual(report["records"], 2)
            self.assertEqual(report["input_model"], "models/custom-smoke.pt")
            lines = output.read_text(encoding="utf-8").splitlines()
            self.assertTrue(lines[0].startswith("#"))
            self.assertEqual(lines[1:], self.export_rows(recorded_payload()))

    def test_cli_never_overwrites_existing_output(self):
        with tempfile.TemporaryDirectory(prefix="smoke-export-test-") as directory:
            output = Path(directory) / "existing.csv"
            sentinel = b"existing report must survive\r\n"
            output.write_bytes(sentinel)
            completed, output = self.run_cli(directory, recorded_payload(), output)
            self.assertNotEqual(completed.returncode, 0)
            self.assertEqual(output.read_bytes(), sentinel)


if __name__ == "__main__":
    unittest.main()
