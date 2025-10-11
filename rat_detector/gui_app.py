from __future__ import annotations

import os
import subprocess
import sys
import threading
from contextlib import suppress
from pathlib import Path
from typing import Callable, Iterable, Optional

from dotenv import load_dotenv

from .detector import detect_rats
from .gemini_client import DEFAULT_MODEL, GeminiRateLimitError

with suppress(ImportError):
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    from PIL import Image, ImageTk


def is_gui_available() -> bool:
    return "tk" in globals()


class RatDetectorApp:
    """Tkinter-based GUI for running rat detection."""

    def __init__(self) -> None:
        if not is_gui_available():
            raise RuntimeError(
                "Tkinter is not available in this Python environment. "
                "Install a Python build with Tk support to use the GUI."
            )

        load_dotenv()

        self._root = tk.Tk()  # type: ignore[name-defined]
        self._root.title("Rat Detector (Gemini + OpenCV)")
        self._root.geometry("900x640")
        self._root.minsize(820, 540)

        self.api_key_var = tk.StringVar(value=os.getenv("GEMINI_API_KEY", ""))  # type: ignore[name-defined]
        self.image_path_var = tk.StringVar()  # type: ignore[name-defined]
        self.output_path_var = tk.StringVar()  # type: ignore[name-defined]
        self.model_var = tk.StringVar(value=DEFAULT_MODEL)  # type: ignore[name-defined]
        self.min_confidence_var = tk.DoubleVar(value=0.2)  # type: ignore[name-defined]
        self.status_var = tk.StringVar(value="Ready")  # type: ignore[name-defined]
        self.result_var = tk.StringVar(value="Run detection to enable preview.")  # type: ignore[name-defined]

        self._annotated_image: Optional[ImageTk.PhotoImage] = None  # type: ignore[name-defined]
        self._preview_window: Optional[tk.Toplevel] = None  # type: ignore[name-defined]
        self._preview_label: Optional[ttk.Label] = None  # type: ignore[name-defined]
        self._detect_thread: Optional[threading.Thread] = None
        self._last_annotated_path: Optional[Path] = None

        self._build_layout()

    # ------------------------------------------------------------------
    # Layout
    # ------------------------------------------------------------------
    def _build_layout(self) -> None:
        root = self._root

        main_frame = ttk.Frame(root, padding=12)  # type: ignore[name-defined]
        main_frame.pack(fill=tk.BOTH, expand=True)  # type: ignore[name-defined]

        # Configuration fields
        config_frame = ttk.LabelFrame(main_frame, text="Configuration", padding=12)  # type: ignore[name-defined]
        config_frame.pack(fill=tk.X)

        self._add_labeled_entry(
            config_frame,
            label="Gemini API key",
            textvariable=self.api_key_var,
            row=0,
            show="*",
            help_text="The key is never stored; it stays in memory for this session only.",
        )

        self._add_file_picker(
            config_frame,
            label="Image file",
            textvariable=self.image_path_var,
            command=self._pick_image,
            row=1,
        )

        self._add_file_picker(
            config_frame,
            label="Annotated output",
            textvariable=self.output_path_var,
            command=self._pick_output_path,
            row=2,
            button_label="Browse",
        )

        self._add_labeled_entry(
            config_frame,
            label="Gemini model",
            textvariable=self.model_var,
            row=3,
        )

        # Confidence slider
        ttk.Label(config_frame, text="Min confidence").grid(row=4, column=0, sticky=tk.W, pady=4)  # type: ignore[name-defined]
        slider = ttk.Scale(
            config_frame,
            variable=self.min_confidence_var,
            from_=0.0,
            to=1.0,
            orient=tk.HORIZONTAL,
            command=lambda _value: self._update_confidence_label(),
        )
        slider.grid(row=4, column=1, sticky=tk.EW, padx=(0, 8), pady=4)
        config_frame.columnconfigure(1, weight=1)

        self._confidence_label = ttk.Label(  # type: ignore[name-defined]
            config_frame,
            text=self._format_confidence(self.min_confidence_var.get()),
        )
        self._confidence_label.grid(row=4, column=2, sticky=tk.W)

        # Actions
        action_frame = ttk.Frame(main_frame, padding=(0, 12, 0, 0))  # type: ignore[name-defined]
        action_frame.pack(fill=tk.X)

        self._run_button = ttk.Button(action_frame, text="Run detection", command=self._on_run_clicked)  # type: ignore[name-defined]
        self._run_button.pack(side=tk.LEFT)

        ttk.Button(action_frame, text="Quit", command=self._root.destroy).pack(side=tk.RIGHT)  # type: ignore[name-defined]

        ttk.Label(action_frame, textvariable=self.status_var).pack(side=tk.RIGHT, padx=16)

        # Results
        result_frame = ttk.LabelFrame(main_frame, text="Results", padding=12)  # type: ignore[name-defined]
        result_frame.pack(fill=tk.BOTH, expand=True)

        self._result_text = tk.Text(result_frame, height=10, wrap=tk.WORD, state=tk.DISABLED)  # type: ignore[name-defined]
        self._result_text.pack(fill=tk.BOTH, expand=True)

        preview_frame = ttk.LabelFrame(main_frame, text="Annotated preview", padding=12)  # type: ignore[name-defined]
        preview_frame.pack(fill=tk.X, pady=(12, 0))

        ttk.Button(preview_frame, text="Open annotated preview", command=self._open_preview_window).pack(
            side=tk.LEFT
        )  # type: ignore[name-defined]
        ttk.Label(preview_frame, textvariable=self.result_var).pack(side=tk.LEFT, padx=12)  # type: ignore[name-defined]

    def _add_labeled_entry(
        self,
        parent: "tk.Widget",
        *,
        label: str,
        textvariable: "tk.StringVar",
        row: int,
        show: str | None = None,
        help_text: str | None = None,
    ) -> None:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky=tk.W, pady=4)  # type: ignore[name-defined]
        entry = ttk.Entry(parent, textvariable=textvariable, show=show)  # type: ignore[name-defined]
        entry.grid(row=row, column=1, sticky=tk.EW, padx=(0, 8), pady=4)
        parent.columnconfigure(1, weight=1)
        if help_text:
            ttk.Label(parent, text=help_text, foreground="#555").grid(row=row, column=2, sticky=tk.W)

    def _add_file_picker(
        self,
        parent: "tk.Widget",
        *,
        label: str,
        textvariable: "tk.StringVar",
    command: Callable[[], None],
        row: int,
        button_label: str = "Browse…",
    ) -> None:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky=tk.W, pady=4)  # type: ignore[name-defined]
        entry = ttk.Entry(parent, textvariable=textvariable)  # type: ignore[name-defined]
        entry.grid(row=row, column=1, sticky=tk.EW, padx=(0, 8), pady=4)
        ttk.Button(parent, text=button_label, command=command).grid(row=row, column=2, sticky=tk.E)  # type: ignore[name-defined]

    # ------------------------------------------------------------------
    # Event handlers
    # ------------------------------------------------------------------
    def _pick_image(self) -> None:
        file_path = filedialog.askopenfilename(  # type: ignore[name-defined]
            title="Select image",
            filetypes=[
                ("Image files", "*.png *.jpg *.jpeg *.bmp *.gif"),
                ("All files", "*.*"),
            ],
        )
        if file_path:
            self.image_path_var.set(file_path)

    def _pick_output_path(self) -> None:
        output_path = filedialog.asksaveasfilename(  # type: ignore[name-defined]
            title="Select output file",
            defaultextension=".png",
            filetypes=[("PNG", "*.png"), ("JPEG", "*.jpg;*.jpeg"), ("All files", "*.*")],
        )
        if output_path:
            self.output_path_var.set(output_path)

    def _on_run_clicked(self) -> None:
        if self._detect_thread and self._detect_thread.is_alive():
            return

        image_path = self.image_path_var.get().strip()
        api_key = self.api_key_var.get().strip()
        output_path = self.output_path_var.get().strip()
        model = self.model_var.get().strip() or DEFAULT_MODEL
        min_confidence = float(self.min_confidence_var.get())

        if not image_path:
            messagebox.showwarning("Missing image", "Please choose an image file to analyze.")  # type: ignore[name-defined]
            return
        if not api_key:
            messagebox.showwarning("Missing API key", "Please enter your Gemini API key.")  # type: ignore[name-defined]
            return

        self._set_running_state(True, status="Analyzing image…")

        def task() -> None:
            try:
                count, annotated_path, detections = detect_rats(
                    image_path,
                    output_path=output_path or None,
                    model=model,
                    min_confidence=min_confidence,
                    api_key=api_key,
                )
            except GeminiRateLimitError as exc:  # pragma: no cover - GUI only
                message = str(exc)
                self._root.after(0, lambda msg=message: self._handle_error(msg))
                return
            except Exception as exc:  # pragma: no cover - GUI only
                message = str(exc)
                self._root.after(0, lambda msg=message: self._handle_error(msg))
                return

            self._root.after(0, lambda: self._handle_success(count, annotated_path, detections))

        self._detect_thread = threading.Thread(target=task, daemon=True)
        self._detect_thread.start()

    # ------------------------------------------------------------------
    # UI helpers
    # ------------------------------------------------------------------
    def _set_running_state(self, running: bool, *, status: str | None = None) -> None:
        self._run_button.configure(state=tk.DISABLED if running else tk.NORMAL)  # type: ignore[name-defined]
        if status:
            self.status_var.set(status)
        if running:
            self.result_var.set("Analyzing…")

    def _handle_success(self, count: int, annotated_path: Path, detections: Iterable) -> None:
        self._set_running_state(False, status=f"Done. Rats detected: {count}")
        self._update_results_text(count, annotated_path, detections)
        self._update_preview(annotated_path)
        self._last_annotated_path = annotated_path
        self.result_var.set(f"Preview ready • {annotated_path.name}")
        self.output_path_var.set(str(annotated_path))
        messagebox.showinfo(
            "Rat detection complete",
            f"Rats detected: {count}\nAnnotated image saved to:\n{annotated_path.resolve()}",
        )  # type: ignore[name-defined]

    def _handle_error(self, message: str) -> None:
        self._set_running_state(False, status="Error")
        self.result_var.set("Detection failed. See message.")
        messagebox.showerror("Rat detection failed", message)  # type: ignore[name-defined]

    def _update_results_text(self, count: int, annotated_path: Path, detections: Iterable) -> None:
        lines = [
            f"Rats detected: {count}",
            f"Annotated image: {annotated_path.resolve()}",
            "",
        ]
        for idx, detection in enumerate(detections, start=1):
            box = detection.box
            lines.append(
                f"{idx}. {detection.label} — conf={detection.confidence:.2f} "
                f"bbox=({box.x_min:.2f}, {box.y_min:.2f}, {box.x_max:.2f}, {box.y_max:.2f})"
            )
        if len(lines) == 3:
            lines.append("No detections above threshold.")

        self._result_text.configure(state=tk.NORMAL)
        self._result_text.delete("1.0", tk.END)
        self._result_text.insert(tk.END, "\n".join(lines))
        self._result_text.configure(state=tk.DISABLED)

    def _update_preview(self, annotated_path: Path) -> None:
        try:
            image = Image.open(annotated_path)
        except OSError:
            self._annotated_image = None
            self.status_var.set("Annotated image unavailable")
            return

        max_width, max_height = 900, 600
        image.thumbnail((max_width, max_height), Image.LANCZOS)
        self._annotated_image = ImageTk.PhotoImage(image)
        image.close()

        if self._preview_window and self._preview_window.winfo_exists() and self._preview_label:
            self._preview_label.configure(image=self._annotated_image)
        else:
            self._preview_window = None
            self._preview_label = None

    def _open_preview_window(self) -> None:
        if not self._annotated_image:
            messagebox.showinfo("No preview", "Run detection to generate an annotated preview first.")  # type: ignore[name-defined]
            return

        if self._preview_window and self._preview_window.winfo_exists():
            self._preview_window.lift()
            return

        window = tk.Toplevel(self._root)  # type: ignore[name-defined]
        window.title("Annotated preview")
        window.geometry("720x520")
        window.minsize(360, 240)
        window.transient(self._root)
        window.grab_set()
        window.protocol("WM_DELETE_WINDOW", self._close_preview_window)

        frame = ttk.Frame(window, padding=12)  # type: ignore[name-defined]
        frame.pack(fill=tk.BOTH, expand=True)

        label = ttk.Label(frame, image=self._annotated_image)  # type: ignore[name-defined]
        label.pack(fill=tk.BOTH, expand=True)

        button_bar = ttk.Frame(window, padding=(12, 0, 12, 12))  # type: ignore[name-defined]
        button_bar.pack(fill=tk.X)

        ttk.Button(button_bar, text="Open file location", command=self._open_output_directory).pack(side=tk.LEFT)  # type: ignore[name-defined]
        ttk.Button(button_bar, text="Close", command=self._close_preview_window).pack(side=tk.RIGHT)  # type: ignore[name-defined]

        self._preview_window = window
        self._preview_label = label

    def _close_preview_window(self) -> None:
        if self._preview_window and self._preview_window.winfo_exists():
            self._preview_window.destroy()
        self._preview_window = None
        self._preview_label = None

    def _open_output_directory(self) -> None:
        if not self._last_annotated_path:
            messagebox.showinfo("No file", "Run detection to create an annotated file first.")  # type: ignore[name-defined]
            return

        path = self._last_annotated_path
        if not path.exists():
            messagebox.showinfo("Missing file", "Annotated image could not be found on disk.")  # type: ignore[name-defined]
            return

        folder = path.parent.resolve()
        try:
            if hasattr(os, "startfile"):
                os.startfile(folder)  # type: ignore[attr-defined]
            elif sys.platform == "darwin":
                subprocess.run(["open", str(folder)], check=False)
            else:
                subprocess.run(["xdg-open", str(folder)], check=False)
        except OSError:
            messagebox.showinfo("Open failed", f"Could not open folder {folder}")  # type: ignore[name-defined]

    def _update_confidence_label(self) -> None:
        self._confidence_label.configure(
            text=self._format_confidence(self.min_confidence_var.get())
        )

    @staticmethod
    def _format_confidence(value: float) -> str:
        return f"{value:.2f}"

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------
    def run(self) -> None:
        self._root.mainloop()


__all__ = ["RatDetectorApp", "is_gui_available"]
