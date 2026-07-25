"""Analyze a real video frame by frame with a real Ultralytics YOLO model."""

import argparse
import json
import math
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple


KEYFRAME_FILENAMES = {
    "first_detection": "first_detection.jpg",
    "max_confidence": "max_confidence.jpg",
    "last_detection": "last_detection.jpg",
}


def frame_max_confidence(frame_result: Dict[str, Any]) -> float:
    boxes = frame_result.get("boxes", [])
    return max((float(box["confidence"]) for box in boxes), default=0.0)


def select_keyframes(
    detection_frames: Sequence[Dict[str, Any]],
) -> List[Tuple[str, Dict[str, Any]]]:
    """Select first, strongest, and last detection frames without duplicates."""
    if not detection_frames:
        return []

    ordered = sorted(detection_frames, key=lambda item: int(item["frame_id"]))
    first = ordered[0]
    strongest = max(
        ordered,
        key=lambda item: (
            frame_max_confidence(item),
            -int(item["frame_id"]),
        ),
    )
    last = ordered[-1]

    selected: List[Tuple[str, Dict[str, Any]]] = []
    seen_frame_ids = set()
    for role, frame_result in (
        ("first_detection", first),
        ("max_confidence", strongest),
        ("last_detection", last),
    ):
        frame_id = int(frame_result["frame_id"])
        if frame_id not in seen_frame_ids:
            selected.append((role, frame_result))
            seen_frame_ids.add(frame_id)
    return selected


def build_video_summary(
    video: str,
    fps: float,
    frame_count: int,
    width: int,
    height: int,
    detection_frames: Sequence[Dict[str, Any]],
) -> Dict[str, Any]:
    """Build summary statistics from positive-frame detection records."""
    ordered = sorted(detection_frames, key=lambda item: int(item["frame_id"]))
    confidences = [
        float(box["confidence"])
        for frame_result in ordered
        for box in frame_result.get("boxes", [])
    ]
    selected = select_keyframes(ordered)

    return {
        "video": video,
        "smoke_detected": bool(ordered),
        "fps": float(fps),
        "frame_count": int(frame_count),
        "width": int(width),
        "height": int(height),
        "duration_sec": (frame_count / fps) if fps > 0.0 else 0.0,
        "positive_frame_count": len(ordered),
        "positive_frame_ratio": (
            len(ordered) / frame_count if frame_count > 0 else 0.0
        ),
        "first_detection_sec": (
            float(ordered[0]["timestamp_sec"]) if ordered else None
        ),
        "last_detection_sec": (
            float(ordered[-1]["timestamp_sec"]) if ordered else None
        ),
        "max_confidence": max(confidences) if confidences else None,
        "mean_confidence": (
            sum(confidences) / len(confidences) if confidences else None
        ),
        "keyframes": [
            {
                "type": role,
                "frame_id": int(frame_result["frame_id"]),
                "timestamp_sec": float(frame_result["timestamp_sec"]),
                "file": "keyframes/{}".format(KEYFRAME_FILENAMES[role]),
            }
            for role, frame_result in selected
        ],
    }


def positive_float(value: str) -> float:
    number = float(value)
    if not 0.0 <= number <= 1.0:
        raise argparse.ArgumentTypeError("value must be between 0 and 1")
    return number


