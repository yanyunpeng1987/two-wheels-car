#ifndef CONTROL_STOP_TRACE_H
#define CONTROL_STOP_TRACE_H

#include <stdint.h>

#define CONTROL_STOP_TRACE_CAPACITY 64U

typedef enum {
    CONTROL_STOP_NONE = 0,
    CONTROL_STOP_ACC = 1,
    CONTROL_STOP_SPD = 2,
    CONTROL_STOP_KEY = 4,
    CONTROL_STOP_ANG = 8,
    CONTROL_STOP_UNKNOWN = 16
} ControlStopReason;

typedef struct {
    uint32_t cycles;
    uint32_t interval_cycles;
    float accel_z;
    float angle;
    float velocity_left;
    float velocity_right;
    float voltage;
    int32_t encoder_left;
    int32_t encoder_right;
    int32_t pwm_left;
    int32_t pwm_right;
    uint8_t imu_valid;
    uint8_t key_pressed;
    uint8_t flag_move;
    uint8_t output_allowed;
    uint8_t reason_mask;
} ControlStopSample;

typedef struct {
    ControlStopSample samples[CONTROL_STOP_TRACE_CAPACITY];
    ControlStopSample trigger;
    uint32_t write_index;
    uint32_t count;
    uint8_t frozen;
    uint8_t was_allowed;
} ControlStopTrace;

/* Clears metadata only; trigger and samples are valid according to metadata. */
void control_stop_trace_reset(volatile ControlStopTrace *state);

/*
 * Record through the first allowed-to-blocked output transition, then freeze.
 * output_allowed is the final PWM safety gate, independent of PWM magnitude.
 * Frozen history survives automatic restarts until an explicit reset.
 * Read trigger/history only after observing frozen != 0; reset/update require
 * one serialized writer. write_index identifies the next ring slot to write.
 */
void control_stop_trace_update(volatile ControlStopTrace *state,
                               const ControlStopSample *sample);

#endif
