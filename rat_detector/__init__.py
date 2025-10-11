"""Rat detection package using Google Gemini and OpenCV."""

from .models import BoundingBox, Detection
from .gemini_client import (
    GeminiRateLimitError,
    GeminiRatDetector,
    parse_detections_from_json,
)
from .gui_app import RatDetectorApp, is_gui_available
from .detector import detect_rats

__all__ = [
    "BoundingBox",
    "Detection",
    "GeminiRatDetector",
    "detect_rats",
    "parse_detections_from_json",
    "GeminiRateLimitError",
    "RatDetectorApp",
    "is_gui_available",
]
