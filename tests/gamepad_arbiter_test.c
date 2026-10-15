#include "gamepad_arbiter.h"
#include <stddef.h>
#include <stdio.h>

static unsigned int checks;
static unsigned int failures;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static const HID_GAMEPAD_Info_TypeDef neutral = {0U, 0U, 0, 0, 0, 0};
static const HID_GAMEPAD_Info_TypeDef forward = {0U, 0U, 0, 127, 0, 0};
static const HID_GAMEPAD_Info_TypeDef turn = {0U, 0U, 0, 0, -127, 0};
static const HID_GAMEPAD_Info_TypeDef combined = {0x4000U, 0U, -47, 84, 127, -18};

static void check_output(uint8_t channel,
                         const HID_GAMEPAD_Info_TypeDef *expected)
{
    HID_GAMEPAD_Info_TypeDef actual;
    unsigned int read;
    CHECK(GamepadArbiter_GetActiveChannel() == channel);
    /* Reading the state repeatedly must not consume any arming or gate. */
    for (read = 0U; read < 2U; ++read) {
        CHECK(GamepadArbiter_GetOutput(&actual) ==
              (channel != GAMEPAD_ARBITER_NO_CHANNEL));
        CHECK(actual.buttons == expected->buttons);
        CHECK(actual.hat == expected->hat);
        CHECK(actual.lx == expected->lx);
        CHECK(actual.ly == expected->ly);
        CHECK(actual.rx == expected->rx);
        CHECK(actual.ry == expected->ry);
    }
}

static void check_zero(void)
{
    check_output(GAMEPAD_ARBITER_NO_CHANNEL, &neutral);
}

static void acquire(uint8_t channel, uint32_t nowMs,
                    const HID_GAMEPAD_Info_TypeDef *gesture,
                    uint16_t rawButtons)
{
    GamepadArbiter_Update(channel, &neutral, 0U, nowMs);
    GamepadArbiter_Update(channel, gesture, rawButtons, nowMs + 1U);
    check_output(channel, gesture);
}

static void test_startup_and_exclusive_owner(void)
{
    GamepadArbiter_Reset();
    check_zero();
    GamepadArbiter_Update(0U, &forward, 0U, 0U);
    GamepadArbiter_Update(1U, &turn, 0U, 1U);
    check_zero();
    acquire(1U, 2U, &turn, 0U);
    GamepadArbiter_Update(0U, &neutral, 0U, 4U);
    GamepadArbiter_Update(0U, &combined, 0x0010U, 5U);
    check_output(1U, &turn);
    GamepadArbiter_Update(1U, &combined, 0x0010U, 6U);
    check_output(1U, &combined);
    GamepadArbiter_Update(0U, &neutral, 0U, 7U);
    check_output(1U, &combined);
    GamepadArbiter_Update(1U, &neutral, 0U, 8U);
    check_zero();
    /* The old neutral on channel 0 cannot survive the release. */
    GamepadArbiter_Update(0U, &forward, 0U, 18U);
    check_zero();
    acquire(0U, 19U, &forward, 0U);
}

static void test_first_new_gesture_wins(void)
{
    unsigned int first;
    for (first = 0U; first < 2U; ++first) {
        uint8_t winner = (uint8_t)first;
        uint8_t loser = (uint8_t)(1U - first);
        GamepadArbiter_Reset();
        GamepadArbiter_Update(0U, &neutral, 0U, 100U);
        GamepadArbiter_Update(1U, &neutral, 0U, 100U);
        GamepadArbiter_Update(winner, &forward, 0U, 101U);
        GamepadArbiter_Update(loser, &turn, 0U, 101U);
        check_output(winner, &forward);
    }
}

static void test_held_other_never_takes_over(void)
{
    unsigned int first;
    for (first = 0U; first < 2U; ++first) {
        uint8_t owner = (uint8_t)first;
        uint8_t other = (uint8_t)(1U - first);
        uint32_t time;
        GamepadArbiter_Reset();
        acquire(owner, 0U, &forward, 0U);
        GamepadArbiter_Update(other, &neutral, 0U, 2U);
        GamepadArbiter_Update(other, &turn, 0U, 3U);
        GamepadArbiter_Update(owner, &neutral, 0U, 4U);
        check_zero();
        for (time = 5U; time < 600U; ++time) {
            GamepadArbiter_Update(other, &turn, 0U, time);
            check_zero();
        }
        acquire(other, 600U, &turn, 0U);
    }
}

