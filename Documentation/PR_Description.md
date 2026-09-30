# Add interactive DBW actuator tests via Serial Monitor

## Summary

Adds opt-in serial diagnostics for the DBW Arduino Due, following the direct
actuator-testing approach of the existing steering and speed test() routines.
Operators can select individual tests without normal CAN, RC arbitration, PID
updates, or logging overriding their requests.

This branch, `feature/dbw-diagnostics-review`, replaces the earlier
`feature/dbw-serial-test-mode` implementation. The older branch is retained as a
backup, not an additional change to merge.

## Minimal production impact

- Diagnostics are disabled by default: `DBW_SERIAL_TEST_MODE=0`.
- SpeedController.cpp and SteeringController.cpp exactly match the upstream
  baseline (2de90d5), including constructors, normal control, sensor conversion,
  and original automatic test() routines.
- Vehicle's original constructor and startup tests remain unchanged. Vehicle.cpp
  adds only four compile-time-guarded diagnostic dispatch/isolation checks.
- Existing production-file changes total 60 added lines across the original
  sketch, Vehicle.cpp, and three headers. All other feature files are new.
- The original no-argument constructors remain the default. Diagnostic overloads,
  state, and methods compile only when test mode is enabled.
- Initial brake engagement reuses Stop(); release reuses ReleaseBrakes(); angle
  conversion reuses computeAngleLeft() through a test-only accessor.
- Normal-mode brake timing and PID initialization are deliberately not changed.

## Organization

| File | Responsibility |
| --- | --- |
| FirmwareMode.h | Explicit build-mode selection; normal mode by default |
| VehicleTestMode.cpp | Test-only Vehicle constructor and forwarding methods |
| VehicleDiagnostics.h / VehicleDiagnostics.cpp | Command actions, limits, timers, software stop state, LEDs, raw RC capture |
| SpeedControllerDiagnostics.cpp | Test-only speed constructor, raw throttle command, brake timer |
| SteeringControllerDiagnostics.cpp | Test-only steering constructor, motor/servo commands, sensor access |
| SerialTestMode.h / SerialTestMode.cpp | Parse input and queue non-blocking serial output |
| tests/serial_test_mode/ | Mock-hardware C++ tests and PowerShell serial PASS/FAIL runner |

Flow: `loop()` -> `Vehicle::update()` -> `Vehicle::test()` ->
`VehicleDiagnostics::update()` -> controller methods.

Diagnostic startup commands steering PWM and throttle code to zero, applies
brakes, and latches software stop. It skips normal RC driving, CAN initialization,
and Logger; guarded Vehicle RC/CAN entry points cannot override diagnostics.
The existing normal startup test sequences remain available but are not invoked
by the interactive tests.

## Commands and limits

Use Programming USB, 115200 baud, and Newline/CRLF. Commands are case-insensitive.
Send one command per line and wait for its completion; S/T/P also accept numbers
on subsequent lines.

| Input | Behavior |
| --- | --- |
| L | Cycle off plus seven RGB combinations, one second each |
| B | Apply brakes using boost selection, then holding selection after 800 ms |
| R | Cancel previous motion and release brakes |
| V 5..255 | Store steering PWM for the next S command; no movement yet |
| S -1000..1000 | Timed motor-shield steering; sign selects configured direction; stop and report feedback |
| T 0..255 | Raw DAC0 throttle code; nonzero requires brakes released |
| E | Stop steering, zero throttle code, apply brakes, and latch software stop |
| C | Report six RC pulse widths; missing/stale channels report zero |
| P | Servo pulse test within configured limits, currently 1000..1850 us |

Deliberate differences from the email, for lab review: V stores rather than
immediately outputs PWM; S is capped at one second; nonzero throttle and servo
tests expire after five seconds; throttle is blocked while brakes are applied.
Timing is non-blocking so serial E and the configured physical-stop input can be
serviced. Repeated B/E do not restart an active brake boost.

