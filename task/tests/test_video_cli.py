"""Real-model CLI tests on explicitly SYNTHETIC, tiny black MP4 fixtures.

Usage: python3 test_video_cli.py /path/to/smoke_video /path/to/best.onnx [-v]
This suite does not assert drone footage accuracy, hardware control or GPU speed.
It requires cv2 and numpy to generate/inspect fixtures; no test downloads a model.
"""

import hashlib
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest

import cv2
import numpy as np


BINARY = None
MODEL = None
MODEL_SHA256 = "7bfb2d6745640bd608faae589e5980dbd851b66cacadf356df43fa71053fc077"


def strict_json(text):
    def reject(value):
        raise ValueError("Invalid JSON numeric constant: " + value)

    return json.loads(text, parse_constant=reject)


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def synthetic_video(path, frame_count):
    writer = cv2.VideoWriter(
        str(path), cv2.VideoWriter_fourcc(*"mp4v"), 10.0, (64, 48),
    )
    if not writer.isOpened():
        raise RuntimeError("Cannot create synthetic MP4 fixture")
    try:
        image = np.zeros((48, 64, 3), dtype=np.uint8)
        for _ in range(frame_count):
            writer.write(image)
    finally:
        writer.release()


def mp4_boxes(data):
    """Read the small, 32-bit-size boxes emitted by the synthetic MP4 writer."""
    offset = 0
    while offset < len(data):
        if len(data) - offset < 8:
            raise ValueError("Truncated synthetic MP4 box")
        size, kind = struct.unpack_from(">I4s", data, offset)
        if size < 8 or size > len(data) - offset:
            raise ValueError("Unsupported synthetic MP4 box size")
        yield offset, size, kind
        offset += size


