from __future__ import annotations

from pathlib import Path
from typing import Iterable, List, Optional, Tuple

import cv2
import numpy as np

from .gemini_client import DEFAULT_MODEL, GeminiRatDetector
from .models import Detection

_BOX_COLOR = (0, 255, 0)
_TEXT_COLOR = (0, 0, 0)
_TEXT_BG_COLOR = (0, 255, 0)
_FONT = cv2.FONT_HERSHEY_SIMPLEX


def detect_rats(
    image_path: str | Path,
    *,
    output_path: str | Path | None = None,
    model: str = DEFAULT_MODEL,
    min_confidence: float = 0.2,
    api_key: str | None = None,
) -> Tuple[int, Path, List[Detection]]:
    """Run Gemini detection, annotate the image, and return rat count and output path."""
    detector = GeminiRatDetector(model=model, api_key=api_key)
    detections = detector.detect(image_path)

    image = cv2.imread(str(image_path))
    if image is None:
        raise ValueError(f"Failed to read image: {image_path}")

    rat_detections = [d for d in detections if d.is_rat() and d.confidence >= min_confidence]
    annotated, count = annotate_image(image, rat_detections)

    output = Path(output_path) if output_path else _default_output_path(image_path)
    cv2.imwrite(str(output), annotated)

    return count, output, rat_detections


def annotate_image(image: np.ndarray, detections: Iterable[Detection]) -> Tuple[np.ndarray, int]:
    """Draw bounding boxes and labels for detections on the provided image."""
    annotated = image.copy()
    height, width = annotated.shape[:2]
    count = 0

    for detection in detections:
        box_pixels = detection.box.to_absolute(width, height)
        x1, y1 = box_pixels["x_min"], box_pixels["y_min"]
        x2, y2 = box_pixels["x_max"], box_pixels["y_max"]

        cv2.rectangle(annotated, (x1, y1), (x2, y2), _BOX_COLOR, 2)

        label = f"{detection.label} {detection.confidence:.2f}"
        (text_width, text_height), baseline = cv2.getTextSize(label, _FONT, 0.6, 1)
        cv2.rectangle(
            annotated,
            (x1, max(0, y1 - text_height - baseline - 4)),
            (x1 + text_width + 6, y1),
            _TEXT_BG_COLOR,
            cv2.FILLED,
        )
        cv2.putText(
            annotated,
            label,
            (x1 + 3, max(text_height + baseline, y1 - 3)),
            _FONT,
            0.6,
            _TEXT_COLOR,
            1,
            cv2.LINE_AA,
        )
        count += 1

    _draw_count_badge(annotated, count)
    return annotated, count


def _draw_count_badge(image: np.ndarray, count: int) -> None:
    message = f"Rats detected: {count}"
    (text_width, text_height), baseline = cv2.getTextSize(message, _FONT, 0.8, 2)
    padding = 12
    x1, y1 = padding, padding
    x2, y2 = x1 + text_width + padding * 2, y1 + text_height + baseline + padding

    cv2.rectangle(image, (x1, y1), (x2, y2), _TEXT_BG_COLOR, cv2.FILLED)
    cv2.putText(
        image,
        message,
        (x1 + padding, y2 - padding - baseline // 2),
        _FONT,
        0.8,
        _TEXT_COLOR,
        2,
        cv2.LINE_AA,
    )


def _default_output_path(image_path: str | Path) -> Path:
    path = Path(image_path)
    return path.with_name(f"{path.stem}_annotated{path.suffix}")
