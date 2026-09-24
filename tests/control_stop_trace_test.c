#include "control_stop_trace.h"
#include <stdio.h>
#include <string.h>

static int failures;
static unsigned int checks;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static ControlStopSample make_sample(uint32_t cycles, uint8_t allowed,
                                      uint8_t reason)
{
    ControlStopSample sample;
    memset(&sample, 0, sizeof(sample));
    sample.cycles = cycles;
    sample.interval_cycles = 336000U;
    sample.accel_z = 1.2f;
    sample.angle = 0.25f;
    sample.velocity_left = 1.5f;
    sample.velocity_right = -2.5f;
    sample.voltage = 11.8f;
    sample.encoder_left = 12;
    sample.encoder_right = -13;
    sample.pwm_left = 101;
    sample.pwm_right = -102;
    sample.imu_valid = 1U;
    sample.key_pressed = 0U;
    sample.flag_move = allowed;
    sample.output_allowed = allowed;
    sample.reason_mask = reason;
    return sample;
}

static void test_startup_disabled_and_zero_pwm(void)
{
    volatile ControlStopTrace trace;
    ControlStopSample sample = make_sample(0U, 0U, CONTROL_STOP_NONE);
    control_stop_trace_reset(&trace);
    control_stop_trace_update(&trace, &sample);
    CHECK(trace.frozen == 0U);
    CHECK(trace.was_allowed == 0U);
    CHECK(trace.count == 1U);

    sample = make_sample(1U, 1U, CONTROL_STOP_NONE);
    sample.pwm_left = 0;
    sample.pwm_right = 0;
    control_stop_trace_update(&trace, &sample);
    sample.cycles = 2U;
    control_stop_trace_update(&trace, &sample);
    CHECK(trace.frozen == 0U);
    CHECK(trace.was_allowed == 1U);
    CHECK(trace.count == 3U);
    CHECK(trace.samples[2].pwm_left == 0);
    CHECK(trace.samples[2].pwm_right == 0);
}

static void test_first_cause_survives_later_samples_and_restart(void)
{
    volatile ControlStopTrace trace;
    ControlStopSample sample = make_sample(10U, 1U, CONTROL_STOP_NONE);
    control_stop_trace_reset(&trace);
    control_stop_trace_update(&trace, &sample);
    sample = make_sample(11U, 0U, CONTROL_STOP_ANG);
    sample.flag_move = 1U;
    sample.angle = 81.0f;
    control_stop_trace_update(&trace, &sample);
    CHECK(trace.frozen == 1U);
    CHECK(trace.was_allowed == 0U);
    CHECK(trace.trigger.reason_mask == CONTROL_STOP_ANG);
    CHECK(trace.trigger.cycles == 11U);
    CHECK(trace.trigger.flag_move == 1U);
    CHECK(trace.trigger.output_allowed == 0U);
    CHECK(trace.trigger.angle == 81.0f);
    CHECK(trace.trigger.interval_cycles == sample.interval_cycles);
    CHECK(trace.trigger.accel_z == sample.accel_z);
    CHECK(trace.trigger.velocity_left == sample.velocity_left);
    CHECK(trace.trigger.velocity_right == sample.velocity_right);
    CHECK(trace.trigger.voltage == sample.voltage);
    CHECK(trace.trigger.encoder_left == sample.encoder_left);
    CHECK(trace.trigger.encoder_right == sample.encoder_right);
    CHECK(trace.trigger.pwm_left == sample.pwm_left);
    CHECK(trace.trigger.pwm_right == sample.pwm_right);
    CHECK(trace.trigger.imu_valid == sample.imu_valid);
    CHECK(trace.trigger.key_pressed == sample.key_pressed);
    CHECK(trace.count == 2U);
    CHECK(trace.write_index == 2U);
    CHECK(trace.samples[1].reason_mask == CONTROL_STOP_ANG);

    sample = make_sample(12U, 0U, CONTROL_STOP_ACC);
    sample.accel_z = 2.5f;
    control_stop_trace_update(&trace, &sample);
    CHECK(trace.trigger.reason_mask == CONTROL_STOP_ANG);
    CHECK(trace.trigger.cycles == 11U);
    CHECK(trace.count == 2U);
    CHECK(trace.write_index == 2U);
    CHECK(trace.samples[1].cycles == 11U);

    sample = make_sample(13U, 1U, CONTROL_STOP_NONE);
    control_stop_trace_update(&trace, &sample);
    CHECK(trace.was_allowed == 1U);
    CHECK(trace.frozen == 1U);
    CHECK(trace.trigger.cycles == 11U);
    sample = make_sample(14U, 0U, CONTROL_STOP_SPD);
    control_stop_trace_update(&trace, &sample);
    CHECK(trace.was_allowed == 0U);
    CHECK(trace.trigger.reason_mask == CONTROL_STOP_ANG);
    CHECK(trace.trigger.cycles == 11U);
    CHECK(trace.count == 2U);
    CHECK(trace.write_index == 2U);
}

