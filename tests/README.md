# Gamepad report regression tests

## 2026-10-14 r5 auxiliary input and independent safety clock

Current r5 checks: decoder 5766, arbiter 93562, adapter 1002 and USB class
4886 (105216 assertions), plus 267 actual-consumer integration assertions:
105483 total, including exhaustive loops. Keil and Flash/PID readback passed;
the twelve-step vehicle procedure is in progress and is not yet accepted.

The measured profile4 auxiliary IN83 endpoint is serviced using independent
storage, with its payload discarded. Tests cover exact metadata, failed resource
allocation/cleanup, DONE isolation from gamepad output and leases, retry after
NAK/ERROR and disabling the auxiliary service on STALL until re-enumeration.
No auxiliary interface control requests are added. The physical same-connection
enable/pause/resume experiment is separate evidence, documented in the
[r5 record](../docs/releases/2026-10-14-gamepad-r5.md).

The safety guard runs before and after the USB host process using raw cycle
differences. Periodic profiles 2/3/4 expire independently at 500 ms even if USB
SOF is frozen; invalid connection/port/class/gamepad identity clears publication
and arming. Legacy/profile1 receives no new periodic timeout assumption. The
integration runner extracts the actual type gate, USB process wrapper, event
callback and gamepad_scan, and links the real decoder, arbiter and adapter:

```sh
python3 tests/gamepad_safety_integration_test.py --cc gcc \
  --output-dir build/tests/gamepad-safety
```

It covers expiry before host work, port failure or elapsed time inside host
work, a frozen SOF clock, raw-cycle wrap, exact timeout boundaries, stale-held
recovery and foreign/keyboard/mouse counterexamples. On Windows, --cc accepts
the existing Zig executable. Hardware stubs do not prove RF disconnect behavior,
physical wheel stopping or execution while the control ISR is blocked. Each
periodic channel is refreshed only by its own valid gamepad reports.

The r4 section below preserves the earlier result and initial hardware failure.

## 2026-10-13 r4 historical software checks

The r4 nine-byte profile is selected for 20BC:5500 / bcd1003 with endpoint
capacity 64 and report-descriptor length 123. It accepts exactly 9 USB bytes,
with no Windows zero-ID prefix; physical right X/Y are bytes 5/6. Literal
fixtures come from one calibrated controller, A/MI_01: eight stick directions,
six mapped keys (Usages 7 through 12), two extra unmapped keys and hat up/right.
All 15 raw button bits remain part of the neutral/ownership decision.

The historical r4 logs contain decoder 5766, arbiter 93562, adapter 852 and class 3052
assertions (103232 total, including exhaustive loops). Both interfaces feed
the existing arbiter independently and use per-channel 500 ms validity. Tests
cover strict metadata/length selection, measured mappings, dual-pipe routing
and retained eight-byte/0810/legacy/old-profile decoder branches. The old
nine-byte receiver has matching 64/123 metadata and would now select profile4;
retaining its old decoder branch is not proof of unchanged physical behavior.

These tests and the Keil build passed. Flash/PID readback also passed, but r4
on the vehicle accepted only the initial neutral report on each interface and
then stopped receiving new reports. The class transport stubs do not emulate
the STM32 USB controller. Host assertions cannot establish working continuous
USB reception, radio power-off handling or motion on the vehicle. Five receivers
have matching descriptors; only one controller completed physical calibration.
See the [device evidence](../docs/devices/20bc-5500-new9-20261013.md) and
[r4 stage record](../docs/releases/2026-10-13-new9-gamepad.md).

## Test commands and earlier profile history

Commands below remain applicable. The r2/r3 implementation notes and historical
counts describe their respective stages; current profile4 results are above.

The r3 dual-handset extension uses `gamepad_arbiter.c`: either controller can
own the output after neutral, the owner releases on all axes/hat/raw buttons
neutral, and a 10 ms zero interval precedes a new gesture from either channel.
Held non-owner inputs are not queued; each channel owns its 500 ms validity.
Vehicle interfaces 0/1 have independent pipes; PC report IDs 1/2 are demultiplexed.
The r3 old nine-byte 20BC and legacy paths were single-channel. R3 checks: decoder3657,
arbiter93562, adapter408, class1291 (98918 assertions, including boundary loops).
Earlier paragraphs below preserve r2's selection history where applicable.

The 2026-10-08 0810:0001 profile uses an 8-byte USB report including ID1.
ID2 shares the endpoint and is ignored without clearing or refreshing ID1.
Recorded physical left-forward/back, digital right-left/right and shoulder/system
samples are literal decoder fixtures. The user accepted that the right stick and
corresponding face keys produce identical digital turns; opposing directions
cancel, and a native right axis outside deadzone 10 takes priority.
At the initial r2 PC stage, this profile alone had a 500 ms valid-ID1 timeout.
Current profile2/3/4 behavior uses separate channel leases as described above.