def patch_synthetic_vfr(source, destination):
    """Change only synthetic sample durations, preserving media bytes/duration.

    The four sample durations become d, d/2, 3d/2, d: the third frame PTS
    differs from CFR by 50 ms while the sample count and total duration stay
    unchanged. moov is required to follow mdat, so growing stts cannot alter
    any compressed-sample chunk offset. This is not a patch of real footage.
    """
    data = source.read_bytes()
    top = list(mp4_boxes(data))
    moov = [offset for offset, _, kind in top if kind == b"moov"]
    mdat = [offset for offset, _, kind in top if kind == b"mdat"]
    if len(moov) != 1 or len(mdat) != 1 or moov[0] < mdat[0]:
        raise ValueError("Synthetic MP4 must have trailing moov and one mdat")
    replaced = [0]
    containers = {b"moov", b"trak", b"mdia", b"minf", b"stbl"}

    def rewrite(boxes):
        result = []
        for offset, size, kind in mp4_boxes(boxes):
            payload = boxes[offset + 8:offset + size]
            if kind in containers:
                payload = rewrite(payload)
            elif kind == b"stts":
                if len(payload) < 8:
                    raise ValueError("Truncated synthetic stts")
                count = struct.unpack_from(">I", payload, 4)[0]
                if len(payload) != 8 + count * 8:
                    raise ValueError("Invalid synthetic stts table length")
                durations = []
                for index in range(count):
                    samples, delta = struct.unpack_from(">II", payload, 8 + index * 8)
                    if samples > 4:
                        raise ValueError("VFR patch requires exactly four synthetic frames")
                    durations.extend([delta] * samples)
                if len(durations) != 4 or len(set(durations)) != 1 or durations[0] < 2:
                    raise ValueError("VFR patch requires a four-frame CFR synthetic source")
                delta = durations[0]
                varied = (delta, delta // 2, 2 * delta - delta // 2, delta)
                payload = payload[:4] + struct.pack(">I", 4) + b"".join(
                    struct.pack(">II", 1, value) for value in varied
                )
                replaced[0] += 1
            result.append(struct.pack(">I4s", len(payload) + 8, kind) + payload)
        return b"".join(result)

    altered = rewrite(data)
    if replaced[0] != 1:
        raise ValueError("Expected exactly one synthetic video stts table")
    destination.write_bytes(altered)


class VideoCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if file_sha256(MODEL) != MODEL_SHA256:
            raise RuntimeError("Tests require the pinned project best.onnx SHA256")
        cls.fixture_directory = tempfile.TemporaryDirectory(prefix="smoke-synthetic-mp4-")
        cls.video = Path(cls.fixture_directory.name) / "synthetic-black.mp4"
        synthetic_video(cls.video, 4)
        capture = cv2.VideoCapture(str(cls.video))
        try:
            if not capture.isOpened() or int(capture.get(cv2.CAP_PROP_FRAME_COUNT)) != 4:
                raise RuntimeError("Synthetic MP4 fixture cannot be decoded")
        finally:
            capture.release()

    @classmethod
    def tearDownClass(cls):
        # Class fixture cleanup also works on the original Nano's Python 3.6.
        cls.fixture_directory.cleanup()

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="smoke-video-cli-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def invoke(self, video=None, model=None, output=None, extra=(), timeout=60):
        output = output or self.root / "analysis"
        command = [
            str(BINARY), "--video", str(video or self.video),
            "--model", str(model or MODEL), "--output-dir", str(output),
        ] + list(extra)
        process = subprocess.run(
            command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            universal_newlines=True, timeout=timeout, cwd=str(self.root),
        )
        return process, output

    def assert_incomplete(self, process, output):
        self.assertNotEqual(process.returncode, 0, process.stdout)
        summary = output / "video_summary.json"
        if summary.exists():
            payload = strict_json(summary.read_text(encoding="utf-8"))
            self.assertNotEqual(payload.get("status"), "complete")
        # Temporary .part files never establish successful completion.

    def test_real_pinned_model_on_synthetic_video_completes(self):
        process, output = self.invoke()
        self.assertEqual(process.returncode, 0, process.stderr)
        stdout_summary = strict_json(process.stdout)
        summary_file = output / "video_summary.json"
        detections_file = output / "detection_results.json"
        annotated_file = output / "annotated_video.mp4"
        for path in (summary_file, detections_file, annotated_file):
            self.assertTrue(path.is_file(), str(path))
            self.assertGreater(path.stat().st_size, 0, str(path))
        summary = strict_json(summary_file.read_text(encoding="utf-8"))
        strict_json(detections_file.read_text(encoding="utf-8"))
        for payload in (stdout_summary, summary):
            self.assertEqual(payload["status"], "complete")
            self.assertEqual(payload["backend"], "opencv_dnn_cpu")
            self.assertEqual(payload["hardware_commands"], 0)
            self.assertEqual(payload["frame_count"], 4)
            self.assertEqual(payload["width"], 64)
            self.assertEqual(payload["height"], 48)
            self.assertAlmostEqual(payload["fps"], 10.0, places=5)
            positives = payload["positive_frame_count"]
            self.assertGreaterEqual(positives, 0)
            self.assertLessEqual(positives, 4)
            self.assertAlmostEqual(payload["positive_frame_ratio"], positives / 4.0)
            if positives == 0:
                self.assertIsNone(payload["max_confidence"])
                self.assertIsNone(payload["mean_confidence"])
            else:
                self.assertGreaterEqual(payload["mean_confidence"], 0.0)
                self.assertLessEqual(payload["mean_confidence"], payload["max_confidence"])
                self.assertLessEqual(payload["max_confidence"], 1.0)
        self.assertEqual(stdout_summary, summary)
        keyframes = summary["keyframes"]
        self.assertIsInstance(keyframes, list)
        self.assertLessEqual(len(keyframes), 3)
        names = []
        for entry in keyframes:
            name = entry if isinstance(entry, str) else entry.get("file", entry.get("path"))
            self.assertIsInstance(name, str)
            relative = Path(name)
            self.assertFalse(relative.is_absolute())
            self.assertNotIn("..", relative.parts)
            self.assertEqual(relative.parts[0], "keyframes")
            names.append(name)
            self.assertIsNotNone(cv2.imread(str(output / relative)))
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual(list(output.rglob("*.part")), [])
        capture = cv2.VideoCapture(str(annotated_file))
        try:
            self.assertTrue(capture.isOpened())
            count = 0
            while True:
                ok, frame = capture.read()
                if not ok:
                    break
                self.assertEqual(frame.shape[:2], (48, 64))
                count += 1
            self.assertEqual(count, 4)
        finally:
            capture.release()

    def test_invalid_cli_exits(self):
        cases = (
            ("--conf", "nan"), ("--conf", "-0.1"),
            ("--iou", "1.1"), ("--max-output-bytes", "0"),
            ("--unknown-option", "1"),
        )
        for index, arguments in enumerate(cases):
            with self.subTest(arguments=arguments):
                process, output = self.invoke(
                    extra=arguments, output=self.root / str(index), timeout=15,
                )
                self.assert_incomplete(process, output)

    def test_missing_video_is_rejected(self):
        process, output = self.invoke(video=self.root / "missing.mp4", timeout=15)
        self.assert_incomplete(process, output)

    def test_corrupt_video_is_rejected(self):
        corrupt = self.root / "corrupt.mp4"
        corrupt.write_bytes(b"synthetic fixture: not an MP4")
        process, output = self.invoke(video=corrupt, timeout=15)
        self.assert_incomplete(process, output)

    def test_missing_model_is_rejected_without_fallback(self):
        process, output = self.invoke(model=self.root / "missing.onnx", timeout=15)
        self.assert_incomplete(process, output)

    def test_other_model_bytes_are_rejected_without_fallback(self):
        other_model = self.root / "best.onnx"
        other_model.write_bytes(b"not the pinned project model")
        process, output = self.invoke(model=other_model, timeout=15)
        self.assert_incomplete(process, output)

    def test_existing_output_directory_is_preserved(self):
        output = self.root / "existing"
        output.mkdir()
        marker = output / "keep.txt"
        marker.write_bytes(b"original user content")
        process, _ = self.invoke(output=output, timeout=15)
        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(marker.read_bytes(), b"original user content")
        self.assertEqual(sorted(path.name for path in output.iterdir()), ["keep.txt"])

    def test_quota_failure_never_claims_complete(self):
        process, output = self.invoke(
            extra=("--max-output-bytes", "1024"), timeout=60,
        )
        self.assert_incomplete(process, output)

    def test_synthetic_variable_timestamps_are_rejected(self):
        variable = self.root / "synthetic-variable-timestamps.mp4"
        patch_synthetic_vfr(self.video, variable)
        capture = cv2.VideoCapture(str(variable), cv2.CAP_FFMPEG)
        try:
            self.assertTrue(capture.isOpened())
            count = 0
            while capture.read()[0]:
                count += 1
            self.assertEqual(count, 4, "VFR fixture must remain fully decodable")
        finally:
            capture.release()
        process, output = self.invoke(video=variable)
        self.assert_incomplete(process, output)
        self.assertTrue(
            "pts" in process.stderr.lower() or "cfr" in process.stderr.lower(),
            "Expected timestamp rejection, not an unrelated failure: " + process.stderr,
        )

    @unittest.skipUnless(os.name == "posix", "SIGTERM regression targets the Linux video CLI")
    def test_sigterm_during_processing_never_publishes_complete(self):
        long_video = self.root / "synthetic-interruption.mp4"
        synthetic_video(long_video, 256)
        output = self.root / "interrupted-analysis"
        process = subprocess.Popen(
            [str(BINARY), "--video", str(long_video), "--model", str(MODEL),
             "--output-dir", str(output)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            universal_newlines=True, cwd=str(self.root),
        )
        try:
            deadline = time.monotonic() + 15.0
            partial = output / "detection_results.part.json"
            while process.poll() is None and time.monotonic() < deadline:
                if partial.is_file() and partial.stat().st_size > 0:
                    break
                time.sleep(0.02)
            self.assertIsNone(process.poll(), "Process exited before interruption checkpoint")
            self.assertTrue(partial.is_file() and partial.stat().st_size > 0,
                            "Never observed in-progress inference output")
            process.send_signal(signal.SIGTERM)
            stdout, stderr = process.communicate(timeout=15)
            self.assertNotEqual(process.returncode, 0, stdout)
            self.assertIn("analysis_interrupted", stderr)
            if (output / "video_summary.json").exists():
                summary = strict_json((output / "video_summary.json").read_text(encoding="utf-8"))
                self.assertNotEqual(summary.get("status"), "complete")
            self.assertFalse((output / "annotated_video.mp4").exists())
            self.assertFalse((output / "detection_results.json").exists())
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate(timeout=5)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit("Usage: test_video_cli.py BINARY best.onnx [unittest options]")
    BINARY = Path(sys.argv.pop(1)).resolve()
    MODEL = Path(sys.argv.pop(1)).resolve()
    if not BINARY.is_file() or not MODEL.is_file():
        raise SystemExit("Both the executable and pinned model must exist")
    unittest.main()
