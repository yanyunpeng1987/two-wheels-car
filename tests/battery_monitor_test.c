#include "battery_monitor.h"
#include <stdio.h>

#define CYCLES_PER_MS 84000U

static unsigned int checks;
static unsigned int failures;
static uint32_t origin;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static uint32_t cycles(uint32_t ms)
{
    return origin + ms * CYCLES_PER_MS;
}

static void sample(BatteryMonitor *state, uint32_t ms, int32_t raw_cv)
{
    battery_monitor_sample(state, cycles(ms), raw_cv, 1U);
}

static void normal(BatteryMonitor *state)
{
    battery_monitor_init(state, CYCLES_PER_MS);
    sample(state, 0U, 1190);
    sample(state, 100U, 1190);
    sample(state, 200U, 1190);
    CHECK(state->ready == 1U);
    CHECK(state->filtered_cv == 1190);
}

static void low(BatteryMonitor *state)
{
    uint32_t ms;
    battery_monitor_init(state, CYCLES_PER_MS);
    for (ms = 0U; ms <= 1200U; ms += 100U) {
        sample(state, ms, 850);
        CHECK(state->low_active == (uint8_t)(ms == 1200U));
    }
    CHECK(state->alarm_episode_count == 1U);
}

static void test_warmup_and_median(void)
{
    static const int32_t values[6][3] = {
        {800, 1000, 1200}, {800, 1200, 1000},
        {1000, 800, 1200}, {1000, 1200, 800},
        {1200, 800, 1000}, {1200, 1000, 800}
    };
    BatteryMonitor state;
    unsigned int i;
    for (i = 0U; i < 6U; ++i) {
        battery_monitor_init(&state, CYCLES_PER_MS);
        CHECK(state.stale == 1U);
        CHECK(state.ready == 0U);
        CHECK(battery_monitor_alarm_due(&state, cycles(0U)) == 0U);
        sample(&state, 0U, values[i][0]);
        CHECK(state.ready == 0U);
        CHECK(state.stale == 0U);
        sample(&state, 100U, values[i][1]);
        CHECK(state.ready == 0U);
        sample(&state, 200U, values[i][2]);
        CHECK(state.ready == 1U);
        CHECK(state.filtered_cv == 1000);
        CHECK(state.min_raw_cv == 800);
        CHECK(state.max_raw_cv == 1200);
        CHECK(state.sample_count == 3U);
        CHECK(state.low_raw_count == 1U);
    }
}

static void test_one_and_two_spikes(void)
{
    BatteryMonitor state;
    uint32_t ms;
    normal(&state);
    sample(&state, 300U, 600);
    CHECK(state.filtered_cv == 1190);
    CHECK(state.low_pending == 0U);
    sample(&state, 400U, 1190);
    sample(&state, 500U, 1190);
    CHECK(state.filtered_cv == 1190);
    sample(&state, 600U, 700);
    sample(&state, 700U, 700);
    CHECK(state.filtered_cv == 700);
    CHECK(state.low_pending == 1U);
    sample(&state, 800U, 1190);
    CHECK(state.filtered_cv == 700);
    sample(&state, 900U, 1190);
    CHECK(state.filtered_cv == 1190);
    CHECK(state.low_pending == 0U);
    for (ms = 1000U; ms <= 5000U; ms += 100U) {
        sample(&state, ms, 1190);
        CHECK(state.low_active == 0U);
        CHECK(battery_monitor_alarm_due(&state, cycles(ms)) == 0U);
    }
    CHECK(state.low_raw_count == 3U);
    CHECK(state.alarm_count == 0U);
}

