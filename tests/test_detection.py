from __future__ import annotations

import json
import os
import unittest
from tempfile import NamedTemporaryFile
from unittest.mock import patch
from types import SimpleNamespace

import numpy as np
from google.api_core.exceptions import ResourceExhausted
from PIL import Image as PILImage

from rat_detector.detector import annotate_image
from rat_detector.gemini_client import (
    GeminiRateLimitError,
    GeminiRatDetector,
    parse_detections_from_json,
)
from rat_detector.models import BoundingBox, Detection


class ParseDetectionsTests(unittest.TestCase):
    def test_parse_valid_json(self) -> None:
        payload = json.dumps(
            {
                "detections": [
                    {
                        "label": "rat",
                        "confidence": 0.91,
                        "box": {"x_min": 0.1, "y_min": 0.2, "x_max": 0.3, "y_max": 0.4},
                    },
                    {
                        "label": "cat",
                        "confidence": 0.88,
                        "box": {"x_min": 0.5, "y_min": 0.5, "x_max": 0.7, "y_max": 0.8},
                    },
                ]
            }
        )
        detections = parse_detections_from_json(payload)
        self.assertEqual(len(detections), 2)
        self.assertTrue(detections[0].is_rat())
        self.assertFalse(detections[1].is_rat())
        self.assertAlmostEqual(detections[0].box.x_min, 0.1)

    def test_rejects_invalid_json(self) -> None:
        with self.assertRaises(ValueError):
            parse_detections_from_json("not-json")


class AnnotateImageTests(unittest.TestCase):
    def test_annotations_modify_image(self) -> None:
        image = np.zeros((200, 200, 3), dtype=np.uint8)
        detection = Detection(
            label="rat",
            confidence=0.95,
            box=BoundingBox(x_min=0.1, y_min=0.1, x_max=0.4, y_max=0.4),
        )

        annotated, count = annotate_image(image, [detection])

        self.assertEqual(count, 1)
        self.assertIsNot(annotated, image)
        self.assertGreater(int(annotated.sum()), 0)
        # Ensure original image remains unchanged (still zeros)
        self.assertEqual(int(image.sum()), 0)


class GeminiRateLimitTests(unittest.TestCase):
    @patch("rat_detector.gemini_client.time.sleep")
    @patch("rat_detector.gemini_client.genai.configure")
    @patch("rat_detector.gemini_client.genai.GenerativeModel")
    def test_rate_limit_exhaustion_raises_friendly_error(
        self,
        mock_model_cls,
        mock_configure,
        mock_sleep,
    ) -> None:
        mock_model = mock_model_cls.return_value
        mock_model.generate_content.side_effect = ResourceExhausted("quota")

        with NamedTemporaryFile(suffix=".png", delete=False) as temp_image:
            PILImage.new("RGB", (10, 10), color="white").save(temp_image.name)

        try:
            detector = GeminiRatDetector(
                api_key="fake-key",
                max_retries=2,
                backoff_seconds=0,
            )

            with self.assertRaises(GeminiRateLimitError):
                detector.detect(temp_image.name)

            self.assertEqual(mock_model.generate_content.call_count, 2)
        finally:
            os.unlink(temp_image.name)


class GeminiResponseHandlingTests(unittest.TestCase):
    @patch("rat_detector.gemini_client.genai.configure")
    @patch("rat_detector.gemini_client.genai.GenerativeModel")
    def test_non_json_response_raises_runtime_error(
        self,
        mock_model_cls,
        mock_configure,
    ) -> None:
        mock_model_cls.return_value  # not used but ensures constructor succeeds

        with NamedTemporaryFile(suffix=".png", delete=False) as temp_image:
            PILImage.new("RGB", (10, 10), color="white").save(temp_image.name)

        try:
            detector = GeminiRatDetector(api_key="fake-key")
            with patch.object(
                GeminiRatDetector,
                "_generate_with_retry",
                return_value=SimpleNamespace(text="not-json"),
            ):
                with self.assertRaises(RuntimeError) as ctx:
                    detector.detect(temp_image.name)

            message = str(ctx.exception).lower()
            self.assertIn("response", message)
            self.assertIn("json", message)
        finally:
            os.unlink(temp_image.name)

    def test_python_style_response_is_parsed(self) -> None:
        payload = "{'detections': [{'label': 'rat', 'confidence': 0.9, 'box': {'x_min': 0.1, 'y_min': 0.2, 'x_max': 0.3, 'y_max': 0.4}}]}"
        detections = parse_detections_from_json(payload)
        self.assertEqual(len(detections), 1)
        self.assertTrue(detections[0].is_rat())

    def test_python_style_with_extra_closers_is_parsed(self) -> None:
        payload = "{'detections': [{'label': 'rat', 'confidence': 0.9, 'box': {'x_min': 0.5, 'y_min': 0.5, 'x_max': 0.8, 'y_max': 0.9}}]}]}"
        detections = parse_detections_from_json(payload)
        self.assertEqual(len(detections), 1)
        self.assertAlmostEqual(detections[0].box.x_min, 0.5)


if __name__ == "__main__":  # pragma: no cover
    unittest.main()