static void test_handoff_neutral_window(uint32_t base)
{
    uint32_t released = base + 3U;
    GamepadArbiter_Reset();
    acquire(0U, base, &forward, 0U);
    GamepadArbiter_Update(0U, &neutral, 0U, released);
    check_zero();
    /* Even neutral followed by a new gesture at the same timestamp is denied. */
    GamepadArbiter_Update(1U, &neutral, 0U, released);
    GamepadArbiter_Update(1U, &turn, 0U, released);
    check_zero();
    GamepadArbiter_Update(1U, &turn, 0U, released + 1U);
    check_zero();
    GamepadArbiter_Update(1U, &neutral, 0U, released + 8U);
    GamepadArbiter_Update(1U, &turn, 0U, released + 9U);
    check_zero();
    /* The rejected 9ms gesture must not be replayed when the window ends. */
    GamepadArbiter_Update(1U, &turn, 0U, released + 10U);
    check_zero();
    GamepadArbiter_Update(1U, &neutral, 0U, released + 10U);
    GamepadArbiter_Update(1U, &turn, 0U, released + 10U);
    check_output(1U, &turn);
    GamepadArbiter_Update(1U, &neutral, 0U, released + 11U);
    GamepadArbiter_Update(0U, &neutral, 0U, released + 20U);
    GamepadArbiter_Update(0U, &forward, 0U, released + 21U);
    check_output(0U, &forward);
}

static void set_axis(HID_GAMEPAD_Info_TypeDef *report, unsigned int axis, int value)
{
    switch (axis) {
    case 0U: report->lx = (int8_t)value; break;
    case 1U: report->ly = (int8_t)value; break;
    case 2U: report->rx = (int8_t)value; break;
    default: report->ry = (int8_t)value; break;
    }
}

static void test_every_axis_neutral_boundary(void)
{
    unsigned int axis;
    int value;
    for (axis = 0U; axis < 4U; ++axis) {
        for (value = -128; value <= 127; ++value) {
            HID_GAMEPAD_Info_TypeDef report = neutral;
            int centered = value >= -10 && value <= 10;
            set_axis(&report, axis, value);
            GamepadArbiter_Reset();
            GamepadArbiter_Update(0U, &report, 0U, 0U);
            check_zero();
            GamepadArbiter_Update(0U, &forward, 0U, 1U);
            if (centered) {
                check_output(0U, &forward);
            } else {
                check_zero();
            }
            GamepadArbiter_Reset();
            acquire(1U, 0U, &forward, 0U);
            GamepadArbiter_Update(1U, &report, 0U, 2U);
            if (centered) {
                check_zero();
            } else {
                check_output(1U, &report);
            }
        }
    }
}

static void test_hat_and_all_raw_buttons(void)
{
    unsigned int value;
    for (value = 0U; value < 256U; ++value) {
        HID_GAMEPAD_Info_TypeDef report = neutral;
        report.hat = (uint8_t)value;
        GamepadArbiter_Reset();
        GamepadArbiter_Update(0U, &report, 0U, 0U);
        GamepadArbiter_Update(0U, &forward, 0U, 1U);
        if (value == 0U) {
            check_output(0U, &forward);
        } else {
            check_zero();
        }
        GamepadArbiter_Reset();
        acquire(1U, 0U, &forward, 0U);
        GamepadArbiter_Update(1U, &report, 0U, 2U);
        if (value == 0U) {
            check_zero();
        } else {
            check_output(1U, &report);
        }
    }
    for (value = 0U; value < 16U; ++value) {
        uint16_t raw = (uint16_t)(1U << value);
        HID_GAMEPAD_Info_TypeDef mapped = neutral;
        GamepadArbiter_Reset();
        /* An unmapped face key is still a held input, not neutral. */
        GamepadArbiter_Update(0U, &neutral, raw, 0U);
        GamepadArbiter_Update(0U, &forward, 0U, 1U);
        check_zero();
        acquire(0U, 2U, &forward, 0U);
        GamepadArbiter_Update(0U, &neutral, raw, 4U);
        check_output(0U, &neutral);
        GamepadArbiter_Update(1U, &neutral, 0U, 5U);
        GamepadArbiter_Update(1U, &turn, 0U, 6U);
        check_output(0U, &neutral);
        GamepadArbiter_Update(0U, &neutral, 0U, 7U);
        check_zero();
        /* Inconsistent decoded button metadata must also fail neutral. */
        mapped.buttons = raw;
        GamepadArbiter_Reset();
        GamepadArbiter_Update(0U, &mapped, 0U, 0U);
        GamepadArbiter_Update(0U, &forward, 0U, 1U);
        check_zero();
    }
}

static void test_owner_timeout_and_wrap(uint32_t base)
{
    uint32_t owned = base + 1U;
    GamepadArbiter_Reset();
    acquire(0U, base, &forward, 0U);
    GamepadArbiter_Update(1U, &neutral, 0U, owned + 498U);
    GamepadArbiter_Tick(owned + 499U);
    check_output(0U, &forward);
    /* Other valid traffic cannot renew channel 0, including its cutoff tick. */
    GamepadArbiter_Update(1U, &turn, 0U, owned + 500U);
    check_zero();
    GamepadArbiter_Update(0U, &forward, 0U, owned + 501U);
    GamepadArbiter_Update(1U, &turn, 0U, owned + 510U);
    check_zero();
    acquire(1U, owned + 511U, &turn, 0U);

    GamepadArbiter_Reset();
    acquire(0U, base, &forward, 0U);
    /* A valid owner report before the cutoff renews only that owner. */
    GamepadArbiter_Update(0U, &combined, 0x0010U, owned + 499U);
    GamepadArbiter_Tick(owned + 998U);
    check_output(0U, &combined);
    GamepadArbiter_Tick(owned + 999U);
    check_zero();

    GamepadArbiter_Reset();
    acquire(0U, base, &forward, 0U);
    /* A nonneutral owner packet exactly at 500ms must not bypass rearming. */
    GamepadArbiter_Update(0U, &forward, 0U, owned + 500U);
    check_zero();
    GamepadArbiter_Update(0U, &forward, 0U, owned + 510U);
    check_zero();
    acquire(0U, owned + 511U, &forward, 0U);
}

