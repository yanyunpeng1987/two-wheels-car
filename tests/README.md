# Gamepad report regression tests

These tests execute the actual portable C decoder, without USB or HAL mocks.
From the repository root on a system with GCC:

```sh
mkdir -p build/tests
gcc -std=c99 -Wall -Wextra -Werror -pedantic \
  -IFirmware/Hiwonder/src Firmware/Hiwonder/src/gamepad_report.c \
  tests/gamepad_report_test.c -o build/tests/gamepad_report_test
./build/tests/gamepad_report_test
```

The adapter test compiles the actual USB-to-decoder bridge with a small header
that supplies only the USB data types and `memset`, not replacement logic:

```sh
mkdir -p build/tests
gcc -std=c99 -Wall -Wextra -Werror -pedantic \
  -Itests/stubs -IFirmware/Hiwonder/src \
  Firmware/Hiwonder/src/gamepad_report.c \
  Firmware/Hiwonder/src/usbh_hid_gamepad.c tests/gamepad_usb_adapter_test.c \
  -o build/tests/gamepad_usb_adapter_test
./build/tests/gamepad_usb_adapter_test
```

It checks aligned 64-byte receive storage, actual-length decoding, publication
only in `HOST_CLASS`, rejected-report clearing/counters, both original official
IDs, legacy status filtering, and reset/reconnect behavior. It does not execute
the USB host enumeration/class state machine or the production event callback.

The captured neutral USB input is `00 00 0F 80 80 80 80 00 00` for interface 0
of 20BC:5500, bcdDevice 1003. It has nine bytes and no Report ID prefix. A
Windows HID API may expose a leading zero outside that USB payload; the decoder
does not accept the resulting ten-byte buffer. The USB integration must pass the
actual report length, even when its endpoint receive buffer is larger.

Tests cover independent axes and extrema, all eight hat directions and neutral,
every button bit, combined buttons, ignored C5/C4 fields, all null hat values, invalid
lengths/pointers/profiles, unchanged output on failure, and legacy compatibility.
Only the neutral sample was captured from the device. Other samples are synthetic
contract tests; passing them does not prove physical button labels or board USB
enumeration, disconnect handling, or vehicle behavior.

The editable `gamepad_20bc_5500_button_map` in `gamepad_report.c` maps one-based
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