These tests execute the actual portable C decoder, without USB or HAL mocks.
From the repository root on a system with GCC:

```sh
mkdir -p build/tests
gcc -std=c99 -Wall -Wextra -Werror -pedantic \
  -IFirmware/Hiwonder/src Firmware/Hiwonder/src/gamepad_report.c \
  tests/gamepad_report_test.c -o build/tests/gamepad_report_test
./build/tests/gamepad_report_test
```

```sh
gcc -std=c99 -Wall -Wextra -Werror -pedantic \
  -IFirmware/Hiwonder/src Firmware/Hiwonder/src/gamepad_arbiter.c \
  tests/gamepad_arbiter_test.c -o build/tests/gamepad_arbiter_test
./build/tests/gamepad_arbiter_test
```

The adapter test compiles the actual USB-to-decoder bridge with a small header
that supplies only the USB data types and `memset`, not replacement logic:

```sh
mkdir -p build/tests
gcc -std=c99 -Wall -Wextra -Werror -pedantic \
  -Itests/stubs -IFirmware/Hiwonder/src \
  Firmware/Hiwonder/src/gamepad_report.c Firmware/Hiwonder/src/gamepad_arbiter.c \
  Firmware/Hiwonder/src/usbh_hid_gamepad.c tests/gamepad_usb_adapter_test.c \
  -o build/tests/gamepad_usb_adapter_test
./build/tests/gamepad_usb_adapter_test
```

It checks aligned 64-byte receive storage, actual-length decoding, publication
only in `HOST_CLASS`, rejected-report clearing/counters, both original official
IDs, legacy status filtering, and reset/reconnect behavior. It does not execute
the USB host enumeration/class state machine or the production event callback.

The additional class test links the production class, decoder, adapter and
event callback against actual ST types, with transport/control dependencies
replaced by request-recording stubs:

```sh
gcc -std=c99 -Wall -Wextra -Werror -pedantic \
  -Itests/usb_class_stubs -IFirmware/Hiwonder/src \
  -IFirmware/Middlewares/ST/STM32_USB_Host_Library/Core/Inc \
  -IFirmware/Middlewares/ST/STM32_USB_Host_Library/Class/HID/Inc \
  Firmware/Hiwonder/src/usbh_hiwonder_hid.c \
  Firmware/Hiwonder/src/usbh_hid_gamepad.c Firmware/Hiwonder/src/gamepad_report.c \
  Firmware/Hiwonder/src/gamepad_arbiter.c \
  Firmware/Hiwonder/src/gamepad_handle.c tests/gamepad_usb_class_test.c \
  -o build/tests/gamepad_usb_class_test
./build/tests/gamepad_usb_class_test
```

It exercises interface-number selection, interrupt endpoint validation, poll
intervals, non-Boot requests, actual transfer length, production publication
gates, mixed IDs, SOF timeout boundaries/wrap, reconnection and existing profiles.
It does not emulate the STM32 USB controller or prove physical enumeration.
The earlier r2 counts were decoder 3656, adapter 510 and class 1079 (5245 total).
The vehicle's separately measured 20BC:5500 eight-byte mode is selected only
for bcd1003 plus endpoint length 8 and HID report-descriptor length 90. It has
no report ID; physical right-horizontal is byte 6 and vertical is byte 5.
Tests cover this measured swap, native axis values, no face-key steering/PID,
strict separation from old nine-byte 20BC, actual class-request initialization
order, interface 0, timeout boundaries/wrap and reconnection.
Physical PC evidence and limits are in [the device record](../docs/devices/0810-0001.md).

The captured neutral USB input is `00 00 0F 80 80 80 80 00 00` for interface 0
of 20BC:5500, bcdDevice 1003. It has nine bytes and no Report ID prefix. A
Windows HID API may expose a leading zero outside that USB payload; the decoder
does not accept the resulting ten-byte buffer. The USB integration must pass the
actual report length, even when its endpoint receive buffer is larger.

Tests cover independent axes and extrema, all eight hat directions and neutral,
every button bit, combined buttons, ignored C5/C4 fields, all null hat values, invalid
lengths/pointers/profiles, unchanged output on failure, and legacy compatibility.
For the original September profile1 evidence, only a neutral sample was captured;
its remaining fixtures were synthetic contract tests. Profile4 now has the
separate October physical calibration above. Neither source proves board USB
enumeration, disconnect handling or vehicle behavior without a matching
hardware test.

