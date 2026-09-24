#!/usr/bin/env python3
"""FW-VOLT-001: exercise the actual battery ADC function with native HAL stubs.

The function is extracted verbatim from adc.c. Tests cover conversion failures,
bounded waits, counter wrap, and ADC ownership/cleanup, not electrical accuracy.
Generated sources, executables, and logs remain under --output-dir.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from startup_safety_test import extract_function, without_comments


def check_integration(adc_source: str, control_source: str, main_source: str) -> int:
    checks = 0

    def require(condition: bool, message: str) -> None:
        nonlocal checks
        checks += 1
        if not condition:
            raise ValueError(f"Battery integration: {message}")

    adc_body = without_comments(extract_function(adc_source, "get_battery_volt")[0])
    irq_body = without_comments(extract_function(control_source, "HAL_GPIO_EXTI_Callback")[0])
    alarm_body = without_comments(extract_function(control_source, "handle_low_voltage_alarm")[0])
    main_body = without_comments(extract_function(main_source, "main")[0])
    require(not re.search(r"\bget_battery_volt\s*\(|\bHAL_ADC_\w+\s*\(", irq_body),
            "control IRQ must not own or poll ADC1")
    require(bool(re.search(r"\bget_battery_volt\s*\(", alarm_body)),
            "main-loop voltage service must perform battery acquisition")
    require(bool(re.search(r"\bhandle_low_voltage_alarm\s*\(\s*\)", main_body)),
            "main must service voltage acquisition and alarms")
    require(not re.search(r"\bhandle_low_voltage_alarm\s*\(", irq_body),
            "control IRQ must not call the main-loop voltage service")
    require(not re.search(r"\bHAL_Delay(?:_us)?\s*\(|\bHAL_GetTick\s*\(", adc_body),
            "battery acquisition must not block on delays or the divided tick")
    poll_calls = re.findall(r"\bHAL_ADC_PollForConversion\s*\(\s*&hadc1\s*,\s*([^)]*)\)", adc_body)
    require(bool(poll_calls) and all(re.fullmatch(r"0[Uu]?\s*", arg) for arg in poll_calls),
            "HAL conversion polling must use zero timeout after EOC")
    return checks


PRELUDE = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int checks, failures;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t unused; } ADC_HandleTypeDef;
typedef struct { uint32_t Channel, Rank, SamplingTime; } ADC_ChannelConfTypeDef;
typedef struct { volatile uint32_t CYCCNT; } DWTStub;
static ADC_HandleTypeDef hadc1;
static DWTStub fake_dwt;
#define DWT (&fake_dwt)
#define RESET 0U
#define ADC_FLAG_EOC 2U
#define ADC_CHANNEL_4 4U
#define ADC_CHANNEL_10 10U
#define ADC_SAMPLETIME_480CYCLES 480U
#define ADC_SAMPLETIME_15CYCLES 15U
static uint32_t SystemCoreClock = 84000000U;

enum Event { STOP_ADC, CONFIG_BATTERY, START_ADC, READ_EOC,
             POLL_HAL, READ_VALUE, CONFIG_CCD };
static enum Event events[128];
static unsigned int event_count;
static HAL_StatusTypeDef first_stop_status, cleanup_stop_status;
static HAL_StatusTypeDef battery_config_status, ccd_config_status;
static HAL_StatusTypeDef start_status, poll_status;
static unsigned int stop_calls, battery_config_calls, ccd_config_calls;
static unsigned int start_calls, flag_calls, poll_calls, value_calls;
static unsigned int ready_after_reads, eoc_ready;
static uint32_t tick_step, raw_value, current_channel, current_sample_time;

static void event(enum Event item)
{
    if (event_count < sizeof(events) / sizeof(events[0])) {
        events[event_count++] = item;
    } else {
        fprintf(stderr, "ADC wait/event sequence exceeded test bound\n");
        exit(2);
    }
}
static HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *handle)
{
    CHECK(handle == &hadc1);
    event(STOP_ADC);
    ++stop_calls;
    return stop_calls == 1U ? first_stop_status : cleanup_stop_status;
}
static HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *handle,
                                              ADC_ChannelConfTypeDef *config)
{
    HAL_StatusTypeDef status;
    CHECK(handle == &hadc1);
    CHECK(config->Rank == 1U);
    CHECK(config->Channel == ADC_CHANNEL_4 || config->Channel == ADC_CHANNEL_10);
    if (config->Channel == ADC_CHANNEL_4) {
        event(CONFIG_BATTERY);
        ++battery_config_calls;
        CHECK(config->SamplingTime == ADC_SAMPLETIME_480CYCLES);
        status = battery_config_status;
    } else {
        event(CONFIG_CCD);
        ++ccd_config_calls;
        CHECK(config->SamplingTime == ADC_SAMPLETIME_15CYCLES);
        status = ccd_config_status;
    }
    if (status == HAL_OK) {
        current_channel = config->Channel;
        current_sample_time = config->SamplingTime;
    }
    return status;
}
static HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *handle)
{
    CHECK(handle == &hadc1);
    CHECK(current_channel == ADC_CHANNEL_4);
    CHECK(stop_calls == 1U);
    event(START_ADC);
    ++start_calls;
    return start_status;
}
static uint32_t read_flag(ADC_HandleTypeDef *handle, uint32_t flag)
{
    CHECK(handle == &hadc1 && flag == ADC_FLAG_EOC);
    CHECK(start_calls == 1U && start_status == HAL_OK);
    event(READ_EOC);
    ++flag_calls;
    DWT->CYCCNT += tick_step;
    if (ready_after_reads != 0U && flag_calls >= ready_after_reads) eoc_ready = 1U;
    return eoc_ready ? ADC_FLAG_EOC : RESET;
}
#define __HAL_ADC_GET_FLAG(handle_, flag_) read_flag((handle_), (flag_))
static HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *handle,
                                                 uint32_t timeout)
{
    CHECK(handle == &hadc1);
    CHECK(eoc_ready == 1U);
    CHECK(timeout == 0U);
    event(POLL_HAL);
    ++poll_calls;
    return poll_status;
}
static uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *handle)
{
    CHECK(handle == &hadc1);
    CHECK(eoc_ready && poll_calls == 1U && poll_status == HAL_OK);
    event(READ_VALUE);
    ++value_calls;
    return raw_value;
}
'''


HARNESS = r'''
static void reset_case(void)
{
    first_stop_status = cleanup_stop_status = HAL_OK;
    battery_config_status = ccd_config_status = HAL_OK;
    start_status = poll_status = HAL_OK;
    event_count = 0U;
    stop_calls = battery_config_calls = ccd_config_calls = 0U;
    start_calls = flag_calls = poll_calls = value_calls = 0U;
    ready_after_reads = 1U;
    eoc_ready = 0U;
    tick_step = 8400U;
    raw_value = 1343U;
    DWT->CYCCNT = 123456U;
    current_channel = ADC_CHANNEL_10;
    current_sample_time = ADC_SAMPLETIME_15CYCLES;
}
static void check_cleanup(void)
{
    CHECK(stop_calls == 2U);
    CHECK(ccd_config_calls == 1U);
    CHECK(event_count >= 3U);
    CHECK(events[event_count - 2U] == STOP_ADC);
    CHECK(events[event_count - 1U] == CONFIG_CCD);
    if (ccd_config_status == HAL_OK) {
        CHECK(current_channel == ADC_CHANNEL_10);
        CHECK(current_sample_time == ADC_SAMPLETIME_15CYCLES);
    }
}
static void test_normal_conversion(void)
{
    static const uint32_t codes[] = {
        0U, 1U, 512U, 563U, 564U, 565U, 1015U, 1016U,
        1072U, 1241U, 1342U, 1343U, 1421U, 2048U, 4094U, 4095U
    };
    unsigned int i;
    for (i = 0U; i < sizeof(codes) / sizeof(codes[0]); ++i) {
        /* The established scale is 36.30 V full scale, truncated to cV. */
        const int expected_cv = (int)(codes[i] * 3630U / 4096U);
        reset_case();
        raw_value = codes[i];
        CHECK(get_battery_volt() == expected_cv);
        CHECK(battery_config_calls == 1U && start_calls == 1U);
        CHECK(flag_calls == 1U && poll_calls == 1U && value_calls == 1U);
        CHECK(events[0] == STOP_ADC && events[1] == CONFIG_BATTERY);
        check_cleanup();
    }
    reset_case();
    ready_after_reads = 4U;
    CHECK(get_battery_volt() == 1190);
    CHECK(flag_calls == 4U && poll_calls == 1U && value_calls == 1U);
    check_cleanup();
}
static void test_setup_failures(void)
{
    static const HAL_StatusTypeDef errors[] = { HAL_ERROR, HAL_BUSY, HAL_TIMEOUT };
    unsigned int i;
    for (i = 0U; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        reset_case();
        first_stop_status = errors[i];
        CHECK(get_battery_volt() == -1);
        CHECK(stop_calls == 1U && event_count == 1U);
        CHECK(battery_config_calls == 0U && ccd_config_calls == 0U);
        CHECK(start_calls == 0U && flag_calls == 0U && value_calls == 0U);
        CHECK(current_channel == ADC_CHANNEL_10);

        reset_case();
        battery_config_status = errors[i];
        CHECK(get_battery_volt() == -1);
        CHECK(battery_config_calls == 1U && start_calls == 0U);
        CHECK(flag_calls == 0U && poll_calls == 0U && value_calls == 0U);
        check_cleanup();

        reset_case();
        start_status = errors[i];
        CHECK(get_battery_volt() == -1);
        CHECK(start_calls == 1U && flag_calls == 0U && poll_calls == 0U);
        CHECK(value_calls == 0U);
        check_cleanup();
    }
}
static void test_deadline_and_wrap(void)
{
    uint32_t started;
    unsigned int wrapped;
    for (wrapped = 0U; wrapped <= 1U; ++wrapped) {
        reset_case();
        if (wrapped) DWT->CYCCNT = UINT32_MAX - 4200U;
        started = DWT->CYCCNT;
        ready_after_reads = 0U;
        CHECK(get_battery_volt() == -1);
        CHECK(flag_calls == 10U); /* 10 * 100 us reaches the 1 ms bound. */
        CHECK((uint32_t)(DWT->CYCCNT - started) == SystemCoreClock / 1000U);
        CHECK(poll_calls == 0U && value_calls == 0U);
        check_cleanup();

        reset_case();
        if (wrapped) DWT->CYCCNT = UINT32_MAX - 4200U;
        ready_after_reads = 3U;
        CHECK(get_battery_volt() == 1190);
        CHECK(flag_calls == 3U && poll_calls == 1U && value_calls == 1U);
        check_cleanup();

        /* EOC completes during a long ISR; elapsed deadline must not veto it. */
        reset_case();
        if (wrapped) DWT->CYCCNT = UINT32_MAX - 4200U;
        tick_step = SystemCoreClock / 100U; /* Ten milliseconds before the next EOC read. */
        CHECK(get_battery_volt() == 1190);
        CHECK(flag_calls == 1U && poll_calls == 1U && value_calls == 1U);
        check_cleanup();
    }
    reset_case();
    ready_after_reads = 10U; /* Exactly at the deadline, completion still wins. */
    CHECK(get_battery_volt() == 1190);
    CHECK(flag_calls == 10U && poll_calls == 1U);
    check_cleanup();
}
static void test_completion_and_cleanup_failures(void)
{
    static const HAL_StatusTypeDef errors[] = { HAL_ERROR, HAL_BUSY, HAL_TIMEOUT };
    unsigned int i;
    for (i = 0U; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        reset_case();
        poll_status = errors[i];
        CHECK(get_battery_volt() == -1);
        CHECK(flag_calls == 1U && poll_calls == 1U && value_calls == 0U);
        check_cleanup();

        reset_case();
        cleanup_stop_status = errors[i];
        CHECK(get_battery_volt() == -1);
        CHECK(value_calls == 1U);
        check_cleanup();

        reset_case();
        ccd_config_status = errors[i];
        CHECK(get_battery_volt() == -1);
        CHECK(value_calls == 1U);
        check_cleanup();

        reset_case();
        start_status = cleanup_stop_status = ccd_config_status = errors[i];
        CHECK(get_battery_volt() == -1);
        CHECK(flag_calls == 0U && value_calls == 0U);
        check_cleanup();
    }
    reset_case();
    raw_value = 4096U;
    CHECK(get_battery_volt() == -1);
    CHECK(value_calls == 1U);
    check_cleanup();
    reset_case();
    raw_value = UINT32_MAX;
    CHECK(get_battery_volt() == -1);
    CHECK(value_calls == 1U);
    check_cleanup();
}
int main(void)
{
    test_normal_conversion();
    test_setup_failures();
    test_deadline_and_wrap();
    test_completion_and_cleanup_failures();
    printf("battery ADC: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
'''


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True, help="Native C compiler path or command on PATH")
    parser.add_argument("--zig", action="store_true", help="Invoke compiler with cc subcommand")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    original = root / "Firmware/Core/Src/adc.c"
    original_bytes = original.read_bytes()
    source = original_bytes.decode("utf-8-sig")
    control_source = (root / "Firmware/Hiwonder/src/control.c").read_text(encoding="utf-8-sig")
    main_source = (root / "Firmware/Core/Src/main.c").read_text(encoding="utf-8-sig")
    body, line = extract_function(source, "get_battery_volt")
    pieces = [PRELUDE, f'#line {line} "{original.as_posix()}"\n{body}',
              '#line 1 "battery_adc_harness.c"', HARNESS]
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    generated = output_dir / "battery_adc.generated.c"
    executable = output_dir / "battery_adc_test.exe"
    generated.write_text("\n\n".join(pieces), encoding="utf-8", newline="\n")
    compiler = shutil.which(args.cc) or str(Path(args.cc).resolve())
    command = [compiler]
    environment = os.environ.copy()
    if args.zig or Path(compiler).stem.lower() == "zig":
        command.append("cc")
        environment.setdefault("ZIG_GLOBAL_CACHE_DIR", str(root / "build/test-tools/zig-cache"))
        environment.setdefault("ZIG_LOCAL_CACHE_DIR", str(output_dir / "zig-cache"))
    command.extend(["-std=c99", "-O0", "-Wall", "-Wextra", "-Werror", str(generated), "-o", str(executable)])
    compiled = subprocess.run(command, cwd=root, env=environment, capture_output=True, text=True, timeout=120)
    compiler_output = compiled.stdout + compiled.stderr
    (output_dir / "battery_adc.compile.log").write_text(compiler_output, encoding="utf-8")
    if compiler_output:
        print(compiler_output, end="")
    if compiled.returncode:
        return compiled.returncode
    print(f"Source SHA-256: {hashlib.sha256(original_bytes).hexdigest()}", flush=True)
    result = subprocess.run([str(executable)], cwd=output_dir, capture_output=True, text=True, timeout=10)
    run_output = result.stdout + result.stderr
    (output_dir / "battery_adc.run.log").write_text(run_output, encoding="utf-8")
    print(run_output, end="")
    if result.returncode:
        return result.returncode
    integration_checks = check_integration(source, control_source, main_source)
    integration_output = f"battery integration: {integration_checks} checks passed\n"
    (output_dir / "battery_adc.integration.log").write_text(integration_output, encoding="utf-8")
    print(integration_output, end="")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"battery ADC: {error}", file=sys.stderr)
        sys.exit(2)
