import sys
import unittest
from pathlib import Path


PYTHON_DIR = Path(__file__).resolve().parents[1]
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from ai_service.analyze_video import build_video_summary, select_keyframes


def detection_frame(frame_id, fps, confidences):
    return {
        "frame_id": frame_id,
        "timestamp_sec": frame_id / fps,
        "boxes": [
            {
                "bbox": [10.0, 20.0, 30.0, 40.0],
                "confidence": confidence,
                "class_id": 0,
                "class_name": "smoke",
            }
            for confidence in confidences
        ],
    }


class AnalyzeVideoTests(unittest.TestCase):
    def test_summary_without_detections(self):
        summary = build_video_summary(
            "empty.mp4",
            fps=25.0,
            frame_count=100,
            width=1920,
            height=1080,
            detection_frames=[],
        )
        self.assertFalse(summary["smoke_detected"])
        self.assertEqual(summary["positive_frame_count"], 0)
        self.assertEqual(summary["positive_frame_ratio"], 0.0)
        self.assertIsNone(summary["first_detection_sec"])
        self.assertIsNone(summary["last_detection_sec"])
        self.assertIsNone(summary["max_confidence"])
        self.assertIsNone(summary["mean_confidence"])
        self.assertEqual(summary["keyframes"], [])

    def test_max_and_mean_confidence(self):
        frames = [
            detection_frame(10, 10.0, [0.4, 0.8]),
            detection_frame(20, 10.0, [0.6]),
        ]
        summary = build_video_summary(
            "detections.mp4",
            fps=10.0,
            frame_count=30,
            width=640,
            height=480,
            detection_frames=frames,
        )
        self.assertAlmostEqual(summary["max_confidence"], 0.8)
        self.assertAlmostEqual(summary["mean_confidence"], 0.6)

    def test_positive_frame_ratio_counts_frames_not_boxes(self):
        frames = [
            detection_frame(1, 30.0, [0.5, 0.6]),
            detection_frame(8, 30.0, [0.7]),
        ]
        summary = build_video_summary(
            "ratio.mp4",
            fps=30.0,
            frame_count=10,
            width=640,
            height=480,
            detection_frames=frames,
        )
        self.assertEqual(summary["positive_frame_count"], 2)
        self.assertAlmostEqual(summary["positive_frame_ratio"], 0.2)

    def test_keyframe_selection_removes_duplicate_frames(self):
        only = [detection_frame(4, 10.0, [0.9])]
        selected = select_keyframes(only)
        self.assertEqual(
            [(role, item["frame_id"]) for role, item in selected],
            [("first_detection", 4)],
        )

        two = [
            detection_frame(4, 10.0, [0.9]),
            detection_frame(8, 10.0, [0.2]),
        ]
        selected = select_keyframes(two)
        self.assertEqual(
            [(role, item["frame_id"]) for role, item in selected],
            [
                ("first_detection", 4),
                ("last_detection", 8),
            ],
        )


if __name__ == "__main__":
    unittest.main()
