# Rat Detector — Features

This file lists the project's interactive features and quick notes. Interactive elements (collapsible sections and the task list) make it easy to scan the capabilities on GitHub.

## Features

- [x] Friendly Tkinter desktop app — guided UI for selecting an image, entering a Gemini API key (pre-filled from the environment), running detection, and opening an annotated preview.
- [x] Google Gemini vision integration — uses configurable Gemini vision models (default: `gemini-2.5-flash`) to locate rats in still images. Model name and minimum confidence are adjustable in the UI.
- [x] OpenCV annotations — draws bounding boxes, confidence labels, and optional notes onto the image; saves an annotated copy and shows a preview window.
- [x] Detection summaries — in-app counts, per-detection confidence and bounding-box coordinates, and a saved annotated image next to the input (or at a custom output path).
- [x] Robust request handling — retries with exponential backoff on transient errors and shows friendly, actionable messages for rate limits and timeouts.
- [x] ESP32 BLE integration — two Arduino sketches included for forwarding alerts over BLE (see `esp32_stuff/`):

  <details>
  <summary>ESP32 sketches (expand)</summary>

  - `MainSystem.ino` — BLE GATT server that advertises as `MainSystem` and prints received JSON alert payloads to Serial.
  - `camera/CameraWebServer.ino` — Camera sketch that captures images, calls Gemini, and attempts a BLE write to `MainSystem` with a JSON alert like `{"rat_detected":true, "confidence":0.92}` when detections occur.

  Notes:

  - BLE writes are best-effort; range and coexistence with WiFi can affect reliability.
  - UUIDs and other constants are defined in the sketches; change them if you need custom behaviour.
  </details>

- [x] Camera quality defaults — the camera sketch automatically uses higher frame size and JPEG fidelity when PSRAM is available (UXGA when present, VGA fallback otherwise). Warmup frames are included to stabilize the sensor.

- [x] Privacy-minded design — the GUI pre-fills the API key from the environment and does not store the API key to disk by default.

## Quick interactive checklist

Use this task list to see supported capabilities at a glance (click to toggle on GitHub):

- [x] GUI app (Tkinter)
- [x] Gemini vision model support
- [x] Annotated previews (OpenCV)
- [x] Save annotated image
- [x] Unit tests with mocks
- [x] ESP32 BLE alerting
