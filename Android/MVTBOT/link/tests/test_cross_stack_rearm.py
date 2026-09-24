#!/usr/bin/env python3
"""Replay actual MiniBalanLink output through the actual MCU UART adapter.

This compiles the maintained Java transport against the deterministic Android
fakes from test_ble_session.py, records every GATT write and its timestamp, then
feeds those writes into bluetooth.c/bluetooth_link.c using the existing HAL
adapter fixture. Both main-loop scheduling orders around an idle timeout are
tested. This is host evidence, not a radio, UART timing, or vehicle acceptance.

Usage: python test_cross_stack_rearm.py --java-home <JDK> --cc <gcc-or-zig>
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import subprocess
from pathlib import Path


JAVA = r'''package com.mvtbot.link;
import android.bluetooth.*;
import android.content.Context;
import android.os.*;
import android.view.View;
import java.util.*;

public final class CrossStackRearmTest {
    static final Context context = new Context();
    static final View view = new View();
    static MiniBalanLink.ControlReset owner = () -> {};
    static final Handler ui = new Handler(message -> MiniBalanLink.acceptUiEvent(message));
    static int assertions;
    static void check(boolean condition, String reason) {
        ++assertions;
        if (!condition) throw new AssertionError(reason);
    }
    static UUID uuid(String id) {
        return UUID.fromString("0000" + id + "-0000-1000-8000-00805f9b34fb");
    }
    static BluetoothGatt open() {
        BluetoothGatt gatt = new BluetoothGatt();
        BluetoothGattService service = new BluetoothGattService(uuid("e0ff"));
        service.add(new BluetoothGattCharacteristic(uuid("ffe1"), 20, true));
        gatt.services.add(service);
        MiniBalanLink.destroy();
        MiniBalanLink.bind(context, ui);
        view.shown = true;
        MiniBalanLink.bindControl(owner, view);
        MiniBalanLink.foreground(true);
        check(MiniBalanLink.connect(new BluetoothDevice("HC-05D", gatt)), "connect accepted");
        Handler.advance(0);
        gatt.discover();
        gatt.completeDescriptor(0);
        Handler.advance(60);
        check(MiniBalanLink.isReady(), "ready after subscription");
        Handler.advance(600); // MCU control lease expires while its UART stays quiet.
        return gatt;
    }
    static void verify(String scenario, BluetoothGatt gatt, long freshAt, int direction) {
        long firstMotion = -1;
        List<Long> freshZeros = new ArrayList<>();
        for (int i = 0; i < gatt.writes.size(); ++i) {
            String wire = gatt.writes.get(i);
            long at = gatt.writeTimes.get(i);
            if (at >= freshAt && wire.equals("CMD|3|0|0|$")) freshZeros.add(at);
            if (!wire.equals("CMD|3|0|0|$")) {
                check(direction != 0, scenario + ": released gesture cannot emit motion");
                check(wire.equals("CMD|3|" + direction + "|0|$"), scenario + ": old direction replayed");
                if (firstMotion < 0) {
                    firstMotion = at;
                    check(freshZeros.size() >= 2, scenario + ": two fresh zeros required");
                    // Release can itself dispatch an extra zero immediately
                    // before the new gesture's two arming zeros.
                    check(freshZeros.get(freshZeros.size() - 1)
                            - freshZeros.get(freshZeros.size() - 2) >= 100,
                            scenario + ": independent zero dispatches must be separated");
                    check(at >= freshAt + 160, scenario + ": stale completion unlocked a new gesture");
                }
            }
            // Normalize each scenario to the first subscription-time zero.
            System.out.println("TRACE " + scenario + " " + (at - gatt.writeTimes.get(0)) + " " + wire);
        }
        check(direction == 0 || firstMotion >= 0, scenario + ": fresh motion eventually sent");
    }
    public static void main(String[] arguments) {
        BluetoothGatt gatt = open();
        long fresh = Handler.now;
        MiniBalanLink.motion(owner, 1, 0);
        Handler.advance(400);
        verify("quiet_timeout", gatt, fresh, 1);

        gatt = open();
        MiniBalanLink.motion(owner, 1, 0);
        Handler.advance(10); // First zero is still the active TX.
        MiniBalanLink.release();
        fresh = Handler.now;
        MiniBalanLink.motion(owner, -1, 0);
        Handler.advance(400);
        verify("release_during_first_zero", gatt, fresh, -1);

        gatt = open();
        MiniBalanLink.motion(owner, 1, 0);
        Handler.advance(80); // Old first completion already scheduled its second zero.
        MiniBalanLink.release();
        fresh = Handler.now;
        MiniBalanLink.motion(owner, -1, 0);
        Handler.advance(400);
        verify("release_during_zero_gap", gatt, fresh, -1);

        gatt = open();
        MiniBalanLink.motion(owner, 1, 0);
        Handler.advance(80);
        MiniBalanLink.release();
        fresh = Handler.now;
        Handler.advance(400);
        verify("release_without_repress", gatt, fresh, 0);

        gatt = open();
        MiniBalanLink.motion(owner, 1, 0);
        Handler.advance(10);
        MiniBalanLink.ControlReset oldOwner = owner;
        owner = new MiniBalanLink.ControlReset() { public void resetMotion() {} };
        MiniBalanLink.bindControl(owner, view);
        fresh = Handler.now;
        MiniBalanLink.motion(oldOwner, 1, 0);
        MiniBalanLink.motion(owner, -1, 0);
        Handler.advance(400);
        verify("owner_replaced_during_arm", gatt, fresh, -1);
        System.out.println("JAVA_ASSERTIONS " + assertions);
    }
}
'''


def run(command: list[str], *, cwd: Path, input_text: str | None = None) -> str:
    result = subprocess.run(command, cwd=cwd, input=input_text,
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--java-home", type=Path, required=True)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = here.parents[3]
    output = (args.output_dir or repo / "build/hc05d-validation/cross-stack-rearm").resolve()
    output.mkdir(parents=True, exist_ok=True)

    spec = importlib.util.spec_from_file_location("ble_fixture", here / "test_ble_session.py")
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load maintained Android host fixture")
    fixture = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixture)
    stubs = dict(fixture.STUBS)
    # The shared Android fixture records actual write submission timestamps.
    # Reuse that interface instead of rewriting the fixture's Java statements.

    java_inputs: list[Path] = []
    for name, source in stubs.items():
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")
        java_inputs.append(path)
    test = output / "com/mvtbot/link/CrossStackRearmTest.java"
    test.parent.mkdir(parents=True, exist_ok=True)
    test.write_text(JAVA, encoding="utf-8")
    java_inputs.append(test)
    actual_java = sorted((here.parent / "src").rglob("*.java"))
    java_inputs += actual_java
    classes = output / "classes"
    classes.mkdir(exist_ok=True)
    java_suffix = ".exe" if (args.java_home / "bin/javac.exe").exists() else ""
    run([str(args.java_home / ("bin/javac" + java_suffix)), "-encoding", "UTF-8",
         "-d", str(classes), *map(str, java_inputs)], cwd=repo)
    java_log = run([str(args.java_home / ("bin/java" + java_suffix)), "-cp", str(classes),
                    "com.mvtbot.link.CrossStackRearmTest"], cwd=repo)
    (output / "java-transmit-trace.txt").write_text(java_log, encoding="utf-8")

    # Reuse the real adapter fixture's HAL edge stubs and test-state reset in this
    # translation unit; its baseline main is renamed rather than copied/changed.
    harness = output / "mcu_pipe.c"
    adapter_fixture = (repo / "tests/bluetooth_adapter_test.c").as_posix()
    harness.write_text('#define main baseline_test_main\n#include "' + adapter_fixture + '"\n'
                       '#undef main\n' + r'''
#include <stdlib.h>
int main(int argc, char **argv) {
    unsigned long ms;
    char wire[129];
    const int service_before_receive = argc > 1 && atoi(argv[1]) != 0;
    reset();
    while (scanf("%lu %128s", &ms, wire) == 2) {
        at((uint32_t)ms);
        bluetooth_control_update(1U);
        if (service_before_receive) bluetooth_main_loop_task();
        receive(wire);
        process();
        printf("%lu %s %u %u %u %u %lu\n", ms, wire,
            (unsigned)blue_front, (unsigned)blue_back,
            (unsigned)blue_left, (unsigned)blue_right,
            (unsigned long)bluetooth_link_stats.stale_frames);
    }
    return 0;
}
''', encoding="utf-8")
    compiler = [args.cc] + (["cc"] if Path(args.cc).name.lower() in ("zig", "zig.exe") else [])
    flags = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic"]
    if "clang" in Path(args.cc).name.lower() or Path(args.cc).name.lower() in ("zig", "zig.exe"):
        flags.append("-Wno-newline-eof")
    executable = output / "mcu_pipe.exe"
    run(compiler + flags + ["-Itests/bluetooth_stubs", "-IFirmware/BSP",
                            "Firmware/BSP/bluetooth_link.c", "Firmware/BSP/bluetooth.c",
                            str(harness), "-o", str(executable)], cwd=repo)

    scenarios: dict[str, list[str]] = {}
    for line in java_log.splitlines():
        if line.startswith("TRACE "):
            _, scenario, at, wire = line.split()
            scenarios.setdefault(scenario, []).append(f"{at} {wire}")
    if len(scenarios) != 5:
        raise AssertionError("missing Java scenario traces")
    assertions = 0
    report: dict[str, object] = {"java_result": java_log.splitlines()[-1], "replays": []}
    for scenario, rows in scenarios.items():
        for service_before in (0, 1):
            result = run([str(executable), str(service_before)], cwd=repo,
                         input_text="\n".join(rows) + "\n")
            (output / f"{scenario}-main-before-{service_before}.txt").write_text(result, encoding="utf-8")
            output_rows = result.splitlines()
            if len(output_rows) != len(rows):
                raise AssertionError("MCU did not consume every actual Java write")
            for row in output_rows:
                at, wire, front, back, left, right, stale = row.split()
                fields = wire.split("|")
                speed, turn = int(fields[2]), int(fields[3])
                expected = (int(speed > 0), int(speed < 0), int(turn < 0), int(turn > 0))
                actual = tuple(map(int, (front, back, left, right)))
                assertions += 1
                if actual != expected:
                    raise AssertionError(f"{scenario} at {at}ms: {wire} -> {actual}, expected {expected}")
            report["replays"].append({"scenario": scenario, "main_before_receive": bool(service_before),
                                      "frames": len(output_rows), "final": output_rows[-1]})
    report["mcu_frame_assertions"] = assertions
    report["source_sha256"] = {str(path.relative_to(repo)): hashlib.sha256(path.read_bytes()).hexdigest()
                               for path in actual_java + [repo / "Firmware/BSP/bluetooth.c",
                                                          repo / "Firmware/BSP/bluetooth_link.c"]}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Cross-stack rearm replay: PASS ({len(scenarios)} scenarios, 2 MCU schedules, "
          f"{assertions} frame assertions; {report['java_result']})")


if __name__ == "__main__":
    main()
