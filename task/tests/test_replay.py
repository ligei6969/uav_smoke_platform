"""CLI integration tests using SYNTHETIC METADATA, never inference or hardware.

Usage: python test_replay.py /path/to/smoke_replay [unittest options]
Every subprocess has a timeout and all generated fixtures use temporary paths.
"""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


BINARY = None
SOURCE = "synthetic-camera"


def strict_json(text):
    def reject_constant(value):
        raise ValueError("Non-JSON numeric constant: " + value)

    return json.loads(text, parse_constant=reject_constant)


def detection(frame_id=1, timestamp=1.0, source=SOURCE, epoch=0, lens="wide"):
    # Box bottom-centre is the image principal point: a straight-down pose
    # therefore projects exactly to the synthetic aircraft latitude/longitude.
    return ",".join(str(value) for value in (
        source, epoch, frame_id, timestamp, lens, 1920, 1080,
        900, 400, 1020, 540, 0.9,
    )) + "\n"


def telemetry(timestamp=1.0, source=SOURCE, latitude=30.0, longitude=120.0):
    return ",".join(str(value) for value in (
        source, timestamp, latitude, longitude, 100, 100,
        -90, 0, 0, 1, 1, 1,
    )) + "\n"


class ReplayIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="smoke-synthetic-replay-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def invoke(self, detection_rows, telemetry_rows=None, calibrated=True,
               extra=(), name="run", output=None, timeout=10):
        detections_path = self.root / (name + "-detections.csv")
        detections_path.write_text(
            "# SYNTHETIC METADATA fixture; no images, inference or hardware.\n"
            + "".join(detection_rows), encoding="utf-8",
        )
        output_path = output or self.root / (name + "-evaluation.jsonl")
        command = [
            str(BINARY), "--detections", str(detections_path),
            "--output", str(output_path), "--queue-capacity", "1",
            "--max-time-delta", "0.2",
        ]
        if calibrated:
            command += ["--lens", "wide", "--calibrated-hfov", "82"]
        if telemetry_rows is not None:
            telemetry_path = self.root / (name + "-telemetry.csv")
            telemetry_path.write_text("".join(telemetry_rows), encoding="utf-8")
            command += ["--telemetry", str(telemetry_path)]
        command += list(extra)
        process = subprocess.run(
            command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            universal_newlines=True, timeout=timeout,
            cwd=str(self.root),
        )
        return process, output_path

    def success(self, process, output_path):
        self.assertEqual(process.returncode, 0, process.stderr)
        summary = strict_json(process.stdout)
        self.assertEqual(summary["mode"], "metadata_replay")
        self.assertEqual(summary["hardware_commands"], 0)
        rows = [strict_json(line) for line in output_path.read_text(
            encoding="utf-8").splitlines()]
        self.assertEqual(summary["processed"], len(rows))
        self.assertLessEqual(summary["queue_high_water"], 1)
        for row in rows:
            self.assertEqual(row["control_mode"], "dry_run")
            self.assertEqual(row["location_model"], "flat_ground_hypothesis")
        return summary, rows

    def test_without_telemetry_location_is_invalid(self):
        process, output = self.invoke([detection()])
        summary, rows = self.success(process, output)
        self.assertEqual(summary["located"], 0)
        self.assertEqual(len(rows), 1)
        self.assertFalse(rows[0]["location_valid"])
        self.assertNotIn("latitude_deg", rows[0])
        self.assertIn("telemetry", rows[0]["location_reason"])

    def test_synthetic_straight_down_projection(self):
        process, output = self.invoke([detection()], [telemetry()])
        summary, rows = self.success(process, output)
        self.assertEqual(summary["located"], 1)
        self.assertTrue(rows[0]["location_valid"])
        self.assertAlmostEqual(rows[0]["latitude_deg"], 30.0, places=8)
        self.assertAlmostEqual(rows[0]["longitude_deg"], 120.0, places=8)
        self.assertEqual(rows[0]["source"], SOURCE)
        self.assertEqual(rows[0]["stream_epoch"], 0)
        self.assertEqual(rows[0]["lens"], "wide")

    def test_without_calibration_location_is_invalid(self):
        process, output = self.invoke(
            [detection()], [telemetry()], calibrated=False,
        )
        summary, rows = self.success(process, output)
        self.assertEqual(summary["located"], 0)
        self.assertFalse(rows[0]["location_valid"])
        self.assertEqual(rows[0]["location_reason"], "camera_uncalibrated")

    def test_stale_telemetry_is_not_used(self):
        process, output = self.invoke(
            [detection(timestamp=10.0)], [telemetry(timestamp=0.0)],
        )
        _, rows = self.success(process, output)
        self.assertFalse(rows[0]["location_valid"])
        self.assertEqual(rows[0]["location_reason"], "telemetry_stale")

    def test_bad_csv_exits_without_hanging(self):
        for name, bad_row in (
            ("field-count", "invalid,csv\n"),
            ("nan", detection().replace(",0.9\n", ",nan\n")),
            ("overlong", "x" * 1025 + "\n"),
        ):
            with self.subTest(name=name):
                process, _ = self.invoke([bad_row], name=name, timeout=5)
                self.assertNotEqual(process.returncode, 0)
                self.assertIn("smoke_replay:", process.stderr)

    def test_existing_output_is_not_overwritten(self):
        process, output = self.invoke([detection()])
        self.success(process, output)
        original = output.read_bytes()
        repeated, _ = self.invoke(
            [detection()], output=output, name="repeat", timeout=5,
        )
        self.assertNotEqual(repeated.returncode, 0)
        self.assertIn("cannot_create_output_exclusively", repeated.stderr)
        self.assertEqual(output.read_bytes(), original)

    def test_output_quota_limits_partial_output(self):
        limit = 1024
        process, output = self.invoke(
            [detection(frame_id=i, timestamp=i / 30.0) for i in range(100)],
            extra=("--max-output-bytes", str(limit)), timeout=5,
        )
        self.assertNotEqual(process.returncode, 0)
        self.assertIn("output_quota_exceeded", process.stderr)
        self.assertGreater(output.stat().st_size, 0)
        self.assertLessEqual(output.stat().st_size, limit)
        for line in output.read_text(encoding="utf-8").splitlines():
            strict_json(line)

    def test_session_changes_are_rejected(self):
        variants = {
            "epoch": detection(frame_id=2, timestamp=2.0, epoch=1),
            "lens": detection(frame_id=2, timestamp=2.0, lens="zoom"),
            "source": detection(frame_id=2, timestamp=2.0, source="other"),
            "frame-id": detection(frame_id=1, timestamp=2.0),
            "time": detection(frame_id=2, timestamp=0.5),
        }
        for name, second in variants.items():
            with self.subTest(name=name):
                process, _ = self.invoke(
                    [detection(), second], name=name, timeout=5,
                )
                self.assertNotEqual(process.returncode, 0)
                self.assertIn("replay_requires_one_source", process.stderr)

    def test_queue_one_drains_ten_thousand_records(self):
        count = 10000
        process, output = self.invoke(
            [detection(frame_id=i, timestamp=i / 30.0) for i in range(count)],
            timeout=30,
        )
        summary, rows = self.success(process, output)
        self.assertEqual(summary["processed"], count)
        self.assertEqual(summary["queue_high_water"], 1)
        self.assertEqual([row["frame_id"] for row in rows], list(range(count)))

    def test_other_high_rate_source_cannot_evict_best_telemetry(self):
        samples = [telemetry(timestamp=1.0)]
        samples += [
            telemetry(timestamp=1.0 + i / 2000.0, source="other-camera",
                      latitude=40.0, longitude=130.0)
            for i in range(1, 301)
        ]
        # This later same-source sample is admissible but less temporally close.
        samples.append(telemetry(timestamp=1.2, latitude=35.0, longitude=125.0))
        process, output = self.invoke([detection()], samples)
        _, rows = self.success(process, output)
        self.assertTrue(rows[0]["location_valid"])
        self.assertAlmostEqual(rows[0]["latitude_deg"], 30.0, places=8)
        self.assertAlmostEqual(rows[0]["longitude_deg"], 120.0, places=8)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("Usage: test_replay.py /path/to/smoke_replay [unittest options]")
    BINARY = Path(sys.argv.pop(1)).resolve()
    if not BINARY.is_file():
        raise SystemExit("Replay executable does not exist: " + str(BINARY))
    unittest.main()