static void test_confirmation_and_alarm_interval(void)
{
    BatteryMonitor state;
    uint32_t ms;
    battery_monitor_init(&state, CYCLES_PER_MS);
    sample(&state, 0U, 850);
    sample(&state, 100U, 850);
    sample(&state, 200U, 850);
    CHECK(state.low_pending == 1U);
    CHECK(state.low_since_cycles == cycles(200U));
    for (ms = 500U; ms <= 1100U; ms += 300U) {
        sample(&state, ms, 850);
        CHECK(state.low_active == 0U);
    }
    sample(&state, 1199U, 850);
    CHECK(state.low_active == 0U);
    sample(&state, 1200U, 850);
    CHECK(state.low_active == 1U);
    CHECK(battery_monitor_alarm_due(&state, cycles(1200U)) == 1U);
    CHECK(battery_monitor_alarm_due(&state, cycles(1200U)) == 0U);
    CHECK(state.last_alarm.cycles == cycles(1200U));
    CHECK(state.last_alarm.raw_cv == 850);
    CHECK(state.last_alarm.filtered_cv == 850);
    CHECK(state.last_alarm.min_raw_cv == 850);
    for (ms = 1300U; ms <= 2100U; ms += 100U) {
        sample(&state, ms, 850);
        CHECK(battery_monitor_alarm_due(&state, cycles(ms)) == 0U);
    }
    sample(&state, 2199U, 850);
    CHECK(battery_monitor_alarm_due(&state, cycles(2199U)) == 0U);
    sample(&state, 2200U, 850);
    CHECK(battery_monitor_alarm_due(&state, cycles(2200U)) == 1U);
    CHECK(state.alarm_count == 2U);
    CHECK(state.alarm_episode_count == 1U);
}

static void test_threshold_boundaries_and_hysteresis(void)
{
    BatteryMonitor state;
    uint32_t ms;
    battery_monitor_init(&state, CYCLES_PER_MS);
    for (ms = 0U; ms <= 2000U; ms += 100U) {
        sample(&state, ms, 900);
        CHECK(state.low_active == 0U);
        CHECK(state.low_pending == 0U);
    }
    CHECK(state.low_raw_count == 0U);
    for (ms = 2100U; ms <= 3200U; ms += 100U) {
        sample(&state, ms, 899);
    }
    CHECK(state.low_active == 1U);
    for (ms = 3300U; ms <= 5000U; ms += 100U) {
        sample(&state, ms, 949);
        CHECK(state.low_active == 1U);
        CHECK(state.recovery_pending == 0U);
    }
    sample(&state, 5100U, 950);
    sample(&state, 5200U, 950);
    CHECK(state.recovery_pending == 1U);
    CHECK(state.recovery_since_cycles == cycles(5200U));
    for (ms = 5300U; ms <= 6100U; ms += 100U) {
        sample(&state, ms, 950);
        CHECK(state.low_active == 1U);
    }
    sample(&state, 6199U, 950);
    CHECK(state.low_active == 1U);
    sample(&state, 6200U, 950);
    CHECK(state.low_active == 0U);
    CHECK(battery_monitor_alarm_due(&state, cycles(6200U)) == 0U);
    for (ms = 6300U; ms <= 7400U; ms += 100U) {
        sample(&state, ms, 899);
    }
    CHECK(state.low_active == 1U);
    CHECK(state.alarm_episode_count == 2U);
    CHECK(battery_monitor_alarm_due(&state, cycles(7400U)) == 1U);
}

static void test_range_and_adc_failures(void)
{
    BatteryMonitor state;
    normal(&state);
    sample(&state, 300U, 500);
    CHECK(state.range_error_count == 1U);
    CHECK(state.raw_cv == 500);
    CHECK(state.min_raw_cv == 1190);
    CHECK(state.ready == 0U);
    CHECK(state.low_raw_count == 1U);
    sample(&state, 400U, 501);
    sample(&state, 500U, 501);
    CHECK(state.ready == 0U);
    sample(&state, 600U, 501);
    CHECK(state.ready == 1U);
    CHECK(state.filtered_cv == 501);
    CHECK(state.min_raw_cv == 501);
    sample(&state, 700U, 3630);
    CHECK(state.range_error_count == 1U);
    CHECK(state.max_raw_cv == 3630);
    sample(&state, 800U, 3631);
    CHECK(state.range_error_count == 2U);
    CHECK(state.ready == 0U);
    CHECK(state.max_raw_cv == 3630);
    battery_monitor_sample(&state, cycles(900U), 700, 0U);
    CHECK(state.adc_error_count == 1U);
    CHECK(state.low_raw_count == 4U);
    CHECK(state.raw_cv == 700);
    CHECK(state.history[8].valid == 0U);
    CHECK(state.history_count == 10U);
    sample(&state, 1000U, -100);
    sample(&state, 1100U, 0);
    CHECK(state.range_error_count == 4U);
    CHECK(state.min_raw_cv == 501);
    CHECK(state.low_active == 0U);
}

