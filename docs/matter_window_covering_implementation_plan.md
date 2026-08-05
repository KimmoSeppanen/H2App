# Matter Window Covering Integration Plan

## 1. Objective

Integrate ESP-Matter into this firmware while preserving all existing non-Matter behavior, expose the device as a Matter Window Covering endpoint (blinds) where servo position controls the blind position, and report battery status to Matter.

## 2. Confirmed Product Decisions

1. Position mapping (phase 1):
   - `0%` = fully closed
   - `100%` = fully open
2. Movement behavior:
   - Immediate jump to target position (no stepped motion in initial implementation)
3. Calibration persistence and boot behavior:
   - Calibration data must be stored in NVS
   - If no calibration data exists at boot, run calibration automatically
4. Later enhancement:
   - Three quick button presses will toggle/invert open-closed direction mapping

## 3. Non-Negotiable Requirements

1. Existing features must continue to work:
   - UART command interface
   - Button short/long press actions
   - Servo calibration routine
   - LED state behavior
2. Matter functionality is additive and must not break current control paths.
3. All motion requests (UART/button/Matter) must use a unified motion path.

## 4. Integration Scope

### In Scope (initial Matter release)

1. ESP-Matter runtime integration in this project
2. One Matter Window Covering endpoint
3. Matter-driven servo movement using existing calibrated limits
4. NVS-backed calibration load/save
5. Auto-calibration on first boot (or when calibration data is missing)
6. Bidirectional state sync:
   - Local moves reflected to Matter current position
   - Matter target drives local servo
7. Battery status reporting to Matter using existing battery measurement path

### Out of Scope (later release)

1. Direction inversion by triple quick button press
2. Smooth/stepped movement and intermediate progress reporting
3. Extended optional Window Covering features beyond required/needed subset

## 5. Architecture Changes

## 5.1 New Modules

1. Matter bridge module:
   - Create node and Window Covering endpoint
   - Register attribute callback
   - Translate Matter attributes <-> local percent model
2. Calibration persistence module:
   - Save/load calibrated min/max angles and any required metadata
   - Validity marker/versioning for NVS data
3. Motion arbitration module:
   - Single queue/handler for move requests from all sources
   - Calibration lockout rules
4. Battery reporting module:
   - Convert local battery voltage reading to Matter-facing battery state values
   - Update Matter attributes on schedule and on meaningful voltage change

## 5.2 Existing Modules To Extend

1. Servo control path:
   - Add APIs for reading current logical position
   - Keep command-level clamping and safety behavior
2. Button task:
   - Preserve existing behavior
   - Reserve extension point for future triple-press inversion logic
3. Startup flow:
   - Load calibration from NVS
   - If unavailable, auto-run calibration before normal operation
   - Initialize Matter after local control stack is stable

## 6. Data Model and State Mapping

1. Canonical local position model:
   - Percent `0..100` where `0=closed`, `100=open`
2. Servo mapping:
   - Convert percent to angle using persisted/calculated safe range
3. Matter mapping:
   - Matter target position -> local percent -> immediate servo jump
   - Local resulting position -> Matter current position attribute update
4. Commissioned state behavior:
   - Attribute updates only from accepted motion path
   - Reject/defer commands when calibration active
5. Battery mapping:
   - Use local battery voltage source (`read_battery_voltage()` path) as canonical input
   - Map voltage into Matter battery status attributes (for example, remaining percent and low-battery indication)
   - Publish initial value at startup and periodic updates during runtime

### 6.1 Proposed Battery Conversion Policy (Initial)

Use LiPo-oriented piecewise mapping with clamping:

1. `voltage >= 4.20V` -> `100%`
2. `4.10V` -> `90%`
3. `4.00V` -> `80%`
4. `3.90V` -> `65%`
5. `3.80V` -> `50%`
6. `3.70V` -> `35%`
7. `3.60V` -> `20%`
8. `3.50V` -> `10%`
9. `3.40V` -> `5%`
10. `voltage <= 3.30V` -> `0%`

Interpolation rule:

1. Linearly interpolate between adjacent voltage points.
2. Clamp below `3.30V` to `0%` and above `4.20V` to `100%`.

Low-battery status policy (initial):

1. Low-battery warning ON when `voltage <= 3.50V`.
2. Low-battery warning OFF only when `voltage >= 3.60V` (hysteresis to avoid flapping).

