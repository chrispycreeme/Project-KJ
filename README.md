# Rat Detector with Google Gemini and OpenCV

Detect rodents in still images by combining Google Gemini's multimodal understanding with OpenCV's image annotation utilities.

## Features

- Friendly Tkinter desktop app that guides you through choosing an image, entering your Gemini API key, and running detection.
- Uses Google Gemini's `gemini-2.5-flash` vision model to spot rats.
- Draws bounding boxes and confidence labels around detections with OpenCV, then shows a preview of the annotated image.
- Displays detection summaries in-app and saves an annotated copy of the image.
- Includes unit tests with mocked responses so you can validate the pipeline without calling the API.

## Prerequisites

- Python 3.10 or newer (64-bit recommended).
- A Google AI Studio API key with access to the Gemini vision models.
- Windows users should run commands from **PowerShell** (`powershell.exe`).

## Setup

1. (Optional) Create and activate a virtual environment:

   ```powershell
   py -3 -m venv .venv
   .\.venv\Scripts\Activate.ps1
   ```

2. Install dependencies:

   ```powershell
   pip install -r requirements.txt
   ```

3. Configure your Gemini API key:

   ```powershell
   copy .env.example .env
   notepad .env  # replace the placeholder with your real GEMINI_API_KEY
   ```

   > Alternatively, set `GEMINI_API_KEY` directly in your shell environment. The GUI
   > pre-fills the API key field from this environment variable but never stores the key on disk.

## Usage

Launch the GUI:

```powershell
py main.py
```

### Using the app

1. Enter your Gemini API key (or leave the field populated if the key was loaded from the environment).
2. Choose the image you want to analyze. Optionally pick a custom output location.
3. Adjust the minimum confidence threshold or Gemini model name if necessary.
4. Click **Run detection**. The app locks the button while Gemini processes the image, then displays the rat count, detection details, and enables the **Open annotated preview** button. Click it to view the annotated image in a separate window and open the containing folder. The annotated file is saved next to your input unless you specified another path.

> **Note:** Tkinter is bundled with the standard Windows/macOS Python installers. If you are using a minimal distribution without Tk support, install a build that includes Tkinter to run the GUI.

## Testing

Unit tests use mocked Gemini responses to keep them offline-friendly:

```powershell
py -m unittest
```

## Troubleshooting

- If you see `GEMINI_API_KEY environment variable is not set`, ensure your `.env` file exists or the variable is exported in your shell.
- A response parsing error usually means Gemini returned non-JSON text. Re-run the program; adding more context to the prompt often helps steady responses.
- Large images may take longer to upload; consider resizing if requests time out.
- `429 Quota exceeded` errors indicate you've hit the Gemini rate limit. The app now retries a few
   times with exponential backoff, then shows a friendly message advising you to wait or request more
   quota.

## License

Provided as-is for educational purposes. Review Google AI Studio terms before using the Gemini API in production.
