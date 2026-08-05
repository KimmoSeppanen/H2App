# Application Documentation: h2_app

## 1. Purpose

`h2_app` is an ESP32-H2 firmware application that combines:
- Servo motion control using LEDC PWM
- Servo position/response feedback sampling through ADC
- Battery voltage measurement through ADC and external divider
- Interactive runtime control over UART commands
- On-device calibration of servo safe operating limits
- BOOT button short-press and long-press interaction model

The implementation is in `main/main.c`.

## 2. Build and Runtime Environment

- Framework: ESP-IDF
- Language: C
- Main entry: `app_main()`
- RTOS: FreeRTOS (task for button handling + foreground command loop)

### Required Components

Declared in `main/CMakeLists.txt`:
- `driver`
- `esp_adc`

### Project CMake Root

Root `CMakeLists.txt` declares the project using standard ESP-IDF boilerplate.

## 3. Hardware Interface

### GPIO and ADC Mapping

- BOOT button: `GPIO9` (input, pull-up enabled, active low)
- Servo PWM output: `GPIO3`
- Battery ADC channel: `ADC1 CH1` (GPIO2)
- Servo feedback ADC channel: `ADC1 CH3` (GPIO4)

### ADC Configuration

- ADC unit: `ADC_UNIT_1`
- Attenuation: `ADC_ATTEN_DB_12` for both battery and servo feedback channels
- Bit width: `ADC_BITWIDTH_DEFAULT`
- Calibration: curve fitting scheme on ESP32-H2 when available

### Electrical Conversion

Battery voltage conversion assumes:
- ADC pin measures divided voltage
- Voltage divider ratio: `5.7`

So:
- `battery_voltage = pin_voltage * 5.7`

## 4. Software Architecture

The code in `main/main.c` is organized into five major sections:

1. Hardware pins and ADC setup
2. Servo PWM setup and movement helpers
3. Step-based calibration and backoff routine
4. Debounced BOOT button task
5. Command processor and main loop

### Runtime Concurrency Model

- Task 1: `button_task`
  - Monitors BOOT button with debounce and hold detection
  - Dispatches short-press servo cycle or long-press calibration
- Task 2: `app_main` loop
  - Reads UART characters with `getchar()`
  - Builds command lines
  - Executes parsed commands

No mutexes are currently used; operations are simple and serialized enough for current behavior.

## 5. Servo Control Design

### PWM Parameters

- Frequency: `50 Hz` (20 ms period)
- Resolution: `14-bit`
- Pulse width range: `500 us` to `2500 us`
- Logical max range: `0..270 degrees`

### Angle to PWM Mapping

For angle `a`:
- pulse width is linearly interpolated between 500 and 2500 us over 0..270 deg
- duty is scaled from pulse width into 14-bit LEDC duty counts over 20 ms period

### Safe Clamp Limits

Commands are clamped to:
- `calib_min_angle`
- `calib_max_angle`

Defaults at boot:
- min = `0`
- max = `270`

Calibration can tighten this range.

## 6. ADC Reading and Feedback Sampling

### Battery Reading

`read_battery_voltage()`:
1. Reads raw sample from battery ADC channel
2. Uses ADC calibration if available, otherwise raw linear approximation
3. Converts pin mV to battery V using divider ratio

### Servo Feedback Reading

`read_servo_feedback()`:
1. Waits 100 ms for analog supply/feedback settling
2. Collects 32 samples with 100 us spacing
3. Averages raw samples
4. Converts to mV using calibration if available

Oversampling smooths noise and improves stall/progress decisions during calibration.

## 7. Calibration Algorithm

`calibrate_servo()` estimates safe mechanical limits by detecting reduced motion progress in feedback voltage.

### Tunable Parameters

- `Calibration_Sensitivity` (1..100), default 50
- `Calibration_Step_Size` (1..45 deg), default 5
- `Calibration_Backoff_Degrees` (0..45 deg), default 20

### Threshold Logic

A baseline phase collects 5 small steps and computes average per-step feedback change magnitude (`ref_delta`).
A stop threshold is then derived from sensitivity:
- Higher sensitivity value -> lower threshold
- Lower threshold means earlier stall detection

The threshold is clamped to at least 1 mV-equivalent step progress.

