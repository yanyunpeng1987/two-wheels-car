#include "battery_monitor.h"
#include <string.h>

#define BATTERY_LOW_CV 900
#define BATTERY_RECOVER_CV 950
#define BATTERY_MIN_CV 500
#define BATTERY_MAX_CV 3630
#define BATTERY_CONFIRM_MS 1000U
#define BATTERY_MAX_GAP_MS 350U
#define BATTERY_STALE_MS 500U

static uint32_t duration(const BatteryMonitor *state, uint32_t ms)
{
    return state->cycles_per_ms * ms;
}

static void invalidate_window(BatteryMonitor *state)
{
    state->ready = 0U;
    state->median_count = 0U;
    state->median_index = 0U;
    state->low_pending = 0U;
    state->recovery_pending = 0U;
}

static int32_t median3(int32_t a, int32_t b, int32_t c)
{
    int32_t swap;
    if (a > b) { swap = a; a = b; b = swap; }
    if (b > c) { swap = b; b = c; c = swap; }
    if (a > b) { swap = a; a = b; b = swap; }
    return b;
}

static void record_sample(BatteryMonitor *state, uint32_t now_cycles,
                          uint8_t valid)
{
    BatteryMonitorSample *item = &state->history[state->history_index];
    item->cycles = now_cycles;
    item->raw_cv = state->raw_cv;
    item->filtered_cv = state->filtered_cv;
    item->valid = valid;
    item->ready = state->ready;
    item->stale = state->stale;
    item->low_active = state->low_active;
    state->history_index = (uint8_t)((state->history_index + 1U) %
                                    BATTERY_MONITOR_HISTORY_SIZE);
    if (state->history_count < BATTERY_MONITOR_HISTORY_SIZE) {
        ++state->history_count;
    }
}

static void confirm_sample(BatteryMonitor *state, uint32_t now_cycles)
{
    if (state->low_active != 0U) {
        state->low_pending = 0U;
        if (state->filtered_cv >= BATTERY_RECOVER_CV) {
            if (state->recovery_pending == 0U) {
                state->recovery_pending = 1U;
                state->recovery_since_cycles = now_cycles;
            } else if ((uint32_t)(now_cycles - state->recovery_since_cycles)
                       >= duration(state, BATTERY_CONFIRM_MS)) {
                state->low_active = 0U;
                state->recovery_pending = 0U;
            }
        } else {
            state->recovery_pending = 0U;
        }
    } else {
        state->recovery_pending = 0U;
        if (state->filtered_cv < BATTERY_LOW_CV) {
            if (state->low_pending == 0U) {
                state->low_pending = 1U;
                state->low_since_cycles = now_cycles;
            } else if ((uint32_t)(now_cycles - state->low_since_cycles)
                       >= duration(state, BATTERY_CONFIRM_MS)) {
                state->low_active = 1U;
                state->low_pending = 0U;
                state->alarm_sent = 0U;
                ++state->alarm_episode_count;
            }
        } else {
            state->low_pending = 0U;
        }
    }
}

void battery_monitor_init(BatteryMonitor *state, uint32_t cycles_per_ms)
{
    memset(state, 0, sizeof(*state));
    state->cycles_per_ms = cycles_per_ms;
    state->stale = 1U;
}

void battery_monitor_tick(BatteryMonitor *state, uint32_t now_cycles)
{
    if ((state->have_valid != 0U) && (state->stale == 0U) &&
        ((uint32_t)(now_cycles - state->last_valid_cycles) >
         duration(state, BATTERY_STALE_MS))) {
        state->stale = 1U;
        invalidate_window(state);
    }
}

void battery_monitor_sample(BatteryMonitor *state, uint32_t now_cycles,
                            int32_t raw_cv, uint8_t valid)
{
    if ((state->have_sample != 0U) &&
        (now_cycles == state->last_sample_cycles)) {
        return;
    }
    battery_monitor_tick(state, now_cycles);
    if ((state->have_sample != 0U) &&
        ((uint32_t)(now_cycles - state->last_sample_cycles) >
         duration(state, BATTERY_MAX_GAP_MS))) {
        invalidate_window(state);
    }
    state->have_sample = 1U;
    state->last_sample_cycles = now_cycles;
    state->raw_cv = raw_cv;
    ++state->sample_count;
    if (valid == 0U) {
        ++state->adc_error_count;
        invalidate_window(state);
        record_sample(state, now_cycles, 0U);
        return;
    }
    if (raw_cv < BATTERY_LOW_CV) {
        ++state->low_raw_count;
    }
    if ((raw_cv <= BATTERY_MIN_CV) || (raw_cv > BATTERY_MAX_CV)) {
        ++state->range_error_count;
        invalidate_window(state);
        record_sample(state, now_cycles, 0U);
        return;
    }
    if ((state->have_valid == 0U) || (raw_cv < state->min_raw_cv)) {
        state->min_raw_cv = raw_cv;
    }
    if ((state->have_valid == 0U) || (raw_cv > state->max_raw_cv)) {
        state->max_raw_cv = raw_cv;
    }
    state->have_valid = 1U;
    state->last_valid_cycles = now_cycles;
    state->stale = 0U;
    state->median_window[state->median_index] = raw_cv;
    state->median_index = (uint8_t)((state->median_index + 1U) % 3U);
    if (state->median_count < 3U) {
        ++state->median_count;
    }
    if (state->median_count == 3U) {
        state->filtered_cv = median3(state->median_window[0],
                                     state->median_window[1],
                                     state->median_window[2]);
        state->ready = 1U;
        confirm_sample(state, now_cycles);
    }
    record_sample(state, now_cycles, 1U);
}

uint8_t battery_monitor_alarm_due(BatteryMonitor *state, uint32_t now_cycles)
{
    battery_monitor_tick(state, now_cycles);
    if ((state->ready == 0U) || (state->stale != 0U) ||
        (state->low_active == 0U)) {
        return 0U;
    }
    if ((state->alarm_sent != 0U) &&
        ((uint32_t)(now_cycles - state->last_alarm_cycles) <
         duration(state, BATTERY_CONFIRM_MS))) {
        return 0U;
    }
    state->alarm_sent = 1U;
    state->last_alarm_cycles = now_cycles;
    ++state->alarm_count;
    state->last_alarm.cycles = now_cycles;
    state->last_alarm.raw_cv = state->raw_cv;
    state->last_alarm.filtered_cv = state->filtered_cv;
    state->last_alarm.min_raw_cv = state->min_raw_cv;
    return 1U;
}
