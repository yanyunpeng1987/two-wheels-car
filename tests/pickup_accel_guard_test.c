#include "pickup_accel_guard.h"
#include <math.h>
#include <stdio.h>

#define CYCLES_PER_MS 84000U
#define CONFIRM_CYCLES (20U * CYCLES_PER_MS)
#define MAX_GAP_CYCLES (10U * CYCLES_PER_MS)
#define SAMPLE_CYCLES (4U * CYCLES_PER_MS)

static int failures;
static unsigned int checks;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static uint8_t sample(PickupAccelGuard *state, uint32_t cycles,
                      float accel_z, uint8_t valid)
{
    return pickup_accel_guard_update(state, cycles, CONFIRM_CYCLES,
                                    MAX_GAP_CYCLES, accel_z, valid);
}

static void test_single_spike(void)
{
    PickupAccelGuard state;
    uint32_t i;
    pickup_accel_guard_reset(&state);
    CHECK(sample(&state, 0U, 1.0f, 1U) == 0U);
    CHECK(sample(&state, SAMPLE_CYCLES, 2.0f, 1U) == 0U);
    for (i = 2U; i <= 10U; ++i) {
        CHECK(sample(&state, i * SAMPLE_CYCLES, 1.0f, 1U) == 0U);
    }
    CHECK(state.tracking == 0U);
}

static void test_confirmation_boundary(void)
{
    PickupAccelGuard state;
    uint32_t i;
    const uint32_t start = 123U;
    pickup_accel_guard_reset(&state);
    for (i = 0U; i < 5U; ++i) {
        CHECK(sample(&state, start + i * SAMPLE_CYCLES, 1.71f, 1U) == 0U);
    }
    CHECK(sample(&state, start + CONFIRM_CYCLES - 1U, 1.71f, 1U) == 0U);
    CHECK(sample(&state, start + CONFIRM_CYCLES, 1.71f, 1U) == 1U);
    CHECK(sample(&state, start + CONFIRM_CYCLES + SAMPLE_CYCLES,
                 1.71f, 1U) == 1U);
}

static void test_interrupted_confirmation(float interrupt_accel,
                                           uint8_t interrupt_valid)
{
    PickupAccelGuard state;
    uint32_t i;
    pickup_accel_guard_reset(&state);
    for (i = 0U; i < 5U; ++i) {
        CHECK(sample(&state, i * SAMPLE_CYCLES, 2.0f, 1U) == 0U);
    }
    CHECK(sample(&state, 5U * SAMPLE_CYCLES, interrupt_accel,
                 interrupt_valid) == 0U);
    CHECK(state.tracking == 0U);
    for (i = 6U; i < 11U; ++i) {
        CHECK(sample(&state, i * SAMPLE_CYCLES, 2.0f, 1U) == 0U);
    }
    CHECK(sample(&state, 11U * SAMPLE_CYCLES, 2.0f, 1U) == 1U);
}

static void test_gap_restart(void)
{
    PickupAccelGuard state;
    uint32_t i;
    uint32_t restart;
    pickup_accel_guard_reset(&state);
    for (i = 0U; i < 5U; ++i) {
        CHECK(sample(&state, i * SAMPLE_CYCLES, 2.0f, 1U) == 0U);
    }
    restart = 4U * SAMPLE_CYCLES + MAX_GAP_CYCLES + 1U;
    CHECK(sample(&state, restart, 2.0f, 1U) == 0U);
    CHECK(state.first_high_cycles == restart);
    for (i = 1U; i < 5U; ++i) {
        CHECK(sample(&state, restart + i * SAMPLE_CYCLES, 2.0f, 1U) == 0U);
    }
    CHECK(sample(&state, restart + CONFIRM_CYCLES, 2.0f, 1U) == 1U);

    /* Exactly the allowed gap retains the confirmation interval. */
    pickup_accel_guard_reset(&state);
    CHECK(sample(&state, 0U, 2.0f, 1U) == 0U);
    CHECK(sample(&state, MAX_GAP_CYCLES, 2.0f, 1U) == 0U);
    CHECK(sample(&state, 2U * MAX_GAP_CYCLES, 2.0f, 1U) == 1U);
}

static void test_counter_wrap(void)
{
    PickupAccelGuard state;
    uint32_t i;
    const uint32_t start = UINT32_MAX - 2U * SAMPLE_CYCLES;
    const uint32_t after_long_gap = UINT32_MAX - SAMPLE_CYCLES +
                                    MAX_GAP_CYCLES + 1U;
    pickup_accel_guard_reset(&state);
    for (i = 0U; i < 5U; ++i) {
        CHECK(sample(&state, start + i * SAMPLE_CYCLES, 2.0f, 1U) == 0U);
    }
    CHECK(sample(&state, start + CONFIRM_CYCLES, 2.0f, 1U) == 1U);

    /* A wrap must not hide an overlong gap. */
    pickup_accel_guard_reset(&state);
    CHECK(sample(&state, UINT32_MAX - SAMPLE_CYCLES, 2.0f, 1U) == 0U);
    CHECK(sample(&state, after_long_gap, 2.0f, 1U) == 0U);
    CHECK(state.first_high_cycles == after_long_gap);
}

static void test_new_run_reset(void)
{
    PickupAccelGuard state;
    uint32_t i;
    pickup_accel_guard_reset(&state);
    for (i = 0U; i < 5U; ++i) {
        CHECK(sample(&state, i * SAMPLE_CYCLES, 2.0f, 1U) == 0U);
    }
    pickup_accel_guard_reset(&state);
    CHECK(state.tracking == 0U);
    CHECK(state.first_high_cycles == 0U);
    CHECK(state.last_high_cycles == 0U);
    CHECK(sample(&state, CONFIRM_CYCLES, 2.0f, 1U) == 0U);
    CHECK(state.first_high_cycles == CONFIRM_CYCLES);
    for (i = 1U; i < 5U; ++i) {
        CHECK(sample(&state, CONFIRM_CYCLES + i * SAMPLE_CYCLES,
                     2.0f, 1U) == 0U);
    }
    CHECK(sample(&state, 2U * CONFIRM_CYCLES, 2.0f, 1U) == 1U);
    pickup_accel_guard_reset(&state);
    CHECK(sample(&state, 2U * CONFIRM_CYCLES + SAMPLE_CYCLES,
                 2.0f, 1U) == 0U);
}

static void test_strict_threshold(void)
{
    PickupAccelGuard state;
    uint32_t i;
    pickup_accel_guard_reset(&state);
    for (i = 0U; i <= 5U; ++i) {
        CHECK(sample(&state, i * SAMPLE_CYCLES, 1.7f, 1U) == 0U);
    }
    CHECK(state.tracking == 0U);
    for (i = 6U; i < 11U; ++i) {
        CHECK(sample(&state, i * SAMPLE_CYCLES, 1.7001f, 1U) == 0U);
    }
    CHECK(sample(&state, 11U * SAMPLE_CYCLES, 1.7001f, 1U) == 1U);
    CHECK(sample(&state, 12U * SAMPLE_CYCLES, 1.7f, 1U) == 0U);
    CHECK(state.tracking == 0U);
}

int main(void)
{
    test_single_spike();
    test_confirmation_boundary();
    test_interrupted_confirmation(1.0f, 1U);
    test_interrupted_confirmation(2.0f, 0U);
    test_interrupted_confirmation(NAN, 1U);
    test_gap_restart();
    test_counter_wrap();
    test_new_run_reset();
    test_strict_threshold();
    printf("pickup_accel_guard: %u checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
