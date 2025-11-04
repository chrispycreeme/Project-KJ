# Project KJ Dashboard

is a Flutter dashboard for supervising and tuning an IoT rat-trap installation. It surfaces real-time telemetry from Firestore, lets operators toggle actuators remotely, and keeps an auditable action log for regulatory or maintenance reviews.

## Features

- Live dashboard cards that stream trap statistics (detections, completed cycles).
- Remote controls for arming the trap, feeding bait, and triggering CO₂ release.
- Editable safety threshold for automatic CO₂ activation with optimistic UI updates.
- Persistent action log with timestamps and payload details for each control change.
- Light and dark themes with a one-tap toggle, tailored for control-room displays.
- In-app alert banners and device notifications when new rats are detected or the CO₂ release engages.

## Tech Stack

- Flutter with Material 3 widgets.
- Firebase Authentication is not required; Firebase Core initializes from platform configs.
- Cloud Firestore for state storage and log streaming.
- Provider for dependency injection and lightweight state management.

## Prerequisites

- Flutter SDK 3.22 or newer (project targets Dart SDK `^3.8.0`).
- An existing Firebase project with Firestore enabled.
- Platform configuration files generated via Firebase (see below).

## Getting Started

### Install Dependencies

- Run `flutter pub get` after cloning or pulling new changes.

### Seed Firestore

- Create a document at `trap_controller/primary` or let the app bootstrap it on first run.
- Expected document shape:

  ```json
  {
    "rat_detected_count": 0,
    "cycle_completed": 0,
    "activate_trap": false,
    "open_feed_dispenser": false,
    "co2_release": false,
    "cycles_needed_for_co2_release": 1,
    "updated_at": <server timestamp>
  }
  ```

### Configure Notifications

- **Android 13+**: Grant the notification permission when prompted. The manifest declares `POST_NOTIFICATIONS`, but emulators may require you to toggle the permission manually under App Info ▸ Notifications.
- **iOS / iPadOS**: Accept the notification permission dialog on first launch. If dismissed, enable notifications from Settings ▸ Project KJ Dashboard.
- **Desktop**: macOS prompts during first launch; Windows/Linux rely on the operating system for toast delivery.

## Firestore Layout

- `trap_controller/primary`: singleton document holding live trap state.
- `trap_controller/primary/logs`: sub-collection capturing recent actions (`label`, `payload`, server `timestamp`).

## Troubleshooting

- **App fails to start Firebase**: Confirm platform config files are present and bundle IDs match your Firebase project.
- **No live data**: Ensure Firestore security rules permit access from your environment and that the IoT device updates the document.
- **Controls do not persist**: Check network connectivity and Cloud Firestore quotas; updates use `SetOptions(merge: true)` on the singleton document.

## License

This project is currently private. Coordinate with the project owner before sharing or publishing.
