import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from client import detection_lines
from load_test import percentile


class BridgeTest(unittest.TestCase):
    def payload(self):
        return {"width": 1920, "height": 1080, "detections": [
            {"frame_id": 101, "timestamp_sec": 3.3666666667, "boxes": [
                {"confidence": 0.5, "bbox": [1, 2, 3, 4], "class_name": "smoke"},
                {"confidence": 0.9, "bbox": [10, 20, 30, 40], "class_name": "smoke"},
            ]}]}

    def test_strongest_box_and_stable_id(self):
        line = list(detection_lines(self.payload(), "video-11"))[0].split()
        self.assertEqual(line[:4], ["DETECT", "video-11", "102", "3367"])
        self.assertAlmostEqual(float(line[4]), 0.9)
        self.assertEqual(line[5:9], ["10", "20", "30", "40"])

    def test_rejects_unordered_frames_and_non_smoke(self):
        payload = self.payload()
        payload["detections"] *= 2
        with self.assertRaises(ValueError):
            list(detection_lines(payload, "video-11"))
        payload = self.payload()
        payload["detections"][0]["boxes"][0]["class_name"] = "person"
        with self.assertRaises(ValueError):
            list(detection_lines(payload, "video-11"))

    def test_invalid_source_and_empty_input(self):
        with self.assertRaises(ValueError):
            list(detection_lines(self.payload(), "a\nb"))
        self.assertEqual(list(detection_lines({"width": 1, "height": 1, "detections": []}, "empty")), [])

    def test_percentiles(self):
        self.assertIsNone(percentile([], 99))
        self.assertEqual(percentile([3, 1, 2], 50), 2)
        self.assertAlmostEqual(percentile([0, 100], 99), 99)


if __name__ == "__main__":
    unittest.main()