static void test_invalid_interrupts_confirmation_and_recovery(void)
{
    BatteryMonitor state;
    uint32_t ms;
    battery_monitor_init(&state, CYCLES_PER_MS);
    for (ms = 0U; ms <= 800U; ms += 100U) {
        sample(&state, ms, 850);
    }
    battery_monitor_sample(&state, cycles(900U), 850, 0U);
    CHECK(state.low_pending == 0U);
    CHECK(state.ready == 0U);
    for (ms = 1000U; ms <= 2100U; ms += 100U) {
        sample(&state, ms, 850);
        CHECK(state.low_active == 0U);
    }
    sample(&state, 2200U, 850);
    CHECK(state.low_active == 1U);
    CHECK(battery_monitor_alarm_due(&state, cycles(2200U)) == 1U);
    battery_monitor_sample(&state, cycles(2300U), 1190, 0U);
    CHECK(state.low_active == 1U);
    CHECK(battery_monitor_alarm_due(&state, cycles(2300U)) == 0U);
    for (ms = 2400U; ms <= 3300U; ms += 100U) {
        sample(&state, ms, 1190);
        CHECK(state.low_active == 1U);
    }
    CHECK(state.recovery_pending == 1U);
    sample(&state, 3400U, 500);
    CHECK(state.low_active == 1U);
    CHECK(state.recovery_pending == 0U);
    CHECK(state.ready == 0U);
    for (ms = 3500U; ms <= 4600U; ms += 100U) {
        sample(&state, ms, 1190);
        CHECK(state.low_active == 1U);
    }
    sample(&state, 4700U, 1190);
    CHECK(state.low_active == 0U);
    CHECK(state.alarm_episode_count == 1U);
}

static void test_gap_boundaries(void)
{
    BatteryMonitor state;
    battery_monitor_init(&state, CYCLES_PER_MS);
    sample(&state, 0U, 850);
    sample(&state, 100U, 850);
    sample(&state, 200U, 850);
    sample(&state, 550U, 850);
    CHECK(state.ready == 1U);
    CHECK(state.low_since_cycles == cycles(200U));
    sample(&state, 900U, 850);
    sample(&state, 1200U, 850);
    CHECK(state.low_active == 1U);
    sample(&state, 1551U, 1190);
    CHECK(state.ready == 0U);
    CHECK(state.median_count == 1U);
    CHECK(state.low_active == 1U);
    sample(&state, 1651U, 1190);
    CHECK(state.ready == 0U);
    sample(&state, 1751U, 1190);
    CHECK(state.ready == 1U);
    CHECK(state.recovery_since_cycles == cycles(1751U));
    normal(&state);
    sample(&state, 551U, 850);
    CHECK(state.ready == 0U);
    CHECK(state.low_pending == 0U);
    sample(&state, 651U, 850);
    sample(&state, 751U, 850);
    CHECK(state.low_since_cycles == cycles(751U));
}

static void test_stale_boundaries_and_retained_history(void)
{
    BatteryMonitor state;
    uint32_t old_count;
    low(&state);
    battery_monitor_tick(&state, cycles(1700U));
    CHECK(state.stale == 0U);
    CHECK(state.ready == 1U);
    old_count = state.history_count;
    battery_monitor_tick(&state, cycles(1701U));
    CHECK(state.stale == 1U);
    CHECK(state.ready == 0U);
    CHECK(state.low_active == 1U);
    CHECK(state.history_count == old_count);
    CHECK(battery_monitor_alarm_due(&state, cycles(1701U)) == 0U);
    battery_monitor_tick(&state, cycles(1701U));
    battery_monitor_tick(&state, cycles(1900U));
    CHECK(state.sample_count == 13U);
    CHECK(state.adc_error_count == 0U);
    CHECK(state.range_error_count == 0U);
    sample(&state, 2000U, 1190);
    CHECK(state.stale == 0U);
    CHECK(state.ready == 0U);
    CHECK(battery_monitor_alarm_due(&state, cycles(2000U)) == 0U);
    sample(&state, 2100U, 1190);
    CHECK(battery_monitor_alarm_due(&state, cycles(2100U)) == 0U);
    sample(&state, 2200U, 1190);
    CHECK(state.ready == 1U);
    CHECK(state.low_active == 1U);
    CHECK(state.recovery_since_cycles == cycles(2200U));
    CHECK(battery_monitor_alarm_due(&state, cycles(2200U)) == 1U);
}

