# 🐭 Rat Detector

A comprehensive AI-powered rat detection system combining desktop application, computer vision, and IoT capabilities. Uses Google Gemini vision models to detect rats in images with real-time notifications via ESP32 BLE integration.

## 📋 Table of Contents

- [Features](#features)
- [Project Structure](#project-structure)
- [Requirements](#requirements)
- [Installation](#installation)
- [Usage](#usage)
- [Configuration](#configuration)
- [Development](#development)

## ✨ Features

- [x] **Friendly Tkinter desktop app** — guided UI for selecting an image, entering a Gemini API key (pre-filled from the environment), running detection, and opening an annotated preview.
- [x] **Google Gemini vision integration** — uses configurable Gemini vision models (default: `gemini-2.5-flash`) to locate rats in still images. Model name and minimum confidence are adjustable in the UI.
- [x] **OpenCV annotations** — draws bounding boxes, confidence labels, and optional notes onto the image; saves an annotated copy and shows a preview window.
- [x] **Detection summaries** — in-app counts, per-detection confidence and bounding-box coordinates, and a saved annotated image next to the input (or at a custom output path).
- [x] **Robust request handling** — retries with exponential backoff on transient errors and shows friendly, actionable messages for rate limits and timeouts.
- [x] **ESP32 BLE integration** — two Arduino sketches included for forwarding alerts over BLE (see `esp32_stuff/`):

  <details>
  <summary>ESP32 sketches (expand)</summary>

  - `MainSystem.ino` — BLE GATT server that advertises as `MainSystem` and prints received JSON alert payloads to Serial.
  - `camera/CameraWebServer.ino` — Camera sketch that captures images, and attempts a BLE write to `MainSystem` with a JSON alert like `{"rat_detected":true, "confidence":0.92}` when detections occur.

  **Notes:**
  - BLE writes are best-effort; range and coexistence with WiFi can affect reliability.
  - UUIDs and other constants are defined in the sketches; change them if you need custom behaviour.
  </details>

- [x] **Camera quality defaults** — the camera sketch automatically uses higher frame size and JPEG fidelity when PSRAM is available (UXGA when present, VGA fallback otherwise). Warmup frames are included to stabilize the sensor.
- [x] **Privacy-minded design** — the GUI pre-fills the API key from the environment and does not store the API key to disk by default.
- [x] **Unit tests with mocks** — comprehensive test coverage for detection and API integration.

## 📁 Project Structure

```
Project KJ/
├── rat_detector/              # Main Python package
│   ├── gui_app.py            # Tkinter desktop application
│   ├── detector.py           # Core detection logic
│   ├── gemini_client.py       # API client
│   ├── models.py             # Data models and types
│   └── __init__.py
├── esp32_stuff/              # Arduino/ESP32 sketches
│   ├── MainSystem/           # BLE GATT server
│   └── camera/               # Camera and detection client
├── project_kj/               # Flutter mobile app (optional UI)
├── tests/                    # Unit tests
├── rat_images/               # Sample images for testing
├── requirements.txt          # Python dependencies
├── main.py                   # Entry point
└── README.md
```

## 🚀 Installation

### Desktop Application

1. Clone the repository:
   ```bash
   git clone https://github.com/chrispycreeme/Project-KJ.git
   cd "Project KJ"
   ```

2. Create a virtual environment (recommended):
   ```bash
   python -m venv venv
   venv\Scripts\activate  # Windows
   # or: source venv/bin/activate  # Linux/macOS
   ```

3. Install dependencies:
   ```bash
   pip install -r requirements.txt
   ```

4. Set up your Google Gemini API key:
   ```bash
   # Windows PowerShell:
   $env:GEMINI_API_KEY="your-api-key-here"
   
   # Or set permanently in environment variables
   ```

### ESP32 Setup (Optional)

1. Install [Arduino IDE](https://www.arduino.cc/en/software) or use PlatformIO
2. Add ESP32 board support to Arduino IDE
3. Open and upload sketches:
   - `esp32_stuff/MainSystem/MainSystem.ino` — to the main receiver ESP32
   - `esp32_stuff/camera/CameraWebServer/CameraWebServer.ino` — to the camera ESP32

## 💻 Usage

### Desktop Application

Run the GUI application:
```bash
python main.py
```

**Steps:**
1. Click "Select Image" to choose an image file
2. Enter or use pre-filled Gemini API key
3. (Optional) Adjust model name and confidence threshold
4. Click "Run Detection" to analyze
5. View results and annotated preview
6. Save the annotated image to your desired location

### Command Line

```python
from rat_detector.detector import RatDetector

detector = RatDetector(api_key="your-api-key")
results = detector.detect("path/to/image.jpg")
print(f"Found {len(results)} rats")
for detection in results:
    print(f"  Confidence: {detection.confidence}")
```

### ESP32 Integration

Once both sketches are uploaded:
1. MainSystem ESP32 will advertise its BLE service
2. Camera ESP32 will capture and analyze images
3. Detections trigger BLE alerts to MainSystem
4. Alerts appear on Serial Monitor with JSON payloads

## ⚙️ Configuration

### Environment Variables

- `GEMINI_API_KEY` — Your Google Gemini API key (required for detection)

### GUI Settings

Within the desktop app, you can adjust:
- **Model**: Gemini model to use (default: `gemini-2.5-flash`)
- **Confidence Threshold**: Minimum detection confidence (0.0 - 1.0)
- **Output Path**: Where to save annotated images (default: same directory as input)

## 🧪 Development

### Running Tests

```bash
python -m pytest tests/ -v
```

### Test Coverage

```bash
python -m pytest tests/ --cov=rat_detector
```

### Code Structure

- **`detector.py`** — Core detection logic and image processing
- **`gemini_client.py`** — Handles API communication and retries
- **`gui_app.py`** — Tkinter UI components
- **`models.py`** — Data classes and type definitions

## 📝 License

[Add your license here]

## 🤝 Contributing

Contributions welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Add tests for new features
4. Submit a pull request

## 📞 Support

For issues, questions, or suggestions, please open an issue on GitHub.
