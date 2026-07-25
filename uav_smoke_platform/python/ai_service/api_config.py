"""Environment-backed configuration for the smoke-analysis API."""

import os
from dataclasses import dataclass
from pathlib import Path


DEFAULT_MAX_UPLOAD_SIZE_BYTES = 512 * 1024 * 1024


def _resolve_path(value: str, product_root: Path) -> Path:
    path = Path(value).expanduser()
    if not path.is_absolute():
        path = product_root / path
    return path.resolve()


@dataclass(frozen=True)
class ApiSettings:
    product_root: Path
    model_path: Path
    results_root: Path
    device: str = "0"
    confidence: float = 0.25
    max_upload_size_bytes: int = DEFAULT_MAX_UPLOAD_SIZE_BYTES

    @classmethod
    def from_environment(cls) -> "ApiSettings":
        product_root = Path(__file__).resolve().parents[2]
        confidence = float(os.getenv("SMOKE_CONFIDENCE", "0.25"))
        if not 0.0 <= confidence <= 1.0:
            raise ValueError("SMOKE_CONFIDENCE must be between 0 and 1")

        max_upload_size = int(
            os.getenv(
                "SMOKE_MAX_UPLOAD_SIZE_BYTES",
                str(DEFAULT_MAX_UPLOAD_SIZE_BYTES),
            )
        )
        if max_upload_size <= 0:
            raise ValueError("SMOKE_MAX_UPLOAD_SIZE_BYTES must be positive")

        return cls(
            product_root=product_root,
            model_path=_resolve_path(
                os.getenv("SMOKE_MODEL_PATH", "models/yolov8s.pt"),
                product_root,
            ),
            results_root=_resolve_path(
                os.getenv("SMOKE_API_RESULTS_ROOT", "results/api_tasks"),
                product_root,
            ),
            device=os.getenv("SMOKE_DEVICE", "0"),
            confidence=confidence,
            max_upload_size_bytes=max_upload_size,
        )
