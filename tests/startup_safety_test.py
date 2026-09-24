#!/usr/bin/env python3
"""Exercise the actual main.c startup gates with small native HAL stubs.

The firmware functions are extracted verbatim. These tests check failure paths,
register postconditions, and control-start ordering, not MCU electrical behavior.
Generated C, executable, and logs are written only to --output-dir.
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
        rf"(?m)^(?:static\s+)?(?:int|void|uint8_t)\s+{re.escape(name)}"
        rf"\s*\([^;{{}}]*\)\s*\{{"
    )
    matches = list(pattern.finditer(source))
    if len(matches) != 1:
        raise ValueError(f"Expected one definition of {name}; got {len(matches)}")
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


def without_comments(source: str) -> str:
    return TOKENS.sub(
        lambda match: " " if match.group().startswith(("/*", "//")) else match.group(),
        source,
    )


def check_integration(main_source: str, gpio_source: str, adc_source: str, ccd_source: str) -> int:
    main_body = without_comments(extract_function(main_source, "main")[0])
    gpio_body = without_comments(extract_function(gpio_source, "MX_GPIO_Init")[0])
    checks = 0

    def require(condition: bool, message: str) -> None:
        nonlocal checks
        checks += 1
        if not condition:
            raise ValueError(f"Startup integration: {message}")

    enable = list(re.finditer(r"\bstartup_enable_control\s*\(\s*\)", main_body))
    require(len(enable) == 1, "main must enable control exactly once")
    flash = list(re.finditer(r"\bstartup_flash_read_only\s*\(\s*\)", main_body))
    require(len(flash) == 1, "main must establish read-only Flash exactly once")
    require(flash[0].start() < main_body.index("HAL_Init("),
            "read-only Flash gate must run before HAL_Init")
    require(bool(re.search(
        r"if\s*\(\s*(?:!\s*startup_flash_read_only\s*\(\s*\)|"
        r"startup_flash_read_only\s*\(\s*\)\s*==\s*0U?)\s*\)\s*"
        r"\{?\s*Error_Handler\s*\(\s*\)", main_body)),
        "Flash-gate failure must enter Error_Handler")
    require("HAL_NVIC_EnableIRQ(EXTI2_IRQn)" not in re.sub(r"\s+", "", gpio_body),
            "MX_GPIO_Init must not enable EXTI2 early")
    require(not re.search(r"HAL_NVIC_EnableIRQ\s*\(\s*EXTI2_IRQn\s*\)", main_body),
            "main must enable EXTI2 through the startup gate")
    require(not re.search(r"\bHAL_ADC_Start_DMA\s*\(", main_body),
            "main must not start ADC DMA without a linked DMA handle")
    for label, adc_user in (("battery", adc_source), ("CCD", ccd_source)):
        require(bool(re.search(r"\bHAL_ADC_PollForConversion\s*\(\s*&hadc1\s*,",
                               without_comments(adc_user))),
                f"{label} acquisition must retain its independent ADC polling")
    qmi_guard = re.search(
        r"if\s*\(\s*qmi8658_app_init\s*\([^;{}]*?\)\s*!=\s*IMU_OK\s*\)"
        r"\s*\{?\s*Error_Handler\s*\(\s*\)", main_body,
    )
    require(qmi_guard is not None, "IMU initialization failure must enter Error_Handler")
    require(qmi_guard.end() < enable[0].start(), "IMU validation must precede control start")
    for operation in (
        "MX_TIM2_Init", "MX_TIM3_Init", "MX_TIM4_Init", "MX_USB_HOST_Init",
        "Lidar_Init", "bluetooth_init", "display_manager_init", "LineFollowIIC_init",
        "ultrasound_init", "ccd_init", "WonderMind_Init", "Read_PID_From_Flash",
        "Write_PID_To_Flash",
    ):
        call = re.search(rf"\b{operation}\s*\(", main_body)
        require(call is not None and call.start() < enable[0].start(),
                f"{operation} must precede control start")
    require(enable[0].start() < main_body.index("while (1)"),
            "control gate must run before the main loop")
    return checks


PRELUDE = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned int checks, failures;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
#define FLASH_CR_PG (1U << 0)
#define FLASH_CR_LOCK (1U << 31)
#define FLASH_CR_PSIZE (3U << 8)
typedef struct { uint32_t CR; } FlashStub;
static FlashStub flash_registers;
#define FLASH (&flash_registers)

enum Event { UNLOCK, CLEAR_PG, LOCK, PWM_ZERO, READ_2, READ_3,
             CLEAR_EXTI, CLEAR_NVIC, READ_PIN, SOFTWARE_IRQ, ENABLE_IRQ };
static enum Event events[32];
static unsigned int event_count, register_writes;
static HAL_StatusTypeDef unlock_result, lock_result;
static unsigned int ignore_pg_clear, ignore_lock;
static void event(enum Event item)
{
    if (event_count < sizeof(events) / sizeof(events[0])) events[event_count++] = item;
    else CHECK(0);
}

static HAL_StatusTypeDef HAL_FLASH_Unlock(void)
{
    event(UNLOCK);
    if (unlock_result == HAL_OK) {
        FLASH->CR &= ~FLASH_CR_LOCK;
        ++register_writes;
    }
    return unlock_result;
}
static void clear_bit(uint32_t *target, uint32_t mask)
{
    event(CLEAR_PG);
    CHECK(target == &FLASH->CR);
    CHECK(mask == FLASH_CR_PG);
    CHECK((FLASH->CR & FLASH_CR_LOCK) == 0U);
    if (!ignore_pg_clear) *target &= ~mask;
    ++register_writes;
}
#define CLEAR_BIT(register_, mask_) clear_bit(&(register_), (mask_))
static HAL_StatusTypeDef HAL_FLASH_Lock(void)
{
    event(LOCK);
    if (lock_result == HAL_OK && !ignore_lock) {
        FLASH->CR |= FLASH_CR_LOCK;
        ++register_writes;
    }
    return lock_result;
}

#define IMU_INT2_Pin 4U
#define IMU_INT2_GPIO_Port ((void *)0x1234U)
#define EXTI2_IRQn 8
static volatile uint8_t flag_move;
static int pwm_left, pwm_right, encoder_2, encoder_3;
static unsigned int exti_pending, nvic_pending, nvic_enabled;
static GPIO_PinState pin_state;
static void check_outputs_safe(void)
{
    CHECK(flag_move == 0U);
    CHECK(pwm_left == 0 && pwm_right == 0);
    CHECK(encoder_2 == 0 && encoder_3 == 0);
}
static void set_pwm(int left, int right)
{
    event(PWM_ZERO);
    CHECK(flag_move == 0U);
    CHECK(left == 0 && right == 0);
    pwm_left = left;
    pwm_right = right;
}
static int read_encoder(int timer)
{
    int result;
    CHECK(pwm_left == 0 && pwm_right == 0 && flag_move == 0U);
    CHECK(timer == 2 || timer == 3);
    if (timer == 2) { event(READ_2); result = encoder_2; encoder_2 = 0; }
    else { event(READ_3); result = encoder_3; encoder_3 = 0; }
    return result;
}
static void clear_exti(uint32_t pin)
{
    event(CLEAR_EXTI);
    CHECK(pin == IMU_INT2_Pin);
    CHECK(!nvic_enabled);
    check_outputs_safe();
    exti_pending = 0U;
}
#define __HAL_GPIO_EXTI_CLEAR_IT(pin_) clear_exti(pin_)
static void HAL_NVIC_ClearPendingIRQ(int irq)
{
    event(CLEAR_NVIC);
    CHECK(irq == EXTI2_IRQn);
    CHECK(!exti_pending && !nvic_enabled);
    nvic_pending = 0U;
}
static GPIO_PinState HAL_GPIO_ReadPin(void *port, uint32_t pin)
{
    event(READ_PIN);
    CHECK(port == IMU_INT2_GPIO_Port && pin == IMU_INT2_Pin);
    CHECK(!nvic_enabled);
    return pin_state;
}
static void software_irq(uint32_t pin)
{
    event(SOFTWARE_IRQ);
    CHECK(pin == IMU_INT2_Pin && pin_state == GPIO_PIN_SET);
    CHECK(!exti_pending && !nvic_pending && !nvic_enabled);
    exti_pending = nvic_pending = 1U;
}
#define __HAL_GPIO_EXTI_GENERATE_SWIT(pin_) software_irq(pin_)
static void HAL_NVIC_EnableIRQ(int irq)
{
    event(ENABLE_IRQ);
    CHECK(irq == EXTI2_IRQn && !nvic_enabled);
    check_outputs_safe();
    nvic_enabled = 1U;
}
'''


HARNESS = r'''
static void reset_flash(uint32_t value)
{
    FLASH->CR = value;
    unlock_result = lock_result = HAL_OK;
    ignore_pg_clear = ignore_lock = 0U;
    event_count = register_writes = 0U;
}
static void check_events(const enum Event *expected, unsigned int count)
{
    unsigned int i;
    CHECK(event_count == count);
    for (i = 0; i < count && i < event_count; ++i) CHECK(events[i] == expected[i]);
}
static void test_flash_postconditions(void)
{
    unsigned int psize, initially_locked, initially_programming;
    const enum Event expected[] = { UNLOCK, CLEAR_PG, LOCK };
    for (psize = 0U; psize < 4U; ++psize) {
        for (initially_locked = 0U; initially_locked < 2U; ++initially_locked) {
            for (initially_programming = 0U; initially_programming < 2U; ++initially_programming) {
                const uint32_t other = (psize << 8) | (1U << 24);
                reset_flash(other | (initially_locked ? FLASH_CR_LOCK : 0U) |
                            (initially_programming ? FLASH_CR_PG : 0U));
                CHECK(startup_flash_read_only() == 1U);
                CHECK(FLASH->CR == (other | FLASH_CR_LOCK));
                CHECK((FLASH->CR & FLASH_CR_PSIZE) == (psize << 8));
                check_events(expected, sizeof(expected) / sizeof(expected[0]));
            }
        }
    }
}
static void test_flash_failures(void)
{
    const uint32_t initial = FLASH_CR_LOCK | FLASH_CR_PG | (2U << 8);
    reset_flash(initial);
    unlock_result = HAL_ERROR;
    CHECK(startup_flash_read_only() == 0U);
    CHECK(FLASH->CR == initial);
    CHECK(register_writes == 0U && event_count == 1U && events[0] == UNLOCK);

    reset_flash(initial);
    lock_result = HAL_ERROR;
    CHECK(startup_flash_read_only() == 0U);
    CHECK(!(FLASH->CR & FLASH_CR_PG));
    CHECK(event_count == 3U && events[2] == LOCK);

    /* A HAL success status alone cannot establish the hardware postcondition. */
    reset_flash(initial);
    ignore_lock = 1U;
    CHECK(startup_flash_read_only() == 0U);
    CHECK(!(FLASH->CR & FLASH_CR_LOCK));
    reset_flash(initial);
    ignore_pg_clear = 1U;
    CHECK(startup_flash_read_only() == 0U);
    CHECK((FLASH->CR & (FLASH_CR_PG | FLASH_CR_LOCK)) == (FLASH_CR_PG | FLASH_CR_LOCK));
}
static void test_control_start(GPIO_PinState level)
{
    const enum Event low[] = { PWM_ZERO, READ_2, READ_3, CLEAR_EXTI, CLEAR_NVIC, READ_PIN, ENABLE_IRQ };
    const enum Event high[] = { PWM_ZERO, READ_2, READ_3, CLEAR_EXTI, CLEAR_NVIC, READ_PIN, SOFTWARE_IRQ, ENABLE_IRQ };
    event_count = 0U;
    flag_move = 1U;
    pwm_left = 3200;
    pwm_right = -2100;
    encoder_2 = 8;
    encoder_3 = -95;
    exti_pending = nvic_pending = 1U;
    nvic_enabled = 0U;
    pin_state = level;
    startup_enable_control();
    CHECK(nvic_enabled == 1U);
    check_outputs_safe();
    if (level == GPIO_PIN_SET) {
        CHECK(exti_pending == 1U && nvic_pending == 1U);
        check_events(high, sizeof(high) / sizeof(high[0]));
    } else {
        CHECK(exti_pending == 0U && nvic_pending == 0U);
        check_events(low, sizeof(low) / sizeof(low[0]));
    }
}
int main(void)
{
    test_flash_postconditions();
    test_flash_failures();
    test_control_start(GPIO_PIN_RESET);
    test_control_start(GPIO_PIN_SET);
    printf("startup safety: %u checks, %u failures\n", checks, failures);
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
    original = root / "Firmware/Core/Src/main.c"
    original_bytes = original.read_bytes()
    source = original_bytes.decode("utf-8-sig")
    gpio_source = (root / "Firmware/Core/Src/gpio.c").read_text(encoding="utf-8-sig")
    adc_source = (root / "Firmware/Core/Src/adc.c").read_text(encoding="utf-8-sig")
    ccd_source = (root / "Firmware/BSP/ccd.c").read_text(encoding="utf-8-sig")
    integration_checks = check_integration(source, gpio_source, adc_source, ccd_source)
    pieces = [PRELUDE]
    for name in ("startup_flash_read_only", "startup_enable_control"):
        body, line = extract_function(source, name)
        pieces.append(f'#line {line} "{original.as_posix()}"\n{body}')
    pieces.extend(['#line 1 "startup_safety_harness.c"', HARNESS])
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    generated = output_dir / "startup_safety.generated.c"
    executable = output_dir / "startup_safety_test.exe"
    generated.write_text("\n\n".join(pieces), encoding="utf-8", newline="\n")
    compiler = shutil.which(args.cc)
    if compiler is None:
        compiler = str(Path(args.cc).resolve())
    command = [compiler]
    if args.zig or Path(compiler).stem.lower() == "zig":
        command.append("cc")
    command.extend(["-std=c99", "-O0", "-Wall", "-Wextra", "-Werror", str(generated), "-o", str(executable)])
    compiled = subprocess.run(command, cwd=root, capture_output=True, text=True)
    compiler_output = compiled.stdout + compiled.stderr
    (output_dir / "startup_safety.compile.log").write_text(compiler_output, encoding="utf-8")
    if compiler_output:
        print(compiler_output, end="")
    if compiled.returncode:
        return compiled.returncode
    print(f"Source SHA-256: {hashlib.sha256(original_bytes).hexdigest()}", flush=True)
    print(f"startup integration: {integration_checks} checks passed", flush=True)
    result = subprocess.run([str(executable)], cwd=output_dir, capture_output=True, text=True)
    run_output = result.stdout + result.stderr
    (output_dir / "startup_safety.run.log").write_text(run_output, encoding="utf-8")
    print(run_output, end="")
    return result.returncode


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"startup safety: {error}", file=sys.stderr)
        sys.exit(2)