### Direction Polarity Handling

Feedback voltage may increase or decrease with angle depending on wiring/sensor orientation.
The routine computes effective progress direction (`dir_max` or `dir_min`) so comparisons remain valid independent of polarity.

### Scan Strategy

1. Move to center (135 deg)
2. Baseline sample in increasing-angle direction
3. Scan toward max angle in `StepSize` increments
4. If progress falls below threshold:
   - mark candidate stall
   - verify candidate with 3 retries (step away, then return)
   - if still stalled, confirm hard limit
   - apply backoff margin to define safe max
5. Return to center
6. Repeat equivalent process toward min angle
7. Apply safe min and safe max
8. Return to center and print final range

### Why Retry Verification Exists

Single-step anomalies can occur due to noise, compliance, load shifts, or temporary friction.
The retry loop reduces false positives before locking in safe limits.

## 8. BOOT Button Interaction

`button_task()` behavior:

- Debounce on press: 50 ms
- Debounce on release: 50 ms
- Long-press threshold: 5000 ms

### Short Press

Cycles servo target percentages through sequence:
- 0%
- 50%
- 100%
- 50%
- then repeats

### Long Press

When hold time reaches 5 seconds:
- triggers `calibrate_servo()` once
- release after long press does not execute short-press action

## 9. UART Command Interface

Commands are parsed case-insensitively in `process_command()`.

### Supported Commands

- `Battery`
- `Calibrate`
- `Sensitivity [1-100]`
- `StepSize [1-45]`
- `Backoff [0-45]`
- `Servo <nnn>`
- `Servo <nnn%>`

### Parsing Notes

- Enter accepted on CR or LF
- Backspace and DEL are handled
- Command line buffer is 128 bytes
- Unknown commands print help summary

### Validation Rules

- `Sensitivity`: must be 1..100
- `StepSize`: must be 1..45
- `Backoff`: must be 0..45
- `Servo` percentage is clamped to 0..100
- Servo degrees are clamped to calibrated safe range when applied

## 10. Startup Sequence

At boot (`app_main()`):

1. Initialize ADC subsystem
2. Initialize servo PWM subsystem
3. Set servo to 135 deg
4. Create button monitor task
5. Print command help banner
6. Enter infinite UART command loop

## 11. Timing Characteristics

Key delays in firmware logic:

- Servo settle after movement command: 600 ms
- Servo feedback pre-read settle: 100 ms
- Oversample spacing: 100 us
- Calibration step dwell: 350 ms
- Centering pauses during calibration: 1000 ms
- Button polling period: 20 ms

These values balance responsiveness with analog stability and calibration reliability.

## 12. Known Limitations and Risks

- Calibration depends on feedback sensor quality and monotonicity.
- No persistent storage of calibrated limits; limits reset after reboot.
- Servo calibration and command handling can overlap conceptually (two contexts), though current logic is straightforward and generally safe for this use case.
- No explicit watchdog/timeout around calibration phases.

## 13. Suggested Enhancements

- Store calibration results in NVS and restore on boot
- Add command to print current calibrated range
- Add command to dump raw ADC streams for diagnostics
- Add optional moving-average filter for runtime servo feedback
- Add explicit state machine to block overlapping motion/calibration actions
- Add unit-level tests for parser and threshold calculation logic (host-side)

## 14. Troubleshooting

### Servo does not move

- Confirm servo power and ground are correct and shared with board ground.
- Verify PWM line is connected to GPIO3.
- Check command syntax with `Servo 90` or `Servo 50%`.

### Calibration detects limits too early

- Increase `Sensitivity` value gradually and retry calibration.
- Decrease `StepSize` for finer detection.
- Ensure feedback line on GPIO4 is stable and not noisy.

### Calibration misses hard stop

- Decrease `Sensitivity` value.
- Increase `StepSize` slightly if changes are too small to detect.

### Battery voltage seems wrong

- Verify external resistor divider ratio matches firmware value `5.7`.
- Confirm battery input is connected to GPIO2 / ADC1 CH1.

## 15. Reference Files

- `main/main.c`: full application implementation
- `main/CMakeLists.txt`: component registration and dependencies
- `CMakeLists.txt`: top-level ESP-IDF project declaration
- `README.md`: quickstart and operator-facing usage
