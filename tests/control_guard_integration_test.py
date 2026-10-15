#!/usr/bin/env python3
"""Run current control.c guard/key functions as native C with hardware stubs.

The four functions are extracted verbatim from the workspace, not reimplemented.
The harness links the real acceleration guard and stop trace modules. It models
only the relevant callback ordering; it does not emulate HAL or prove hardware
timing. Generated source, executable, and compiler log stay in --output-dir.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import sys


TOKENS = re.compile(
    r'/\*[\s\S]*?\*/|//[^\r\n]*|"(?:\\.|[^"\\])*"|'
    r"'(?:\\.|[^'\\])*'|[{}]"
)


def extract_function(source: str, name: str) -> tuple[str, int]:
    pattern = re.compile(
        rf"(?m)^(?:int|void|uint8_t)\s+{re.escape(name)}\s*\([^;{{}}]*\)\s*\{{"
    )
    matches = list(pattern.finditer(source))
    if len(matches) != 1:
        raise ValueError(f"Expected exactly one definition of {name}; got {len(matches)}")
    match = matches[0]
    opening = source.index("{", match.start(), match.end())
    depth = 0
    for token in TOKENS.finditer(source, opening):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():token.end()], source.count("\n", 0, match.start()) + 1
    raise ValueError(f"Unclosed function body: {name}")


PRELUDE = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pickup_accel_guard.h"
#include "control_stop_trace.h"

static volatile uint8_t flag_move;
static uint8_t running_mode;
static int mode;
static float voltage, velocity_left, velocity_right, acceleration_z;
static uint32_t SystemCoreClock = 84000000U;
static PickupAccelGuard pickup_accel_guard;
static uint8_t control_imu_valid, control_stop_pending;
static uint32_t control_imu_cycles;
static volatile ControlStopTrace control_stop_trace;

typedef enum {
    KEY_EVENT_NONE, KEY_EVENT_SINGLE_CLICK, KEY_EVENT_DOUBLE_CLICK,
    KEY_EVENT_LONG_PRESS
} KeyEvent_t;
static KeyEvent_t queued_key;
static unsigned int beep_count;
typedef struct Buzzer Buzzer;
struct Buzzer { void (*beep)(Buzzer *, int, int, int, int); };
static void beep_stub(Buzzer *self, int frequency, int on, int off, int repeat)
{
    (void)self; (void)frequency; (void)on; (void)off; (void)repeat;
    ++beep_count;
}
static Buzzer buzzers[] = {{beep_stub}};
static KeyEvent_t key_event_scan(void)
{
    KeyEvent_t event = queued_key;
    queued_key = KEY_EVENT_NONE;
    return event;
}
'''


HARNESS = r'''
static unsigned int checks, failures;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static void reset_fixture(void)
{
    flag_move = 1U;
    running_mode = 3U;
    mode = 3;
    voltage = 12.0f;
    velocity_left = velocity_right = 0.0f;
    acceleration_z = 1.0f;
    control_imu_valid = 1U;
    control_stop_pending = CONTROL_STOP_NONE;
    control_imu_cycles = 0U;
    queued_key = KEY_EVENT_NONE;
    beep_count = 0U;
    pickup_accel_guard_reset(&pickup_accel_guard);
    control_stop_trace_reset(&control_stop_trace);
}

/* Minimal caller stitching only: the actual functions above decide the gates. */
static void record_final_gate(float angle)
{
    ControlStopSample sample;
    memset(&sample, 0, sizeof(sample));
    sample.cycles = control_imu_cycles;
    sample.flag_move = flag_move;
    sample.output_allowed = (turn_off(angle) == 0U);
    sample.angle = angle;
    if (angle < -80.0f || angle > 80.0f) {
        control_stop_pending |= CONTROL_STOP_ANG;
    }
    sample.reason_mask = control_stop_pending;
    control_stop_trace_update(&control_stop_trace, &sample);
}

static void press_long(void)
{
    queued_key = KEY_EVENT_LONG_PRESS;
    key_scan();
}

static void test_temporary_speed_boundaries(void)
{
    static const struct {
        float volts, left, right;
        int expected;
    } cases[] = {
        {12.0f, 180.0f, 180.0f, 0},
        {12.0f, 180.9f, 180.9f, 0},
        {12.0f, -180.9f, -180.9f, 0},
        {12.0f, 181.0f, 180.0f, 1},
        {12.0f, -181.0f, 180.0f, 1},
        {11.0f, 170.0f, 170.0f, 0},
        {11.0f, 170.9f, 170.9f, 0},
        {11.0f, -170.9f, -170.9f, 0},
        {11.0f, 171.0f, 170.0f, 1},
        {11.0f, -171.0f, 170.0f, 1},
        {11.0001f, 171.0f, 170.0f, 0},
        {10.9999f, 171.0f, 170.0f, 1},
        {12.2f, 0.0f, 253.663467f, 0},
        {12.2f, 0.0f, 348.0f, 0},
        {11.0f, 0.0f, 348.0f, 1},
        {12.0f, 0.0f, 361.0f, 1},
        {12.0f, 100.0f, 261.0f, 1}
    };
    unsigned int i;
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        reset_fixture();
        voltage = cases[i].volts;
        velocity_left = cases[i].left;
        velocity_right = cases[i].right;
        CHECK(pick_up() == cases[i].expected);
        CHECK(control_stop_pending == (cases[i].expected ?
              CONTROL_STOP_SPD : CONTROL_STOP_NONE));
    }
    CHECK(myabs(-180) == 180);
    CHECK(myabs(180) == 180);
    CHECK(myabs(0) == 0);
}

static void test_acceleration_through_original_pick_up(void)
{
    unsigned int i;
    const uint32_t cycles_per_ms = SystemCoreClock / 1000U;
    reset_fixture();
    acceleration_z = 2.0f;
    CHECK(pick_up() == 0);
    control_imu_cycles = 4U * cycles_per_ms;
    acceleration_z = 1.0f;
    CHECK(pick_up() == 0);
    CHECK(pickup_accel_guard.tracking == 0U);
    CHECK(control_stop_pending == CONTROL_STOP_NONE);

    reset_fixture();
    acceleration_z = 2.0f;
    for (i = 0U; i < 5U; ++i) {
        control_imu_cycles = i * 4U * cycles_per_ms;
        CHECK(pick_up() == 0);
    }
    control_imu_cycles = 20U * cycles_per_ms;
    CHECK(pick_up() == 1);
    CHECK(control_stop_pending == CONTROL_STOP_ACC);

    reset_fixture();
    acceleration_z = 2.0f;
    CHECK(pick_up() == 0);
    control_imu_cycles = 4U * cycles_per_ms;
    control_imu_valid = 0U;
    CHECK(pick_up() == 0);
    CHECK(pickup_accel_guard.tracking == 0U);
    CHECK(control_stop_pending == CONTROL_STOP_NONE);
}

static void test_key_stop_and_explicit_restart(void)
{
    reset_fixture();
    record_final_gate(0.0f);
    press_long();
    CHECK(flag_move == 0U);
    CHECK(control_stop_pending == CONTROL_STOP_KEY);
    CHECK(beep_count == 1U);
    CHECK(running_mode == 3U);
    record_final_gate(0.0f);
    CHECK(control_stop_trace.frozen == 1U);
    CHECK(control_stop_trace.trigger.reason_mask == CONTROL_STOP_KEY);

    /* A separate callback, now stopped, accepts an explicit restart. */
    control_stop_pending = CONTROL_STOP_NONE;
    pickup_accel_guard.tracking = 1U;
    press_long();
    CHECK(flag_move == 1U);
    CHECK(control_stop_trace.frozen == 0U);
    CHECK(control_stop_trace.count == 0U);
    CHECK(pickup_accel_guard.tracking == 0U);
    CHECK(control_stop_pending == CONTROL_STOP_NONE);
    CHECK(beep_count == 2U);
    record_final_gate(0.0f);
    CHECK(control_stop_trace.was_allowed == 1U);
}

static void test_pickup_key_same_callback_preserves_final_angle_stop(void)
{
    reset_fixture();
    record_final_gate(0.0f);
    velocity_left = 361.0f;
    CHECK(pick_up() == 1);
    flag_move = 0U; /* Original callback applies pick_up's decision. */
    press_long();
    CHECK(flag_move == 1U);
    CHECK(control_stop_pending == CONTROL_STOP_SPD);
    CHECK(control_stop_trace.was_allowed == 1U);
    CHECK(control_stop_trace.count == 1U);
    record_final_gate(81.0f);
    CHECK(control_stop_trace.frozen == 1U);
    CHECK(control_stop_trace.trigger.reason_mask ==
          (CONTROL_STOP_SPD | CONTROL_STOP_ANG));
    CHECK(control_stop_trace.trigger.flag_move == 1U);
    CHECK(control_stop_trace.trigger.output_allowed == 0U);
}

static void test_pickup_key_without_final_stop_does_not_freeze(void)
{
    reset_fixture();
    record_final_gate(0.0f);
    velocity_left = 361.0f;
    CHECK(pick_up() == 1);
    flag_move = 0U;
    press_long();
    record_final_gate(0.0f);
    CHECK(flag_move == 1U);
    CHECK(control_stop_trace.frozen == 0U);
    CHECK(control_stop_trace.was_allowed == 1U);
}

static void test_original_angle_boundaries(void)
{
    static const float allowed[] = {-80.0f, -79.9f, 0.0f, 79.9f, 80.0f};
    unsigned int i;
    reset_fixture();
    for (i = 0U; i < sizeof(allowed) / sizeof(allowed[0]); ++i) {
        CHECK(turn_off(allowed[i]) == 0U);
    }
    CHECK(turn_off(-80.001f) == 1U);
    CHECK(turn_off(80.001f) == 1U);
    CHECK(flag_move == 1U);
    flag_move = 0U;
    CHECK(turn_off(0.0f) == 1U);
    CHECK(turn_off(80.0f) == 1U);
}

int main(void)
{
    test_temporary_speed_boundaries();
    test_acceleration_through_original_pick_up();
    test_key_stop_and_explicit_restart();
    test_pickup_key_same_callback_preserves_final_angle_stop();
    test_pickup_key_without_final_stop_does_not_freeze();
    test_original_angle_boundaries();
    printf("control guard integration: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
'''


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", type=Path, required=True, help="Native C compiler executable")
    parser.add_argument("--zig", action="store_true", help="Invoke the compiler with the cc subcommand")
    parser.add_argument("--output-dir", type=Path, required=True, help="Generated test build directory")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    original = root / "Firmware/Hiwonder/src/control.c"
    original_bytes = original.read_bytes()
    source = original_bytes.decode("utf-8-sig")
    pieces = [PRELUDE]
    for name in ("PICKUP_ACCEL_CONFIRM_MS", "PICKUP_ACCEL_MAX_GAP_MS"):
        macros = re.findall(rf"(?m)^#define\s+{name}\s+[^\r\n]+", source)
        if len(macros) != 1:
            raise ValueError(f"Expected exactly one {name} macro")
        pieces.append(macros[0])
    for name in ("myabs", "turn_off", "pick_up", "key_scan"):
        body, line = extract_function(source, name)
        pieces.append(f'#line {line} "{original.as_posix()}"\n{body}')
    pieces.extend(['#line 1 "control_guard_integration_harness.c"', HARNESS])

    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    generated = output_dir / "control_guard_integration.generated.c"
    executable = output_dir / "control_guard_integration_test.exe"
    generated.write_text("\n\n".join(pieces), encoding="utf-8", newline="\n")
    compiler = shutil.which(str(args.cc))
    if compiler is None:
        compiler = str(args.cc.resolve())
    command = [compiler]
    if args.zig or args.cc.stem.lower() == "zig":
        command.append("cc")
    warnings = ["-Wall", "-Werror"]
    if args.cc.stem.lower() != "tcc":
        warnings.append("-Wextra")
    command.extend([
        "-std=c99", "-O0", *warnings,
        "-I", str(root / "Firmware/Hiwonder/inc"), str(generated),
        str(root / "Firmware/Hiwonder/src/pickup_accel_guard.c"),
        str(root / "Firmware/Hiwonder/src/control_stop_trace.c"),
        "-o", str(executable),
    ])
    compiled = subprocess.run(command, cwd=root, capture_output=True, text=True)
    compiler_output = compiled.stdout + compiled.stderr
    (output_dir / "control_guard_integration.compile.log").write_text(
        compiler_output, encoding="utf-8"
    )
    if compiler_output:
        print(compiler_output, end="")
    if compiled.returncode:
        return compiled.returncode
    print(f"Source SHA-256: {hashlib.sha256(original_bytes).hexdigest()}", flush=True)
    return subprocess.run([str(executable)], cwd=output_dir).returncode


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"control guard integration: {error}", file=sys.stderr)
        sys.exit(2)
