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