static void test_pause_and_alarm_resume(void)
{
    BatteryMonitor state;
    uint32_t ms;
    low(&state);
    CHECK(battery_monitor_alarm_due(&state, cycles(1200U)) == 1U);
    battery_monitor_sample(&state, cycles(1300U), 850, 0U);
    CHECK(battery_monitor_alarm_due(&state, cycles(1300U)) == 0U);
    for (ms = 1400U; ms <= 2100U; ms += 100U) {
        sample(&state, ms, 850);
        CHECK(battery_monitor_alarm_due(&state, cycles(ms)) == 0U);
    }
    sample(&state, 2200U, 850);
    CHECK(battery_monitor_alarm_due(&state, cycles(2200U)) == 1U);
    CHECK(battery_monitor_alarm_due(&state, cycles(2701U)) == 0U);
    CHECK(state.stale == 1U);
    sample(&state, 4000U, 850);
    sample(&state, 4100U, 850);
    CHECK(battery_monitor_alarm_due(&state, cycles(4100U)) == 0U);
    sample(&state, 4200U, 850);
    CHECK(battery_monitor_alarm_due(&state, cycles(4200U)) == 1U);
    CHECK(battery_monitor_alarm_due(&state, cycles(4200U)) == 0U);
    CHECK(state.alarm_count == 3U);
    CHECK(state.alarm_episode_count == 1U);
}

static void test_duplicate_timestamps_and_tick_no_confirmation(void)
{
    BatteryMonitor state;
    unsigned int i;
    battery_monitor_init(&state, CYCLES_PER_MS);
    sample(&state, 0U, 850);
    for (i = 0U; i < 30U; ++i) {
        sample(&state, 0U, 850);
        CHECK(state.ready == 0U);
        CHECK(state.sample_count == 1U);
    }
    sample(&state, 100U, 850);
    sample(&state, 200U, 850);
    sample(&state, 500U, 850);
    sample(&state, 800U, 850);
    sample(&state, 1100U, 850);
    for (i = 0U; i < 30U; ++i) {
        sample(&state, 1100U, 850);
        battery_monitor_tick(&state, cycles(1200U));
        CHECK(state.low_active == 0U);
        CHECK(state.sample_count == 6U);
        CHECK(battery_monitor_alarm_due(&state, cycles(1200U)) == 0U);
    }
    sample(&state, 1200U, 850);
    CHECK(state.low_active == 1U);
}

static void test_threshold_noise(void)
{
    BatteryMonitor state;
    uint32_t ms;
    battery_monitor_init(&state, CYCLES_PER_MS);
    for (ms = 0U; ms <= 10000U; ms += 100U) {
        sample(&state, ms, ((ms / 100U) % 2U) ? 899 : 900);
        CHECK(state.low_active == 0U);
        CHECK(battery_monitor_alarm_due(&state, cycles(ms)) == 0U);
    }
    low(&state);
    for (ms = 1300U; ms <= 10000U; ms += 100U) {
        sample(&state, ms, ((ms / 100U) % 2U) ? 949 : 950);
        CHECK(state.low_active == 1U);
    }
    CHECK(state.alarm_episode_count == 1U);
}

static void test_history_ring(void)
{
    BatteryMonitor state;
    uint32_t i;
    battery_monitor_init(&state, CYCLES_PER_MS);
    for (i = 0U; i < 40U; ++i) {
        sample(&state, i * 100U, 1000 + (int32_t)i);
        CHECK(state.history_index == (uint8_t)((i + 1U) % 16U));
        CHECK(state.history_count == ((i < 16U) ? i + 1U : 16U));
    }
    for (i = 0U; i < 16U; ++i) {
        uint32_t sequence = 24U + i;
        uint32_t slot = (state.history_index + i) % 16U;
        CHECK(state.history[slot].cycles == cycles(sequence * 100U));
        CHECK(state.history[slot].raw_cv == 1000 + (int32_t)sequence);
        CHECK(state.history[slot].filtered_cv == 999 + (int32_t)sequence);
        CHECK(state.history[slot].valid == 1U);
        CHECK(state.history[slot].ready == 1U);
    }
}

