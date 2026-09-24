#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdint.h>

#define BATTERY_MONITOR_HISTORY_SIZE 16U

typedef struct {
    uint32_t cycles;
    int32_t raw_cv;
    int32_t filtered_cv;
    uint8_t valid;
    uint8_t ready;
    uint8_t stale;
    uint8_t low_active;
} BatteryMonitorSample;

typedef struct {
    uint32_t cycles;
    int32_t raw_cv;
    int32_t filtered_cv;
    int32_t min_raw_cv;
} BatteryMonitorAlarm;

typedef struct {
    int32_t raw_cv;
    int32_t filtered_cv;
    uint8_t ready;
    uint8_t stale;
    uint8_t low_active;
    uint32_t sample_count;
    uint32_t adc_error_count;
    uint32_t range_error_count;
    uint32_t low_raw_count;
    uint32_t alarm_count;
    uint32_t alarm_episode_count;
    int32_t min_raw_cv;
    int32_t max_raw_cv;
    BatteryMonitorAlarm last_alarm;
    BatteryMonitorSample history[BATTERY_MONITOR_HISTORY_SIZE];
    uint8_t history_index;
    uint8_t history_count;

    /* Private state; public layout permits read-only SWD diagnostics. */
    uint32_t cycles_per_ms;
    uint32_t last_sample_cycles;
    uint32_t last_valid_cycles;
    uint32_t low_since_cycles;
    uint32_t recovery_since_cycles;
    uint32_t last_alarm_cycles;
    int32_t median_window[3];
    uint8_t median_count;
    uint8_t median_index;
    uint8_t have_sample;
    uint8_t have_valid;
    uint8_t low_pending;
    uint8_t recovery_pending;
    uint8_t alarm_sent;
} BatteryMonitor;

/*
 * All voltages are centivolts. Single main-context owner; no IRQ calls.
 * cycles_per_ms must be nonzero and <= UINT32_MAX / 1000 (84000 at 84 MHz).
 * Call sample/tick at least once per counter wrap, including ADC failures.
 * No dynamic memory, floating point, motor state or hardware dependencies.
 */
void battery_monitor_init(BatteryMonitor *state, uint32_t cycles_per_ms);

/*
 * Only distinct timestamps are new samples. valid means ADC read succeeded.
 * Range (500, 3630] cV is accepted. low_raw_count counts successful ADC reads
 * below 900 cV, including out-of-range reads; min/max use accepted samples.
 * Three consecutive accepted samples are required before ready becomes 1.
 * Errors or gaps >350 ms reset the median window and pending confirmation.
 * They never clear an already confirmed low_active condition.
 */
void battery_monitor_sample(BatteryMonitor *state, uint32_t now_cycles,
                            int32_t raw_cv, uint8_t valid);

/*
 * Age >500 ms invalidates the window without fabricating samples or recovery.
 * Diagnostic history and confirmed low_active survive all invalidation.
 */
void battery_monitor_tick(BatteryMonitor *state, uint32_t now_cycles);

/*
 * Poll from main; also checks staleness. First confirmed low alarm is due
 * immediately, then at most once per 1000 ms while ready and not stale.
 * Pauses preserve the previous alarm timestamp, preventing catch-up bursts.
 */
uint8_t battery_monitor_alarm_due(BatteryMonitor *state, uint32_t now_cycles);

#endif