A valid following letter command clears the software stop latch and cancels
previous motion, but does not automatically release brakes. Numbers alone cannot
clear the latch. D49 LOW inhibits commands. These software mechanisms do not
replace an independent physical power cutoff.

## Validation

- Host C++ tests passed using the real Vehicle, RC, speed, steering, and PID
  sources against mocked Arduino/Servo/CAN interfaces. Coverage includes startup,
  parser rejection, CAN/RC isolation, timing, stop handling, rollover, RC capture,
  sensor conversion, and selected normal-mode behavior.
- PowerShell runner regressions passed, including reconnects, missing/corrupted
  replies, timing failures, cleanup failures, and command-by-command evidence.
- Both Arduino Due builds passed: normal mode (default 0) and explicit diagnostics
  via `compiler.cpp.extra_flags=-DDBW_SERIAL_TEST_MODE=1`.
- The contributor uploaded the revised diagnostic firmware to a USB-only Due on
  COM5 and reported the successful run below on September 30, 2026.

Host tests do not measure electrical signals. The live runner verifies serial
responses and host receipt timing, not voltages, movement, or brake force. Its
live scenarios do not cover T/P/C or physical-stop wiring. The normal controller
fixture uses static storage and does not validate upstream's uninitialized
PIDThrottle across all allocation patterns. This is not certification of normal
driving or vehicle safety.

### Reported Due output

Command run by the contributor:

```powershell
.\tests\serial_test_mode\run-serial.ps1 -Port COM5 -BareDueConfirmed
```

This combines the two consecutive excerpts supplied for the same run. It is not
a new hardware run performed while preparing this PR.

<details>
<summary>Successful serial run: all nine commands</summary>

```text
Connected to COM5 at 115200. Verifying diagnostic firmware with E-stop; no RESET needed.
TEST 01 [Diagnostic handshake]: E -- wait for ESTOP acknowledgement
PASS COMMAND 01 [Diagnostic handshake]: E -- Exact ESTOP reply received: PWM=0, DAC0=0, brakes applied, numeric commands blocked.
PASS: Diagnostic handshake
  SATISFIED: E returned the exact required reply: ESTOP: PWM=0 DAC0=0; brakes applied; numeric commands blocked
TEST 02 [LED sequence]: L -- wait for LED cycle complete (about 8 seconds)
PASS COMMAND 02 [LED sequence]: L -- Masks 0..7 in order, LED cycle complete; host intervals 994.5, 999.5, 999.5, 1003.2, 999.4, 999.6, 999.4, 999.9 ms, all within 650..1500 ms.
TEST 03 [LED sequence]: E -- wait for ESTOP acknowledgement
PASS COMMAND 03 [LED sequence]: E -- Exact ESTOP reply received: PWM=0, DAC0=0, brakes applied, numeric commands blocked.
PASS: LED sequence
  SATISFIED: Reported LED masks: 0, 1, 2, 3, 4, 5, 6, 7, in the required order.
  SATISFIED: Received LED cycle complete after mask 7.
  SATISFIED: Host intervals: 994.5, 999.5, 999.5, 1003.2, 999.4, 999.6, 999.4, 999.9 ms; all 8 within 650..1500 ms.
  SATISFIED: Cleanup E returned the exact diagnostic E-stop acknowledgement.
TEST 04 [Brake sequence]: R -- wait for BRAKE OFF
PASS COMMAND 04 [Brake sequence]: R -- BRAKE OFF received: D44=1, D40=1; equal released relay levels.
TEST 05 [Brake sequence]: B -- wait for BRAKE HOLD (about 800 ms)
PASS COMMAND 05 [Brake sequence]: B -- BRAKE ON D44=0, D40=0 -> BRAKE HOLD D44=0, D40=1; expected relay transitions; 794.7 ms within 600..1200 ms (host timing).
TEST 06 [Brake sequence]: E -- wait for ESTOP acknowledgement
PASS COMMAND 06 [Brake sequence]: E -- Exact ESTOP reply received: PWM=0, DAC0=0, brakes applied, numeric commands blocked.
PASS: Brake sequence
  SATISFIED: Reported release: D44=1, D40=1; both at the same released level.
  SATISFIED: Reported boost: D44=0, D40=0; both inverted from release.
  SATISFIED: Reported hold: D44=0, D40=1; brake stayed applied, voltage select returned to release level.
  SATISFIED: Boost interval: 794.7 ms; allowed 600..1200 ms (host receipt timing).
  SATISFIED: Cleanup E returned the exact diagnostic E-stop acknowledgement.
TEST 07 [Timed steering]: V 100 -- wait for STEER PWM setting=100
PASS COMMAND 07 [Timed steering]: V 100 -- STEER PWM setting=100 received, matching the requested setting; firmware reports output remains zero until S.
TEST 08 [Timed steering]: S -200 -- wait for STEER STOP (about 200 ms)
PASS COMMAND 08 [Timed steering]: S -200 -- Reported ms=-200 and PWM=100; raw sensor 438 -> 244 within 0..1023; STEER STOP after 196.6 ms within 100..600 ms (host timing). Sensor accuracy NOT TESTED.
TEST 09 [Timed steering]: E -- wait for ESTOP acknowledgement
PASS COMMAND 09 [Timed steering]: E -- Exact ESTOP reply received: PWM=0, DAC0=0, brakes applied, numeric commands blocked.
PASS: Timed steering
  SATISFIED: Received STEER PWM setting=100 acknowledgement.
  SATISFIED: Received STEER ms=-200 PWM=100, matching the requested direction, duration, and drive setting.
  SATISFIED: Raw sensor reports: 438 -> 244; both within 0..1023. Accuracy NOT TESTED.
  SATISFIED: Received STEER STOP after 196.6 ms; allowed 100..600 ms (host receipt timing).
  SATISFIED: Cleanup E returned the exact diagnostic E-stop acknowledgement.
Serial behavior: PASS
Electrical outputs: NOT TESTED
Physical actuators: NOT TESTED
```

