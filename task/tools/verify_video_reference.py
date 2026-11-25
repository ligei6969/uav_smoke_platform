"""Compare the pinned model's OpenCV probe with independent ORT CPU inference.

Uses real decoded frames 0, 233, 1231 and 6333 from the supplied drone video.
It independently performs RGB/114 letterboxing and CHW float32 normalization.
Example on Windows (the output directory must not already exist):

  python verify_video_reference.py --probe /mnt/j/.../smoke_model_probe \
      --model J:/.../best.onnx --video J:/.../11.mp4 \
      --output-dir J:/.../results/reference

A Windows host automatically runs an absolute Linux --probe through WSL.
For another launcher, put --probe-command and its tokens last on the command line.
No shell command strings, model downloads, or tolerance overrides are used.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import subprocess
import sys
import time

import cv2
import numpy as np
import onnxruntime as ort


MODEL_SHA256 = "7bfb2d6745640bd608faae589e5980dbd851b66cacadf356df43fa71053fc077"
FRAME_IDS = (0, 233, 1231, 6333)
INPUT_SHAPE = (1, 3, 640, 640)
OUTPUT_SHAPE = (1, 25200, 7)
RTOL = 1e-3
ATOL = 1e-4


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def linux_drive_path(path):
    """Map a resolved local Windows drive path to its standard WSL mount."""
    windows = PureWindowsPath(str(path))
    drive = windows.drive
    if not windows.is_absolute() or len(drive) != 2 or drive[1] != ":":
        raise ValueError("WSL probe paths must use a local Windows drive: " + str(path))
    return "/mnt/" + drive[0].lower() + "/" + "/".join(windows.parts[1:])


def independent_input(bgr):
    """Reference preprocessing, independent of the C++ implementation."""
    height, width = bgr.shape[:2]
    scale = min(640.0 / width, 640.0 / height)
    # The confirmed YOLOv5 letterbox contract uses Python ties-to-even rounding.
    resized_width = int(round(width * scale))
    resized_height = int(round(height * scale))
    resized = cv2.resize(
        bgr, (resized_width, resized_height), interpolation=cv2.INTER_LINEAR,
    )
    canvas = np.full((640, 640, 3), 114, dtype=np.uint8)
    left = (640 - resized_width) // 2
    top = (640 - resized_height) // 2
    canvas[top:top + resized_height, left:left + resized_width] = resized
    rgb = canvas[:, :, ::-1]
    return np.ascontiguousarray(
        rgb.transpose(2, 0, 1)[np.newaxis, :, :, :], dtype=np.float32,
    ) / np.float32(255.0)


def verify_odd_dimension_contract():
    """Exercise the .5 tie case without using any real-video result as truth."""
    # 193 * (640 / 256) = 482.5, which must round to even 482, not 483.
    bgr = np.zeros((193, 256, 3), dtype=np.uint8)
    bgr[:, :, 2] = 255
    tensor = independent_input(bgr)
    padding = np.float32(114.0) / np.float32(255.0)
    if (tensor.shape != INPUT_SHAPE or tensor.dtype != np.float32
            or not np.all(tensor[0, :, 78, :] == padding)
            or not np.all(tensor[0, :, 561, :] == padding)
            or not np.all(tensor[0, 0, 79:561, :] == 1.0)
            or not np.all(tensor[0, 1:, 79:561, :] == 0.0)):
        raise ValueError("Independent RGB/114/ties-to-even letterbox contract failed")
    return {
        "synthetic_contract_check": True,
        "original_width": 256,
        "original_height": 193,
        "resized_width": 640,
        "resized_height": 482,
        "pad_top": 79,
        "pad_bottom": 79,
        "rounding": "ties_to_even",
        "passed": True,
    }


def read_tensor(path, shape):
    expected_bytes = int(np.prod(shape)) * 4
    if not path.is_file() or path.stat().st_size != expected_bytes:
        raise ValueError("Wrong tensor byte count for {} (expected {})".format(
            path, expected_bytes,
        ))
    tensor = np.fromfile(str(path), dtype="<f4").reshape(shape)
    if not np.isfinite(tensor).all():
        raise ValueError("Non-finite tensor: " + str(path))
    return tensor


def compare_tensors(actual, expected):
    if actual.shape != expected.shape:
        raise ValueError("Tensor shape mismatch")
    if not np.isfinite(actual).all() or not np.isfinite(expected).all():
        raise ValueError("Tensor contains NaN or infinity")
    difference = np.abs(actual.astype(np.float64) - expected.astype(np.float64))
    tolerance = ATOL + RTOL * np.abs(expected.astype(np.float64))
    outside = difference > tolerance
    return {
        "passed": not bool(outside.any()),
        "shape": list(expected.shape),
        "element_count": int(expected.size),
        "outside_tolerance": int(np.count_nonzero(outside)),
        "max_absolute_error": float(difference.max()),
        "mean_absolute_error": float(difference.mean()),
        "max_relative_error": float((difference / np.maximum(
            np.abs(expected.astype(np.float64)), 1e-12,
        )).max()),
    }


def probe_launcher(args):
    if args.probe_command:
        command = list(args.probe_command)
        uses_wsl = Path(command[0]).name.lower() in ("wsl", "wsl.exe")
        return command, uses_wsl
    if os.name == "nt" and args.probe.startswith("/"):
        return ["wsl", "-d", args.wsl_distro, "--", args.probe], True
    return [args.probe], False


def run(args):
    model = args.model.resolve()
    video = args.video.resolve()
    output_dir = args.output_dir.resolve()
    if not model.is_file() or not video.is_file():
        raise ValueError("Both the real model and video must exist")
    model_hash = file_sha256(model)
    if model_hash != MODEL_SHA256:
        raise ValueError("Model SHA256 differs from pinned project best.onnx")
    odd_dimension_contract = verify_odd_dimension_contract()
    command_prefix, uses_wsl = probe_launcher(args)
    map_path = linux_drive_path if uses_wsl and os.name == "nt" else str
    model_for_probe = map_path(model)
    # Creating an exclusive directory protects an earlier verification report.
    output_dir.mkdir(parents=True, exist_ok=False)

    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    options.inter_op_num_threads = 1
    session = ort.InferenceSession(
        str(model), sess_options=options, providers=["CPUExecutionProvider"],
    )
    if len(session.get_inputs()) != 1 or len(session.get_outputs()) != 1:
        raise ValueError("Pinned model must have exactly one input and one output")
    model_input = session.get_inputs()[0]
    model_output = session.get_outputs()[0]
    if tuple(model_input.shape) != INPUT_SHAPE or tuple(model_output.shape) != OUTPUT_SHAPE:
        raise ValueError("Pinned model tensor shape contract differs")

    capture = cv2.VideoCapture(str(video))
    if not capture.isOpened():
        raise ValueError("Cannot open real video: " + str(video))
    report = {
        "status": "running",
        "video": str(video),
        "model": str(model),
        "model_sha256": model_hash,
        "reference_backend": "onnxruntime_cpu",
        "onnxruntime_version": ort.__version__,
        "opencv_python_version": cv2.__version__,
        "rtol": RTOL,
        "atol": ATOL,
        "required_frame_ids": list(FRAME_IDS),
        "odd_dimension_contract": odd_dimension_contract,
        "frames": [],
    }
    try:
        for frame_id in FRAME_IDS:
            frame_report = {"frame_id": frame_id}
            report["frames"].append(frame_report)
            try:
                if not capture.set(cv2.CAP_PROP_POS_FRAMES, frame_id):
                    raise ValueError("Video frame seek failed")
                ok, bgr = capture.read()
                if not ok:
                    raise ValueError("Cannot decode requested real frame")
                if abs(capture.get(cv2.CAP_PROP_POS_FRAMES) - (frame_id + 1)) > 0.5:
                    raise ValueError("Video decoder position does not match requested frame")
                image_path = output_dir / ("frame_{:06d}.png".format(frame_id))
                if not cv2.imwrite(str(image_path), bgr):
                    raise ValueError("Cannot save lossless probe image")
                tensor = independent_input(bgr)
                reference_started = time.perf_counter()
                reference = session.run(
                    [model_output.name], {model_input.name: tensor},
                )[0]
                frame_report["reference_seconds"] = time.perf_counter() - reference_started
                if reference.shape != OUTPUT_SHAPE:
                    raise ValueError("ORT returned an unexpected output shape")
                prefix = output_dir / ("frame_{:06d}".format(frame_id))
                tensor.astype("<f4", copy=False).tofile(str(prefix) + ".reference_input.bin")
                reference.astype("<f4", copy=False).tofile(str(prefix) + ".reference_output.bin")
                command = command_prefix + [
                    "--model", model_for_probe, "--image", map_path(image_path),
                    "--output-prefix", map_path(prefix),
                ]
                completed = subprocess.run(
                    command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    timeout=60,
                )
                frame_report["probe_returncode"] = completed.returncode
                if completed.returncode != 0:
                    # WSL startup diagnostics can mix UTF-16 and UTF-8 bytes on
                    # Windows. Binary capture avoids locale-dependent reader errors.
                    raise RuntimeError("Probe failed: " + completed.stderr.decode(
                        "utf-8", errors="replace",
                    ).strip())
                actual_input = read_tensor(Path(str(prefix) + ".input.bin"), INPUT_SHAPE)
                actual_output = read_tensor(Path(str(prefix) + ".output.bin"), OUTPUT_SHAPE)
                frame_report["input"] = compare_tensors(actual_input, tensor)
                frame_report["output"] = compare_tensors(actual_output, reference)
                frame_report["status"] = (
                    "passed" if frame_report["input"]["passed"]
                    and frame_report["output"]["passed"] else "failed"
                )
            except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
                frame_report["status"] = "error"
                frame_report["error"] = str(error)
            print("frame {}: {}".format(frame_id, frame_report["status"]), flush=True)
    finally:
        capture.release()

    all_passed = len(report["frames"]) == len(FRAME_IDS) and all(
        frame["status"] == "passed" for frame in report["frames"]
    )
    report["status"] = "passed" if all_passed else "failed"
    report_path = output_dir / "reference_report.json"
    with report_path.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write("\n")
    print("Report: " + str(report_path), flush=True)
    return 0 if all_passed else 1


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--video", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--wsl-distro", default="Ubuntu-24.04")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--probe", help="Native executable or absolute Linux path on Windows")
    group.add_argument("--probe-command", nargs=argparse.REMAINDER,
                       help="Explicit launcher argv; put this option last")
    args = parser.parse_args()
    if args.probe_command is not None and not args.probe_command:
        parser.error("--probe-command requires at least an executable")
    return args


def main():
    try:
        return run(parse_args())
    except (OSError, ValueError, RuntimeError) as error:
        print("verify_video_reference: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
