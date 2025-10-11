from __future__ import annotations

import ast
import json
import os
import time
from dataclasses import asdict
from pathlib import Path
from typing import Any, Dict, Iterable, List, Sequence

import google.generativeai as genai
from google.api_core.exceptions import GoogleAPIError, ResourceExhausted
from google.generativeai.types import GenerationConfig
from PIL import Image

from .models import BoundingBox, Detection

DEFAULT_MODEL = "gemini-2.5-flash-lite"
_PROMPT = (
    "You are an expert pest-control inspector. Given an image, identify every rat you see. "
    "Respond with strict JSON shaped as {\"detections\": [{\"label\": \"rat\", \"confidence\": 0.97, "
    "\"box\": {\"x_min\": 0.1, \"y_min\": 0.2, \"x_max\": 0.3, \"y_max\": 0.4}}]}. "
    "Include only actual rats. Use normalized coordinates in range [0, 1]. "
    "Do not add Markdown, explanations, or additional keys."
)


class GeminiRateLimitError(RuntimeError):
    """Raised when Gemini API quota is exhausted even after retries."""


class GeminiRatDetector:
    """Thin wrapper around the Gemini API for rat detection."""

    def __init__(
        self,
        api_key: str | None = None,
        *,
        model: str = DEFAULT_MODEL,
        max_output_tokens: int = 1024,
        temperature: float = 0.0,
        max_retries: int = 3,
        backoff_seconds: float = 2.0,
    ) -> None:
        self.api_key = api_key or os.getenv("GEMINI_API_KEY")
        if not self.api_key:
            raise RuntimeError("GEMINI_API_KEY environment variable is not set.")

        self.model_name = model
        self.max_output_tokens = max_output_tokens
        self.temperature = temperature
        self.max_retries = max(1, int(max_retries))
        self.backoff_seconds = max(0.0, float(backoff_seconds))

        genai.configure(api_key=self.api_key)
        self._model = genai.GenerativeModel(model)

    def detect(self, image_path: str | os.PathLike[str]) -> List[Detection]:
        path = Path(image_path)
        if not path.exists():
            raise FileNotFoundError(f"Image not found: {path}")

        with Image.open(path) as img:
            image = img.convert("RGB")

        generation_config = GenerationConfig(
            response_mime_type="application/json",
            max_output_tokens=self.max_output_tokens,
            temperature=self.temperature,
        )

        response = self._generate_with_retry(
            [_PROMPT, image],
            generation_config,
        )

        json_payload = _extract_text(response)
        try:
            detections = parse_detections_from_json(json_payload)
        except ValueError as exc:
            snippet = _summarize_payload(json_payload)
            message = (
                "Gemini returned a response that wasn't valid JSON. "
                "Please retry in a few seconds or reduce the temperature.\n"
                f"Response excerpt: {snippet}"
            )
            raise RuntimeError(message) from exc
        return detections

    def _generate_with_retry(
        self,
        prompt_parts: Sequence[Any],
        generation_config: GenerationConfig,
    ) -> Any:
        last_error: Exception | None = None
        for attempt in range(1, self.max_retries + 1):
            try:
                return self._model.generate_content(
                    prompt_parts,
                    generation_config=generation_config,
                )
            except ResourceExhausted as exc:
                last_error = exc
                if attempt == self.max_retries:
                    message = (
                        "Gemini API quota has been exhausted. Wait a minute or request a "
                        "higher quota before trying again."
                    )
                    raise GeminiRateLimitError(message) from exc
                delay = self.backoff_seconds * attempt
                if delay > 0:
                    time.sleep(delay)
            except GoogleAPIError as exc:
                raise RuntimeError("Gemini API request failed") from exc

        if last_error is not None:
            raise GeminiRateLimitError("Gemini API quota exhausted") from last_error
        raise RuntimeError("Gemini request failed for an unknown reason")


def _extract_text(response: Any) -> str:
    """Extract the textual/JSON portion from a Gemini response."""
    if response is None:
        raise RuntimeError("Empty response from Gemini API.")

    text = getattr(response, "text", None)
    if text:
        return text

    # Some responses nest the text under candidates/parts.
    candidates: Sequence[Any] = getattr(response, "candidates", ())
    for candidate in candidates:
        content = getattr(candidate, "content", None)
        if not content:
            continue
        parts: Iterable[Any] = getattr(content, "parts", ())
        for part in parts:
            part_text = getattr(part, "text", None)
            if part_text:
                return part_text

    raise RuntimeError("Unable to extract JSON payload from Gemini response.")


def _summarize_payload(payload: Any, limit: int = 320) -> str:
    text = str(payload).strip()
    if len(text) <= limit:
        return text or "<empty>"
    return text[:limit] + "…"


def _normalize_structured_text(text: str) -> str:
    if not text:
        return text

    normalized = text
    for opener, closer in (("{", "}"), ("[", "]")):
        normalized = _rebalance_delimiters(normalized, opener, closer)
    return normalized


def _rebalance_delimiters(text: str, opener: str, closer: str) -> str:
    result = text.rstrip()

    open_count = result.count(opener)
    close_count = result.count(closer)

    while close_count > open_count and result.endswith(closer):
        result = result[:-1].rstrip()
        close_count -= 1

    open_count = result.count(opener)
    close_count = result.count(closer)
    if open_count > close_count:
        result = result + (closer * (open_count - close_count))

    return result


def parse_detections_from_json(payload: str | Dict[str, Any]) -> List[Detection]:
    """Parse Gemini JSON payload into Detection objects."""
    if isinstance(payload, str):
        cleaned = payload.strip().strip("`\n")
        if cleaned.startswith("json"):
            cleaned = cleaned[4:].lstrip()
        cleaned = _normalize_structured_text(cleaned)
        try:
            data = json.loads(cleaned)
        except json.JSONDecodeError as exc:
            try:
                data = ast.literal_eval(cleaned)
            except (ValueError, SyntaxError) as alt_exc:
                raise ValueError("Gemini response was not valid JSON") from exc
    else:
        data = payload

    detections_raw = data.get("detections", []) if isinstance(data, dict) else []
    result: List[Detection] = []
    for entry in detections_raw:
        try:
            label = str(entry.get("label", "")).strip() or "unknown"
            confidence = float(entry.get("confidence", 0.0))
            box_data = entry.get("box") or {}
            box = BoundingBox(
                x_min=float(box_data.get("x_min", 0.0)),
                y_min=float(box_data.get("y_min", 0.0)),
                x_max=float(box_data.get("x_max", 0.0)),
                y_max=float(box_data.get("y_max", 0.0)),
            ).clamp()
            result.append(Detection(label=label, confidence=confidence, box=box))
        except (TypeError, ValueError) as exc:
            raise ValueError(f"Malformed detection entry: {entry}") from exc

    return result


def detection_to_dict(detection: Detection) -> Dict[str, Any]:
    """Utility helper primarily used in unit tests."""
    raw = asdict(detection)
    raw["box"] = asdict(detection.box)
    return raw