</details>

The raw change `438 -> 244` is from an unconnected input and is not evidence of
steering movement. Timing differences from 800/200 ms include USB/OS scheduling.
Reported relay values are not pin read-back measurements.

## Next steps with real sensors and actuators

These are pending lab checks, not completed tests. Run them with Tyler or another
qualified lab operator, one subsystem at a time. Do not run the bare-Due automated
suite or supply `-BareDueConfirmed` with actuators/drivers attached: the suite
releases brakes and commands steering. Use supervised manual commands instead.

1. **Inspect configuration and isolate power.** Verify board revision, connectors,
   pin mapping, relay inversion, grounds, supply ratings, and sensor conditioning.
   Secure the frame with no rider, raise/support the driven wheel, and clear
   steering and pinch areas. Verify an independent hardware cutoff before motion.
   Serial E and D49 are not independent power-disconnect mechanisms.

2. **Measure outputs before connecting actuators.** Check D51-D53 under L, D44/D40
   under R/B/E, and D3/D9/D12 under short S commands. Use a meter for DC levels and
   a scope/logic analyzer for PWM and timing. Verify startup/reset output behavior.
   D9's motor-shield brake/enable polarity must be confirmed, not inferred from
   ST_ON/ST_OFF names. The Due supplies relay logic, not 12/24 V directly.

3. **Validate steering feedback with actuator power disabled.** Power down before
   wiring the conditioned left sensor to A10; confirm the input stays within
   0..3.3 V. Due inputs are not 5 V tolerant. Use S 0 for a raw reading without
   motor movement. Vary position manually only if mechanically safe, and check
   repeatability, center, direction, and range. Verify local calibration before
   trusting degrees. A floating or failed sensor does not inhibit timed steering.