def write_json(path: Path, payload: Dict[str, Any]) -> None:
    with path.open("w", encoding="utf-8") as stream:
        json.dump(payload, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def class_name(names: Any, class_id: int) -> str:
    if isinstance(names, dict):
        return str(names.get(class_id, class_id))
    if isinstance(names, (list, tuple)) and 0 <= class_id < len(names):
        return str(names[class_id])
    return str(class_id)


def extract_boxes(result: Any, names: Any) -> List[Dict[str, Any]]:
    boxes: List[Dict[str, Any]] = []
    if result.boxes is None:
        return boxes

    for detected_box in result.boxes:
        boxes.append(
            {
                "bbox": [
                    float(value)
                    for value in detected_box.xyxy[0].detach().cpu().tolist()
                ],
                "confidence": float(
                    detected_box.conf[0].detach().cpu().item()
                ),
                "class_id": int(detected_box.cls[0].detach().cpu().item()),
                "class_name": class_name(
                    names,
                    int(detected_box.cls[0].detach().cpu().item()),
                ),
            }
        )
    return boxes


def analyze_video(
    video_path: Path,
    model_path: Path,
    output_dir: Path,
    device: str = "0",
    confidence: float = 0.25,
) -> Dict[str, Any]:
    """Run real inference on every decodable frame and write all artifacts."""
    try:
        import cv2
        from ultralytics import YOLO
    except ImportError as exc:
        raise RuntimeError(
            "opencv-python and ultralytics are required"
        ) from exc

    video_path = video_path.resolve()
    model_path = model_path.resolve()
    output_dir = output_dir.resolve()
    if not video_path.is_file():
        raise FileNotFoundError("Video does not exist: {}".format(video_path))
    if not model_path.is_file():
        raise FileNotFoundError("Model does not exist: {}".format(model_path))

    output_dir.mkdir(parents=True, exist_ok=True)
    keyframe_dir = output_dir / "keyframes"
    keyframe_dir.mkdir(parents=True, exist_ok=True)

    capture = cv2.VideoCapture(str(video_path))
    if not capture.isOpened():
        raise RuntimeError("Unable to open video: {}".format(video_path))

    width = int(round(capture.get(cv2.CAP_PROP_FRAME_WIDTH)))
    height = int(round(capture.get(cv2.CAP_PROP_FRAME_HEIGHT)))
    fps = float(capture.get(cv2.CAP_PROP_FPS))
    reported_frame_count = int(
        round(capture.get(cv2.CAP_PROP_FRAME_COUNT))
    )
    if width <= 0 or height <= 0 or not math.isfinite(fps) or fps <= 0.0:
        capture.release()
        raise RuntimeError(
            "Invalid video metadata: width={}, height={}, fps={}".format(
                width,
                height,
                fps,
            )
        )

    annotated_video = output_dir / "annotated_video.mp4"
    writer = cv2.VideoWriter(
        str(annotated_video),
        cv2.VideoWriter_fourcc(*"mp4v"),
        fps,
        (width, height),
    )
    if not writer.isOpened():
        capture.release()
        raise RuntimeError(
            "Unable to create annotated video: {}".format(annotated_video)
        )

    print("Loading model: {}".format(model_path), flush=True)
    model = YOLO(str(model_path))
    print(
        "Opened video: {} ({}x{}, {:.6f} FPS, {} reported frames)".format(
            video_path,
            width,
            height,
            fps,
            reported_frame_count,
        ),
        flush=True,
    )

    detection_frames: List[Dict[str, Any]] = []
    processed_frames = 0
    first_annotated: Optional[Any] = None
    strongest_annotated: Optional[Any] = None
    last_annotated: Optional[Any] = None
    strongest_confidence = -1.0

    try:
        while True:
            ok, frame = capture.read()
            if not ok:
                break

            result = model.predict(
                source=frame,
                conf=confidence,
                device=device,
                verbose=False,
            )[0]
            boxes = extract_boxes(result, model.names)
            annotated_frame = result.plot()
            writer.write(annotated_frame)

            if boxes:
                frame_result = {
                    "frame_id": processed_frames,
                    "timestamp_sec": processed_frames / fps,
                    "boxes": boxes,
                }
                detection_frames.append(frame_result)
                if first_annotated is None:
                    first_annotated = annotated_frame.copy()
                last_annotated = annotated_frame.copy()
                current_max = frame_max_confidence(frame_result)
                if current_max > strongest_confidence:
                    strongest_confidence = current_max
                    strongest_annotated = annotated_frame.copy()

            processed_frames += 1
            if processed_frames % 100 == 0:
                print(
                    "Processed {}/{} frames; positive frames: {}".format(
                        processed_frames,
                        (
                            reported_frame_count
                            if reported_frame_count > 0
                            else "?"
                        ),
                        len(detection_frames),
                    ),
                    flush=True,
                )
    finally:
        capture.release()
        writer.release()

    if processed_frames == 0:
        raise RuntimeError("No frames were decoded from the video")
    if not annotated_video.is_file() or annotated_video.stat().st_size == 0:
        raise RuntimeError("Annotated video was not created correctly")

    detection_payload = {
        "video": str(video_path),
        "model": str(model_path),
        "fps": fps,
        "frame_count": processed_frames,
        "width": width,
        "height": height,
        "detections": detection_frames,
    }
    summary = build_video_summary(
        video=str(video_path),
        fps=fps,
        frame_count=processed_frames,
        width=width,
        height=height,
        detection_frames=detection_frames,
    )

    keyframe_images = {
        "first_detection": first_annotated,
        "max_confidence": strongest_annotated,
        "last_detection": last_annotated,
    }
    for role, _frame_result in select_keyframes(detection_frames):
        keyframe_path = keyframe_dir / KEYFRAME_FILENAMES[role]
        image = keyframe_images[role]
        if image is None or not cv2.imwrite(str(keyframe_path), image):
            raise RuntimeError(
                "Unable to write keyframe: {}".format(keyframe_path)
            )

    write_json(output_dir / "detection_results.json", detection_payload)
    write_json(output_dir / "video_summary.json", summary)

    print("Analysis complete: {}".format(output_dir), flush=True)
    print(json.dumps(summary, ensure_ascii=False, indent=2), flush=True)
    return summary


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Analyze a real MP4 with a real YOLO model."
    )
    parser.add_argument("--video", required=True, type=Path)
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument(
        "--device",
        default="0",
        help="Ultralytics device such as 0 or cpu (default: 0).",
    )
    parser.add_argument(
        "--conf",
        type=positive_float,
        default=0.25,
        help="Confidence threshold from 0 to 1 (default: 0.25).",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    analyze_video(
        video_path=args.video,
        model_path=args.model,
        output_dir=args.output_dir,
        device=args.device,
        confidence=args.conf,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