static void test_explicit_reset(void)
{
    volatile ControlStopTrace trace;
    ControlStopSample sample = make_sample(20U, 1U, CONTROL_STOP_NONE);
    control_stop_trace_reset(&trace);
    control_stop_trace_update(&trace, &sample);
    sample = make_sample(21U, 0U, CONTROL_STOP_ACC);
    control_stop_trace_update(&trace, &sample);
    CHECK(trace.frozen == 1U);

    control_stop_trace_reset(&trace);
    CHECK(trace.frozen == 0U);
    CHECK(trace.was_allowed == 0U);
    CHECK(trace.count == 0U);
    CHECK(trace.write_index == 0U);
    /* Reset deliberately avoids clearing the data buffer or old trigger. */
    CHECK(trace.samples[0].cycles == 20U);
    CHECK(trace.samples[1].cycles == 21U);
    CHECK(trace.trigger.cycles == 21U);
    sample = make_sample(22U, 0U, CONTROL_STOP_ACC);
    control_stop_trace_update(&trace, &sample);
    CHECK(trace.frozen == 0U);
    sample = make_sample(23U, 1U, CONTROL_STOP_NONE);
    control_stop_trace_update(&trace, &sample);
    sample = make_sample(24U, 0U, CONTROL_STOP_KEY);
    sample.key_pressed = 1U;
    control_stop_trace_update(&trace, &sample);
    CHECK(trace.frozen == 1U);
    CHECK(trace.trigger.reason_mask == CONTROL_STOP_KEY);
    CHECK(trace.trigger.key_pressed == 1U);
    CHECK(trace.trigger.cycles == 24U);
    CHECK(trace.count == 3U);
}

static void test_ring_capacity_and_trigger_inclusion(void)
{
    volatile ControlStopTrace trace;
    ControlStopSample sample;
    uint32_t i;
    control_stop_trace_reset(&trace);
    for (i = 0U; i < 80U; ++i) {
        sample = make_sample(i, 1U, CONTROL_STOP_NONE);
        control_stop_trace_update(&trace, &sample);
    }
    CHECK(trace.count == CONTROL_STOP_TRACE_CAPACITY);
    CHECK(trace.write_index == 16U);
    CHECK(trace.frozen == 0U);
    for (i = 0U; i < CONTROL_STOP_TRACE_CAPACITY; ++i) {
        CHECK(trace.samples[(16U + i) % CONTROL_STOP_TRACE_CAPACITY].cycles ==
              16U + i);
    }
    sample = make_sample(80U, 0U, CONTROL_STOP_SPD);
    control_stop_trace_update(&trace, &sample);
    CHECK(trace.frozen == 1U);
    CHECK(trace.count == CONTROL_STOP_TRACE_CAPACITY);
    CHECK(trace.write_index == 17U);
    CHECK(trace.samples[16].cycles == 80U);
    CHECK(trace.samples[16].reason_mask == CONTROL_STOP_SPD);
    CHECK(trace.trigger.cycles == 80U);
    for (i = 0U; i < CONTROL_STOP_TRACE_CAPACITY; ++i) {
        CHECK(trace.samples[(17U + i) % CONTROL_STOP_TRACE_CAPACITY].cycles ==
              17U + i);
    }
}

static void test_reason_masks(void)
{
    volatile ControlStopTrace trace;
    ControlStopSample sample;
    const uint8_t combined = CONTROL_STOP_ACC | CONTROL_STOP_SPD |
                              CONTROL_STOP_KEY | CONTROL_STOP_ANG;
    control_stop_trace_reset(&trace);
    sample = make_sample(30U, 1U, CONTROL_STOP_NONE);
    control_stop_trace_update(&trace, &sample);
    sample = make_sample(31U, 0U, CONTROL_STOP_NONE);
    control_stop_trace_update(&trace, &sample);
    CHECK(trace.frozen == 1U);
    CHECK(trace.trigger.reason_mask == CONTROL_STOP_UNKNOWN);
    CHECK(trace.samples[1].reason_mask == CONTROL_STOP_NONE);

    control_stop_trace_reset(&trace);
    sample = make_sample(32U, 1U, CONTROL_STOP_NONE);
    control_stop_trace_update(&trace, &sample);
    sample = make_sample(33U, 0U, combined);
    control_stop_trace_update(&trace, &sample);
    CHECK(trace.frozen == 1U);
    CHECK(trace.trigger.reason_mask == combined);
    CHECK(trace.samples[1].reason_mask == combined);
}

int main(void)
{
    test_startup_disabled_and_zero_pwm();
    test_first_cause_survives_later_samples_and_restart();
    test_explicit_reset();
    test_ring_capacity_and_trigger_inclusion();
    test_reason_masks();
    printf("control_stop_trace: %u checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