4. **Verify actual brake operation.** With propulsion disabled and the frame
   secured, run R then B manually. Measure the intended 24 V boost followed by
   12 V hold after about 800 ms at the solenoid. Verify cable travel, engagement,
   continued holding, release, current, and component temperature limits. Relay
   clicks alone are insufficient. Avoid rapid repeated release/reapply cycles;
   startup itself applies brakes.

5. **Test steering conservatively.** After direction and enable polarity checks,
   choose low usable PWM and short durations appropriate to the actuator with the
   lab team, away from travel limits. Try each sign separately and verify movement,
   sensor direction, stopping, and current draw. Do not treat V 100 / S -200 as an
   initial real-actuator recipe. The original test's ST_LEFT/ST_RIGHT convention
   conflicts with the normal motor-control direction boolean; this pre-existing
   issue is preserved, not resolved by the PR. There is no automatic end-stop,
   jam/current protection, target-angle control, or automatic return to center.

6. **Verify interruption and stop behavior.** On the secured fixture, confirm E
   and D49 interrupt short steering actions, remove drive demand, and apply brakes.
   D49 held LOW must reject following commands. Verify startup/reset states and
   do not assume cable loss is a verified stop. Any valid following letter clears
   the software latch; keep the independent cutoff available throughout.

7. **Test throttle last with the rear wheel raised.** First measure DAC0 and the
   controller-side signal with the motor/controller isolated. A Due DAC is not a
   direct 0..5 V source; code zero is not zero physical volts. Verify scaling,
   idle threshold, startup interlocks, and enable/brake wiring against the motor
   controller documentation. After independent stopping and brakes are verified,
   use R and small approved T values under supervision. Check wheel response,
   T 0, E, and the five-second timeout. Never inject 5 V into a Due input.

8. **Check RC and servo paths separately.** For C, verify receiver signal voltage
   compatibility, move one control at a time, and check all six channels plus
   missing/stale reports. For P, verify D48 goes to a servo-pulse input, not the
   alternative right-turn driver; measure pulse widths before attaching a servo.
   Confirm the actuator's safe pulse limits. Detaching pulses does not guarantee
   every servo stops or loses holding force; verify the actual device behavior.

Record command, configuration, measured electrical result, sensor reading,
physical response, and stop result separately. On any unexpected response, use
the independent cutoff and investigate before continuing. Before normal operation,
select mode 0, verify the actual vehicle configuration, upload deliberately, and
perform separate normal-driving regression checks. Serial PASS does not authorize
ground driving.

## Reproduce software checks

```powershell
.\tests\serial_test_mode\run.cmd
.\tests\serial_test_mode\test-serial-runner.ps1
arduino-cli compile --fqbn arduino:sam:arduino_due_x --libraries . .\Drive_By_Wire
arduino-cli compile --fqbn arduino:sam:arduino_due_x --libraries . --build-property "compiler.cpp.extra_flags=-DDBW_SERIAL_TEST_MODE=1" .\Drive_By_Wire
```

Local Settings.h is vehicle-specific and is not submitted. Configure it from
SettingsTemplate.h, retaining the correct calibration and steering definitions;
older copies may lack steering-method definitions. Install the Arduino SAM core,
the original firmware's libraries, and Servo. The host runner uses MSVC (defaults
to VS 2019 Build Tools or an existing developer environment).

For an Arduino IDE bench upload, temporarily enable mode 1 and select Arduino Due
(Programming Port) and the current COM port. Restore the source header to 0 before
submission; this source change does not alter the running board until another
upload. Close Serial Monitor before starting the PowerShell runner. The runner
does not upload firmware.

See [Serial_Test_Mode.md](Serial_Test_Mode.md) for manual replay, detailed criteria,
and optional JSON transcripts. Diagnostic constructor overloads depend on controller
state and may require maintenance if that state changes. The original static
controller-pointer/single-Vehicle assumptions and normal-mode PID/brake issues
remain outside this PR's scope.