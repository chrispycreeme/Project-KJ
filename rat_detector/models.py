from __future__ import annotations

from dataclasses import dataclass
from typing import Dict


@dataclass(slots=True)
class BoundingBox:
    """Normalized bounding box using relative coordinates in range [0, 1]."""

    x_min: float
    y_min: float
    x_max: float
    y_max: float

    def clamp(self) -> "BoundingBox":
        """Clamp all coordinates to the [0, 1] range and ensure x/y ordering."""
        x1, y1 = max(0.0, min(1.0, self.x_min)), max(0.0, min(1.0, self.y_min))
        x2, y2 = max(0.0, min(1.0, self.x_max)), max(0.0, min(1.0, self.y_max))
        x_min, x_max = sorted((x1, x2))
        y_min, y_max = sorted((y1, y2))
        return BoundingBox(x_min=x_min, y_min=y_min, x_max=x_max, y_max=y_max)

    def to_absolute(self, width: int, height: int) -> Dict[str, int]:
        """Scale normalized values to absolute pixel coordinates."""
        clamped = self.clamp()
        return {
            "x_min": int(round(clamped.x_min * width)),
            "y_min": int(round(clamped.y_min * height)),
            "x_max": int(round(clamped.x_max * width)),
            "y_max": int(round(clamped.y_max * height)),
        }


@dataclass(slots=True)
class Detection:
    label: str
    confidence: float
    box: BoundingBox

    def is_rat(self) -> bool:
        return "rat" in self.label.lower()
