from __future__ import annotations

import sys

from rat_detector.gui_app import RatDetectorApp, is_gui_available


def main() -> None:
    if not is_gui_available():
        message = (
            "Tkinter is not available in this Python environment.\n"
            "Install a Python build with GUI support (e.g. python.org installer) to run the app."
        )
        print(message, file=sys.stderr)
        raise SystemExit(1)

    app = RatDetectorApp()
    app.run()


if __name__ == "__main__":  # pragma: no cover
    main()