static void test_wrap_and_alternate_clock(void)
{
    BatteryMonitor state;
    uint32_t ms;
    origin = UINT32_MAX - 600U * CYCLES_PER_MS;
    low(&state);
    CHECK(battery_monitor_alarm_due(&state, cycles(1200U)) == 1U);
    for (ms = 1300U; ms <= 2200U; ms += 100U) {
        sample(&state, ms, 850);
    }
    CHECK(battery_monitor_alarm_due(&state, cycles(2200U)) == 1U);
    CHECK(state.alarm_count == 2U);
    origin = UINT32_MAX - 1600U * CYCLES_PER_MS;
    low(&state);
    CHECK(battery_monitor_alarm_due(&state, cycles(1200U)) == 1U);
    for (ms = 1300U; ms <= 2100U; ms += 100U) {
        sample(&state, ms, 850);
    }
    CHECK(battery_monitor_alarm_due(&state, cycles(2199U)) == 0U);
    sample(&state, 2200U, 850);
    CHECK(battery_monitor_alarm_due(&state, cycles(2200U)) == 1U);
    origin = 0U;
    battery_monitor_init(&state, 1U);
    for (ms = 0U; ms <= 1200U; ms += 100U) {
        battery_monitor_sample(&state, ms, 850, 1U);
    }
    CHECK(state.low_active == 1U);
    CHECK(battery_monitor_alarm_due(&state, 1200U) == 1U);
    battery_monitor_tick(&state, 1700U);
    CHECK(state.stale == 0U);
    battery_monitor_tick(&state, 1701U);
    CHECK(state.stale == 1U);
}

static void test_new_episode_after_full_wrap(void)
{
    BatteryMonitor state;
    uint32_t ms;
    uint32_t i;
    uint32_t next_alarm_cycles;
    uint32_t first_low_cycles;
    origin = 0U;
    low(&state);
    CHECK(battery_monitor_alarm_due(&state, cycles(1200U)) == 1U);
    /* Keep sampling normally for more than one full 84 MHz DWT wrap. */
    for (ms = 1300U; ms <= 51200U; ms += 100U) {
        sample(&state, ms, 1190);
    }
    CHECK(state.low_active == 0U);
    CHECK(state.alarm_count == 1U);
    /*
     * Target the next episode only 100 ms after the old alarm modulo 2^32.
     * Keeping the old alarm_sent would incorrectly suppress this first alarm.
     * Real elapsed time since the old alarm is one full wrap plus 100 ms.
     */
    next_alarm_cycles = cycles(1200U) + 100U * CYCLES_PER_MS;
    first_low_cycles = next_alarm_cycles - 1100U * CYCLES_PER_MS;
    for (i = 0U; i <= 11U; ++i) {
        battery_monitor_sample(&state,
            first_low_cycles + i * 100U * CYCLES_PER_MS, 850, 1U);
        CHECK(state.low_active == (uint8_t)(i == 11U));
    }
    CHECK(state.alarm_episode_count == 2U);
    CHECK(battery_monitor_alarm_due(&state, next_alarm_cycles) == 1U);
    CHECK(state.alarm_count == 2U);
    CHECK(state.last_alarm.cycles == next_alarm_cycles);
    CHECK(battery_monitor_alarm_due(&state, next_alarm_cycles) == 0U);
}

int main(void)
{
    test_warmup_and_median();
    test_one_and_two_spikes();
    test_confirmation_and_alarm_interval();
    test_threshold_boundaries_and_hysteresis();
    test_range_and_adc_failures();
    test_invalid_interrupts_confirmation_and_recovery();
    test_gap_boundaries();
    test_stale_boundaries_and_retained_history();
    test_pause_and_alarm_resume();
    test_duplicate_timestamps_and_tick_no_confirmation();
    test_threshold_noise();
    test_history_ring();
    test_wrap_and_alternate_clock();
    test_new_episode_after_full_wrap();
    printf("battery monitor: %u checks, %u failures\n", checks, failures);
    return failures != 0U;
}
