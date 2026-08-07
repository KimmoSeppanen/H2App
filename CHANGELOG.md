# Changelog

All notable changes to this project are documented in this file.

This project follows Semantic Versioning (`MAJOR.MINOR.PATCH`).

## [0.7.0] - 2026-08-07

### Added

- Added dedicated servo power-enable control pin on GPIO10 (active LOW).
- Added servo power gating sequence: enable power, wait 50 ms for rail stabilization, then allow servo movement.

### Changed

- Updated servo movement paths to automatically disable servo power after movement settles.
- Calibration flow now keeps servo power enabled for the full calibration routine and disables it when complete.

## How To Update

When making changes in future:

1. Increase `APP_VERSION` in `main/main.c`.
2. Update `CHANGELOG_LATEST_1..3` in `main/main.c`.
3. Add a new section at the top of this file.
4. Summarize Added, Changed, and Fixed items.
5. Keep entries short and operator-focused.

## [0.6.0] - 2026-08-05

### Added

- Added Phase A Matter bridge module and startup initialization path.
- Added serial boot status line showing Matter runtime init result.

### Changed

- Main component now builds mixed C/C++ sources and links ESP-Matter dependencies.
- Versioned startup release highlights updated for Phase A Matter integration.

## [0.5.0] - 2026-08-05

### Added

- Startup serial banner now prints the three latest changelog entries.

### Changed

- Firmware constants now track the latest three release highlights shown at boot.

## [0.4.0] - 2026-08-05

### Changed

- Corrected Olimex board LED handling to match hardware: single user LED on GPIO8, active LOW.
- Replaced RGB/WS2812-style control path with direct GPIO LED control for reliable behavior.
- Updated button/LED behavior logic to target:
  - Idle: LED off
  - Button press: LED on
  - Calibration active: LED flashing

### Added

- Added startup button-level debug print for easier input diagnostics.
- Added firmware version output in serial boot banner.

## [0.3.0] - 2026-08-05

### Added

- Added button/calibration status LED state machine in firmware.
- Added LED status task and state transitions for press and calibration events.

### Changed

- Updated documentation to include Olimex target board notes and LED behavior requirements.

## [0.2.0] - 2026-08-05

### Added

- Replaced template README with project-specific usage documentation.
- Added comprehensive technical documentation in `docs/application_documentation.md`.

## [0.1.0] - 2026-08-05

### Added

- Initial ESP32-H2 interactive servo controller application.
- UART command interface (`Battery`, `Servo`, `Calibrate`, `Sensitivity`, `StepSize`, `Backoff`).
- Servo feedback ADC reading, battery sensing, and calibration flow.
- BOOT button short-press and long-press interaction handling.