static void test_idle_neutral_freshness(void)
{
    GamepadArbiter_Reset();
    GamepadArbiter_Update(0U, &neutral, 0U, 0U);
    GamepadArbiter_Update(0U, &forward, 0U, 499U);
    check_output(0U, &forward);
    GamepadArbiter_Reset();
    GamepadArbiter_Update(0U, &neutral, 0U, 0U);
    GamepadArbiter_Update(0U, &forward, 0U, 500U);
    check_zero();
    GamepadArbiter_Reset();
    GamepadArbiter_Update(0U, &neutral, 0U, UINT32_MAX - 3U);
    GamepadArbiter_Tick(496U);
    GamepadArbiter_Update(0U, &forward, 0U, 497U);
    check_zero();
}

static void test_bad_report_isolation_and_reconnect(void)
{
    unsigned int owner;
    for (owner = 0U; owner < 2U; ++owner) {
        uint8_t current = (uint8_t)owner;
        uint8_t other = (uint8_t)(1U - owner);
        GamepadArbiter_Reset();
        acquire(current, 0U, &forward, 0U);
        GamepadArbiter_Update(other, &neutral, 0U, 2U);
        GamepadArbiter_Invalidate(other, 3U);
        check_output(current, &forward);
        GamepadArbiter_Update(other, NULL, 0U, 4U);
        check_output(current, &forward);
        GamepadArbiter_Invalidate(current, 5U);
        check_zero();
        GamepadArbiter_Update(other, &turn, 0U, 15U);
        GamepadArbiter_Update(current, &forward, 0U, 15U);
        check_zero();
        acquire(other, 16U, &turn, 0U);
        GamepadArbiter_Update(other, NULL, 0U, 18U);
        check_zero();
        /* NULL input invalidates the known channel and opens the same gate. */
        GamepadArbiter_Update(current, &neutral, 0U, 19U);
        GamepadArbiter_Update(current, &forward, 0U, 27U);
        check_zero();
        GamepadArbiter_Update(current, &forward, 0U, 28U);
        check_zero();
        acquire(current, 29U, &forward, 0U);
    }
}

static void test_reset_and_invalid_arguments(void)
{
    HID_GAMEPAD_Info_TypeDef actual;
    GamepadArbiter_Reset();
    acquire(1U, 0U, &turn, 0U);
    CHECK(GamepadArbiter_GetOutput(NULL) == 0);
    check_output(1U, &turn);
    GamepadArbiter_Update(2U, &neutral, 0U, 2U);
    GamepadArbiter_Update(255U, &forward, 0U, 2U);
    GamepadArbiter_Invalidate(2U, 2U);
    GamepadArbiter_Invalidate(255U, 2U);
    check_output(1U, &turn);
    GamepadArbiter_Reset();
    check_zero();
    GamepadArbiter_Update(1U, &turn, 0U, 3U);
    check_zero();
    acquire(0U, 4U, &forward, 0U);
    GamepadArbiter_Update(0U, &neutral, 0U, 6U);
    GamepadArbiter_Reset();
    /* A USB reset clears the old release gate as well as all input. */
    acquire(1U, 6U, &turn, 0U);
    GamepadArbiter_Reset();
    actual = combined;
    CHECK(GamepadArbiter_GetOutput(&actual) == 0);
    CHECK(actual.buttons == 0U && actual.hat == 0U);
    CHECK(actual.lx == 0 && actual.ly == 0 && actual.rx == 0 && actual.ry == 0);
}

int main(void)
{
    CHECK(GAMEPAD_ARBITER_CHANNELS == 2U);
    CHECK(GAMEPAD_ARBITER_TIMEOUT_MS == 500U);
    CHECK(GAMEPAD_ARBITER_HANDOFF_MS == 10U);
    test_startup_and_exclusive_owner();
    test_first_new_gesture_wins();
    test_held_other_never_takes_over();
    test_handoff_neutral_window(100U);
    test_handoff_neutral_window(UINT32_MAX - 5U);
    test_every_axis_neutral_boundary();
    test_hat_and_all_raw_buttons();
    test_owner_timeout_and_wrap(0U);
    test_owner_timeout_and_wrap(UINT32_MAX - 20U);
    test_idle_neutral_freshness();
    test_bad_report_isolation_and_reconnect();
    test_reset_and_invalid_arguments();
    if (failures != 0U) {
        fprintf(stderr, "%u failures in %u checks\n", failures, checks);
        return 1;
    }
    printf("PASS: %u gamepad arbiter checks\n", checks);
    return 0;
}
