#!/usr/bin/env python3
"""Exercise actual USB host polling, publication callback and control consumer.

Only hardware/USB completion and globals are modeled. Production function bodies
are extracted verbatim; the real decoder, arbiter and adapter are linked. USB SOF
is deliberately frozen while an independent simulated DWT clock advances.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from control_guard_integration_test import extract_function

PRELUDE = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "usbh_hid_gamepad.h"
#include "usbh_hiwonder_hid.h"
#define MAX_LEVEL 5
#define MIN_LEVEL 1
#define TEST_CYCLES_PER_MS 84000U

static USBH_HandleTypeDef hUsbHostFS;
USBH_ClassTypeDef HIWONDER_HID_CLASS;
#define usb_class HIWONDER_HID_CLASS
static HID_HandleTypeDef hid;
HID_GAMEPAD_Info_TypeDef *info;
static struct { uint32_t CYCCNT; } test_dwt;
#define DWT (&test_dwt)
static uint32_t SystemCoreClock = 84000000U;
static float gamepad_speed, gamepad_turn;
static int velocity_multiple = 3, turn_multiple = 2;
static float turn_amplitude = 80.0f;
static struct { float middle_angle; } pidparams;
static int modify;
static unsigned int beeps, checks, failures;
static uint8_t queued_packet;
static uint8_t process_fault;
static uint8_t expect_closed_on_entry;
typedef struct Buzzer Buzzer;
struct Buzzer { void (*beep)(Buzzer *, int, int, int, int); };
static void beep_stub(Buzzer *self, int hz, int on, int off, int count)
{ (void)self; (void)hz; (void)on; (void)off; (void)count; ++beeps; }
static Buzzer buzzers[] = {{beep_stub}};
static int myabs(int value) { return value < 0 ? -value : value; }
static USBH_StatusTypeDef USBH_HID_KeybdInit(USBH_HandleTypeDef *host)
{ (void)host; return USBH_OK; }
static USBH_StatusTypeDef USBH_HID_MouseInit(USBH_HandleTypeDef *host)
{ (void)host; return USBH_OK; }
static USBH_StatusTypeDef USBH_Process(USBH_HandleTypeDef *host);
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; \
  fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); } } while (0)
'''

HARNESS = r'''
static const uint8_t neutral[9] = {0U,0U,15U,128U,128U,128U,128U,0U,0U};

static USBH_StatusTypeDef USBH_Process(USBH_HandleTypeDef *host)
{
    if (expect_closed_on_entry) {
        CHECK(info == NULL); /* Real pre-poll must expire before host work. */
        expect_closed_on_entry = 0U;
    }
    if (process_fault == 1U) {
        host->device.PortEnabled = 0U;
        host->gState = HOST_CLASS_REQUEST;
        /* Reproduce the observed inconsistent flags, without DISCONNECTION. */
        CHECK(host->device.is_connected == 1U && host->device.is_disconnected == 0U);
    } else if (process_fault == 2U) {
        DWT->CYCCNT += 600U * TEST_CYCLES_PER_MS;
    }
    process_fault = 0U;
    if (queued_packet) {
        USBH_HID_GamepadReceive(host, 9U);
        USBH_HID_EventCallback(host);
        queued_packet = 0U;
    }
    return USBH_OK;
}

static void cycle(uint32_t cycles)
{
    DWT->CYCCNT = cycles;
    MX_USB_HOST_Process();
    gamepad_scan();
}

static void reset_fixture(uint32_t cycles)
{
    memset(&hUsbHostFS, 0, sizeof(hUsbHostFS));
    memset(&usb_class, 0, sizeof(usb_class));
    memset(&hid, 0, sizeof(hid));
    hUsbHostFS.gState = HOST_CLASS;
    hUsbHostFS.device.is_connected = hUsbHostFS.device.PortEnabled = 1U;
    hUsbHostFS.device.DevDesc.idVendor = 0x20BCU;
    hUsbHostFS.device.DevDesc.idProduct = 0x5500U;
    hUsbHostFS.device.DevDesc.bcdDevice = 0x1003U;
    hUsbHostFS.pActiveClass = &usb_class;
    usb_class.pData = &hid;
    hid.length = 64U; hid.HID_Desc.wItemLength = 123U; hid.Init = USBH_HID_GamepadInit;
    hUsbHostFS.Timer = 16599U;
    CHECK(USBH_HID_GamepadInit(&hUsbHostFS) == USBH_OK);
    queued_packet = process_fault = expect_closed_on_entry = 0U;
    velocity_multiple = 3; turn_multiple = 2;
    beeps = 0U; pidparams.middle_angle = 0.0f; modify = 0;
    cycle(cycles);
    CHECK(info == NULL && gamepad_speed == 0.0f && gamepad_turn == 0.0f);
}

static void packet(uint8_t motion, uint32_t cycles)
{
    memcpy(hid.pData, neutral, 9U);
    if (motion) { hid.pData[4] = 0U; hid.pData[5] = 255U; }
    queued_packet = 1U;
    cycle(cycles);
}

static void check_motion(void)
{
    CHECK(info != NULL && gamepad_active_channel == 0U);
    CHECK(gamepad_speed == 15.0f && gamepad_turn == 160.0f);
    CHECK(modify == 0 && beeps == 0U);
}

static void check_closed(void)
{
    CHECK(info == NULL && gamepad_active_channel == 255U);
    CHECK(gamepad_speed == 0.0f && gamepad_turn == 0.0f);
    CHECK(modify == 0 && beeps == 0U);
}

static void test_frozen_sof_timeout_and_wrap(void)
{
    static const uint32_t starts[] = {0U, UINT32_MAX - 100U};
    unsigned int index;
    for (index = 0U; index < sizeof(starts)/sizeof(starts[0]); ++index) {
        uint32_t start = starts[index];
        reset_fixture(start);
        packet(0U, start);
        packet(1U, start + 1U);
        check_motion();
        cycle(start + 1U + 500U * TEST_CYCLES_PER_MS - 1U);
        check_motion();
        expect_closed_on_entry = 1U;
        cycle(start + 1U + 500U * TEST_CYCLES_PER_MS);
        check_closed();
        CHECK(hUsbHostFS.Timer == 16599U && gamepad_reports_timed_out == 1U);
        CHECK(gamepad_reports_accepted == 2U);
        hUsbHostFS.Timer += 10U; /* USB handoff clock resumes. */
        packet(1U, start + 1U + 510U * TEST_CYCLES_PER_MS);
        check_closed(); /* A held value alone never restores motion. */
        packet(0U, start + 1U + 511U * TEST_CYCLES_PER_MS);
        packet(1U, start + 1U + 512U * TEST_CYCLES_PER_MS);
        check_motion();
    }
}

static void test_fault_inside_host_process(void)
{
    reset_fixture(0U);
    packet(0U, 0U); packet(1U, 1U); check_motion();
    process_fault = 1U;
    cycle(2U);
    check_closed(); /* Real post-poll catches port loss in this iteration. */
    CHECK(gamepad_reports_timed_out == 0U);
    hUsbHostFS.device.PortEnabled = 1U; hUsbHostFS.gState = HOST_CLASS;
    hUsbHostFS.Timer += 10U;
    packet(1U, 3U); check_closed();
    packet(0U, 4U); packet(1U, 5U); check_motion();

    reset_fixture(0U);
    packet(0U, 0U); packet(1U, 1U); check_motion();
    process_fault = 2U;
    cycle(2U);
    check_closed(); /* Time can advance inside host work without SOF. */
    CHECK(gamepad_reports_timed_out == 1U);
}

static void test_foreign_and_keyboard_cannot_retain_motion(void)
{
    USBH_ClassTypeDef foreign_class;
    unsigned int kind;
    for (kind = 0U; kind < 3U; ++kind) {
        reset_fixture(0U);
        packet(0U, 0U); packet(1U, 1U); check_motion();
        if (kind == 0U) {
            foreign_class = usb_class;
            hUsbHostFS.pActiveClass = &foreign_class; /* Same nonempty pData. */
        } else {
            hid.Init = kind == 1U ? USBH_HID_KeybdInit : USBH_HID_MouseInit;
        }
        cycle(2U);
        check_closed();
        hUsbHostFS.pActiveClass = &usb_class;
        hid.Init = USBH_HID_GamepadInit;
        hUsbHostFS.Timer += 10U;
        packet(1U, 3U); check_closed();
        packet(0U, 4U); packet(1U, 5U); check_motion();
    }
}

static void test_valid_frames_refresh_only_when_delivered(void)
{
    unsigned int tick;
    reset_fixture(0U); packet(0U, 0U);
    for (tick = 1U; tick <= 2001U; tick += 40U) {
        packet(1U, tick * TEST_CYCLES_PER_MS);
        check_motion();
    }
    CHECK(gamepad_reports_timed_out == 0U && hUsbHostFS.Timer == 16599U);
    packet(0U, 2041U * TEST_CYCLES_PER_MS); check_closed();
    CHECK(gamepad_reports_rejected == 0U);
}

int main(void)
{
    test_frozen_sof_timeout_and_wrap();
    test_fault_inside_host_process();
    test_foreign_and_keyboard_cannot_retain_motion();
    test_valid_frames_refresh_only_when_delivered();
    if (failures) { fprintf(stderr, "%u failures / %u checks\n", failures, checks); return 1; }
    printf("PASS: %u gamepad safety integration checks\n", checks);
    return 0;
}
'''


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--output-dir", default="build/tests/gamepad-safety")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = (root / args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    source_paths = {
        "HIWONDER_USBH_HID_GetDeviceType": root / "Firmware/Hiwonder/src/usbh_hiwonder_hid.c",
        "MX_USB_HOST_Process": root / "Firmware/USB_HOST/App/usb_host.c",
        "USBH_HID_EventCallback": root / "Firmware/Hiwonder/src/gamepad_handle.c",
        "gamepad_scan": root / "Firmware/Hiwonder/src/control.c",
    }
    bodies = []
    for name, path in source_paths.items():
        source = path.read_text(encoding="utf-8")
        if name == "HIWONDER_USBH_HID_GetDeviceType":
            # This public type gate is the final function in the custom class.
            marker = "HID_TypeTypeDef HIWONDER_USBH_HID_GetDeviceType("
            if source.count(marker) != 1:
                raise ValueError("Expected exactly one HID type gate")
            body = source[source.index(marker):]
        else:
            body, _ = extract_function(source, name)
        bodies.append(body)
    generated = output / "gamepad_safety_actual.c"
    generated.write_text(PRELUDE + "\n\n".join(bodies) + HARNESS, encoding="utf-8")
    executable = output / "gamepad_safety_test.exe"
    cc = [args.cc, "cc"] if Path(args.cc).name.lower() in {"zig", "zig.exe"} else [args.cc]
    sources = [root / "Firmware/Hiwonder/src" / name for name in
               ("gamepad_report.c", "gamepad_arbiter.c", "usbh_hid_gamepad.c")]
    command = cc + ["-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    "-I" + str(root / "tests/stubs"),
                    "-I" + str(root / "Firmware/Hiwonder/src"),
                    *(str(path) for path in sources), str(generated), "-o", str(executable)]
    result = subprocess.run(command, capture_output=True, text=True)
    (output / "compile.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        print(result.stdout + result.stderr)
        return result.returncode
    result = subprocess.run([str(executable)], capture_output=True, text=True)
    (output / "run.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    print(result.stdout + result.stderr, end="")
    (output / "sourcefreeze.json").write_text(json.dumps({
        "sourceSha256": {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                         for path in [*source_paths.values(), *sources, Path(__file__).resolve()]},
        "command": command, "result": result.stdout.strip(), "returncode": result.returncode,
        "scope": "Actual host wrapper, callback and gamepad_scan with real adapter/arbiter/decoder; hardware stubs, no physical timing claim"
    }, indent=2) + "\n", encoding="utf-8")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