Reporting cadence policy (initial):

1. Publish at startup.
2. Publish every 30 seconds.
3. Publish immediately if computed battery percent changes by at least 2 percentage points.

## 7. NVS Strategy for Calibration

1. Namespace: dedicated calibration namespace (for example `servo_cal`)
2. Stored keys (planned):
   - `valid` marker
   - `min_angle`
   - `max_angle`
   - optional `schema_ver`
3. Boot flow:
   - Try load -> validate bounds
   - If valid: use loaded range
   - If invalid/missing: run auto calibration, then persist
4. Recalibration flow:
   - Manual calibration command overwrites persisted values on success

## 8. Execution Phases

## Phase A: Baseline Matter plumbing

1. Add ESP-Matter dependencies and build integration
2. Add Matter bootstrap files and app event callbacks
3. Verify project builds and boots with Matter runtime enabled

Exit criteria:
1. Firmware boots and prints Matter startup info
2. Existing local features still work unchanged

## Phase B: Window Covering endpoint

1. Add Window Covering endpoint creation
2. Register attribute PRE_UPDATE callback for target position handling
3. Connect callback to existing servo move path (immediate jump)

Exit criteria:
1. Device commissions and appears as Window Covering/Blinds
2. Matter target position updates move servo correctly

## Phase C: Unified motion path and sync

1. Route UART/button/Matter moves through single internal API
2. Update Matter current position after any local motion
3. Ensure calibration-active lockout/serialization works

Exit criteria:
1. No motion path divergence
2. Matter state remains consistent after local actions

## Phase D: Battery reporting to Matter

1. Add battery status endpoint/cluster wiring required by chosen ESP-Matter data model path
2. Implement voltage-to-Matter conversion policy (including low-battery threshold)
3. Add periodic reporting task/timer and on-change update trigger
4. Ensure battery status updates are safe during calibration and normal operation

Exit criteria:
1. Matter controller can read current battery status attributes
2. Battery values update over time and reflect measured voltage changes

## Phase E: Calibration persistence and auto-calibration

1. Implement NVS save/load for calibration data
2. Run auto calibration if NVS data missing or invalid
3. Persist successful calibration results

Exit criteria:
1. Reboot retains calibration values
2. First boot (no saved data) auto-calibrates correctly

## Phase F: Stabilization and docs

1. Add logs for commissioning and endpoint updates
2. Update user and technical docs
3. Update changelog/version constants

Exit criteria:
1. End-to-end test pass
2. Documentation and release notes complete

## 9. Test Plan

1. Local regression:
   - UART commands and button interactions unchanged
2. Matter commissioning:
   - Commission using a controller and verify endpoint type is Window Covering
3. Functional control:
   - Send position commands: 0, 25, 50, 75, 100
   - Verify immediate servo movement and correct current-position reporting
4. Battery reporting:
   - Read battery attributes from Matter controller and verify they match measured voltage trend
   - Validate periodic updates and low-battery threshold behavior
5. Calibration persistence:
   - Erase NVS -> confirm auto calibration runs
   - Reboot -> confirm calibration is loaded and auto-calibration does not rerun
6. Concurrency safety:
   - Attempt Matter moves during calibration; ensure expected lockout behavior

## 10. Risks and Mitigations

1. Risk: Motion conflicts between local and Matter commands
   - Mitigation: single motion dispatcher + calibration lock state
2. Risk: Startup delay due to auto-calibration on fresh device
   - Mitigation: clear startup logs and explicit LED state indications during calibration
3. Risk: Attribute mapping mismatch
   - Mitigation: explicit mapping helpers and integration tests for boundary values
4. Risk: Incorrect battery-to-percent conversion creates misleading status
   - Mitigation: define and document conversion thresholds; validate with controlled voltage points

## 11. Versioning Plan

1. Matter integration kickoff release target: `0.6.0`
2. Keep in sync on each implementation step:
   - `APP_VERSION` in `main/main.c`
   - latest three startup changelog lines in `main/main.c`
   - top entry in `CHANGELOG.md`

## 12. Future Phase (post-initial integration)

1. Implement triple quick press to invert direction mapping:
   - Normal: `0=closed`, `100=open`
   - Inverted: `0=open`, `100=closed`
2. Persist inversion setting in NVS
3. Reflect inversion consistently across UART, button cycle behavior, and Matter mapping