The historical profile1 `gamepad_20bc_5500_button_map` in `gamepad_report.c` maps one-based
Button Usages 1..15 to project masks. Usages 5..12 provisionally map to L1, R1,
L2, R2, Select, Start, L3, R3. Face usages 1..4 and extra usages 13..15 remain
unmapped pending physical checks; this prevents guessing their PID-edit actions.
SDL's primary database contains analogous ShanWan mappings for other VID/PIDs,
but no exact 20BC:5500 match was verified:
[SDL gamepad database](https://github.com/libsdl-org/SDL/blob/main/src/joystick/SDL_gamepad_db.h).
After confirming the physical labels, edit that table and its expected test masks
together. New-profile hat bits are Up=1, Right=2, Down=4, Left=8, neutral=0;
all low-nibble values 8..15 are neutral according to the descriptor's HasNull
flag. Legacy hat bytes retain their original inversion behavior.

## Pickup protection and stop evidence (stage 1)

These tests compile the same portable C modules used by Keil. The integration
runner extracts the current `pick_up`, `myabs`, `turn_off`, and `key_scan`
functions from `control.c`; only hardware/event interfaces are replaced.

```sh
mkdir -p build/tests
gcc -std=c99 -Wall -Wextra -Werror -pedantic -IFirmware/Hiwonder/inc \
  Firmware/Hiwonder/src/pickup_accel_guard.c tests/pickup_accel_guard_test.c \
  -o build/tests/pickup_accel_guard_test
./build/tests/pickup_accel_guard_test
gcc -std=c99 -Wall -Wextra -Werror -pedantic -IFirmware/Hiwonder/inc \
  Firmware/Hiwonder/src/control_stop_trace.c tests/control_stop_trace_test.c \
  -o build/tests/control_stop_trace_test
./build/tests/control_stop_trace_test
python3 tests/control_guard_integration_test.py --cc gcc --output-dir build/tests/integration
```

On this Windows host, the local test tool is Zig 0.15.2 from the
[Zig PyPI distribution](https://pypi.org/project/ziglang/0.15.2/), isolated under
`build/test-tools/`; it does not replace the Keil compiler or modify PATH.
Use `build/test-tools/ziglang/zig.exe cc` in place of `gcc` for the two C commands.
The integration runner accepts the path to `zig.exe` via `--cc`.

Coverage includes isolated acceleration spikes, sustained events, exact timer
boundaries, invalid samples, excessive sample gaps, DWT wrap, unchanged speed
limits, angle gates, key toggles, first-cause retention and a 64-frame ring.
These are software behavior tests, not proof of sensor timing, interrupt load,
motor shutdown latency or successful vehicle balancing. See
[the staged vehicle test procedure](../docs/STOP_DIAGNOSTIC_TEST.md).

## Startup safety (T2)

```sh
python3 tests/startup_safety_test.py --cc gcc --output-dir build/tests/startup
```

The runner extracts `startup_flash_read_only` and `startup_enable_control` from
the actual main.c, then compiles them with minimal register/HAL stubs. It checks
Flash PG/LOCK success and failure states, PWM/encoder reset ordering, high/low
DRDY startup, and source integration that prevents early EXTI2 and the unlinked
ADC DMA start. `--cc` accepts compiler names on PATH or an absolute compiler path;
Zig is detected automatically. Passing host tests does not prove Flash integrity
on the running board; T2 additionally requires post-boot readback and PG/LOCK checks.

## Battery filtering and acquisition (T4 / FW-VOLT-001)

```sh
mkdir -p build/tests
gcc -std=c99 -Wall -Wextra -Werror -pedantic -IFirmware/Hiwonder/inc \
  Firmware/Hiwonder/src/battery_monitor.c tests/battery_monitor_test.c \
  -o build/tests/battery_monitor_test
./build/tests/battery_monitor_test
python3 tests/battery_adc_test.py --cc gcc --output-dir build/tests/battery-adc
```

The production monitor tests cover median warmup/spikes, voltage thresholds,
continuous low/recovery confirmation, invalid/stale data, alarm spacing,
DWT wrap and diagnostic history. The ADC runner extracts the actual
`get_battery_volt()` and provides minimal HAL/DWT stubs to exercise conversion,
failure cleanup, EOC completion priority, the 1 ms wait limit, and restoration
of the CCD channel. Integration checks require battery acquisition outside the
control interrupt. These tests do not simulate analog interference or prove
physical battery voltage; T4 must be checked on the vehicle alongside T3's
retained encoder filtering and control behavior.

## HC-05D link framing, DMA ownership and motion lease

```sh
python3 tests/run_bluetooth_tests.py --cc gcc
```

This compiles the actual portable `bluetooth_link.c` and the actual `bluetooth.c`
HAL adapter against bounded test doubles. It covers strict fields, fragments,
128-byte boundaries, DWT wrap, 500ms expiry, fresh-neutral rearming, stamped RX
backlog, HT/IDLE/TC, UART/restart failures, immutable active DMA buffers, query
priority and telemetry coalescing. The integration check verifies placement of
the guard after mode selection and before control consumers. On Windows, `--cc`
can name the existing isolated Zig executable; no global compiler is changed.

Android JVM/state tests are under `Android/MVTBOT/link/tests`; bridge replay
requires the private v21 smali inputs. These tests do not execute Android ART,
the BLE radio or physical motion. See `docs/HC05D_COMPATIBILITY.md`.
