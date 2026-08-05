# h2_app

ESP32-H2 firmware application for:
- Servo control through PWM (LEDC)
- Servo feedback reading through ADC
- Battery voltage measurement through ADC and voltage divider
- Runtime servo calibration with hard-limit detection
- BOOT button short-press and long-press actions
- UART console command interface

Main implementation is in [main/main.c](main/main.c).

## Features

- Moves servo by absolute angle: `Servo <degrees>`
- Moves servo by relative percentage: `Servo <percent>%`
- Reads battery voltage: `Battery`
- Runs calibration routine: `Calibrate`
- Tunes calibration behavior at runtime:
	- `Sensitivity [1-100]`
	- `StepSize [1-45]`
	- `Backoff [0-45]`
- BOOT button handling:
	- Short press cycles servo target: `0% -> 50% -> 100% -> 50% -> ...`
	- Long press (5 seconds) triggers calibration

## Hardware Mapping

- BOOT button input: `GPIO9`
- Servo PWM output: `GPIO3`
- Servo feedback ADC: `ADC1 CH3` (GPIO4)
- Battery ADC: `ADC1 CH1` (GPIO2)

Current ADC attenuation is `12 dB` for both channels.

## Build Prerequisites

- ESP-IDF installed and exported (matching your toolchain)
- USB/UART connection to ESP32-H2 board
- Target configured as `esp32h2`

If target is not set yet:

```bash
idf.py set-target esp32h2
```

## Build, Flash, Monitor

From project root:

```bash
idf.py build
idf.py -p <PORT> flash
idf.py -p <PORT> monitor
```

Combined flash + monitor:

```bash
idf.py -p <PORT> flash monitor
```

Exit monitor with `Ctrl+]`.

## Console Commands

- `Battery`
	- Prints measured battery voltage in volts.
- `Servo 120`
	- Moves to absolute angle in degrees.
- `Servo 75%`
	- Moves to a percentage of currently calibrated safe range.
- `Calibrate`
	- Runs step-based scan to detect usable min/max servo limits.
- `Sensitivity`
	- Without value: prints current sensitivity.
	- With value: sets detection sensitivity between 1 and 100.
- `StepSize`
	- Without value: prints step size in degrees.
	- With value: sets step size between 1 and 45 degrees.
- `Backoff`
	- Without value: prints backoff margin in degrees.
	- With value: sets backoff between 0 and 45 degrees.

## Application Behavior Summary

- On startup:
	- ADC and PWM are initialized.
	- Servo is set to center (135 degrees).
	- BOOT button task is started.
	- Command banner is printed to UART.
- Servo target commands are clamped to current safe limits.
- Calibration scans both high and low directions, confirms candidate stalls with retries, then applies a configurable safety backoff.

## Project Structure

```text
.
|- CMakeLists.txt
|- sdkconfig
|- main/
|  |- CMakeLists.txt
|  `- main.c
`- README.md
```

## More Documentation

For detailed architecture and algorithm-level documentation, see:

- [docs/application_documentation.md](docs/application_documentation.md)
