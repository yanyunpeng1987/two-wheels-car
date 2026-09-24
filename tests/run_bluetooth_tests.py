#!/usr/bin/env python3
"""Build and run actual portable link and UART adapter with minimal HAL stubs.

Example: python tests/run_bluetooth_tests.py --cc /path/to/zig.exe
No HAL timing, radio, physical motion, or device acceptance is claimed.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--output-dir", default="build/tests")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = Path(args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    name = Path(args.cc).name.lower()
    command = [args.cc] + (["cc"] if name in ("zig", "zig.exe") else [])
    flags = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic"]
    # The untouched persistent_storage.h has no final newline.
    if "clang" in name or name in ("zig", "zig.exe"):
        flags += ["-Wno-newline-eof"]
    targets = {
        "bluetooth_link_test": ["-IFirmware/BSP", "Firmware/BSP/bluetooth_link.c",
                                "tests/bluetooth_link_test.c"],
        "bluetooth_adapter_test": ["-Itests/bluetooth_stubs", "-IFirmware/BSP",
                                   "Firmware/BSP/bluetooth_link.c", "Firmware/BSP/bluetooth.c",
                                   "tests/bluetooth_adapter_test.c"],
    }
    for target, sources in targets.items():
        executable = output / (target + ".exe")
        result = subprocess.run(command + flags + sources + ["-o", str(executable)],
                                cwd=root, text=True, capture_output=True)
        (output / (target + ".build.log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"{target}: {result.stdout}{result.stderr}")
        result = subprocess.run([str(executable)], cwd=root, text=True, capture_output=True)
        (output / (target + ".run.log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        print(result.stdout, end="")
        if result.returncode:
            raise RuntimeError(f"{target}: {result.stderr}")
    control = (root / "Firmware/Hiwonder/src/control.c").read_text(encoding="utf-8")
    hook = "bluetooth_control_update((uint8_t)(running_mode == Normal_Mode));"
    assert control.count(hook) == 1
    assert control.index("key_scan();") < control.index(hook) < control.index("velocity_pwm=velocity(")
    assert control.index("gamepad_scan();") < control.index(hook) < control.index("turn_pwm=turn(")
    main_c = (root / "Firmware/Core/Src/main.c").read_text(encoding="utf-8")
    assert "else if (huart->Instance == USART6) {\n        bluetooth_uart_error_callback(huart);" in main_c
    print("bluetooth control ISR ordering and USART6 error routing passed")


if __name__ == "__main__":
    main()
