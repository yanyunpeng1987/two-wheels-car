#include "control_stop_trace.h"

void control_stop_trace_reset(volatile ControlStopTrace *state)
{
    state->write_index = 0U;
    state->count = 0U;
    state->was_allowed = 0U;
    state->frozen = 0U;
}

void control_stop_trace_update(volatile ControlStopTrace *state,
                               const ControlStopSample *sample)
{
    uint8_t freeze = 0U;

    if (!state->frozen) {
        state->samples[state->write_index] = *sample;
        state->write_index = (state->write_index + 1U) %
                              CONTROL_STOP_TRACE_CAPACITY;
        if (state->count < CONTROL_STOP_TRACE_CAPACITY) {
            ++state->count;
        }

        if (state->was_allowed && !sample->output_allowed) {
            state->trigger = *sample;
            if (state->trigger.reason_mask == CONTROL_STOP_NONE) {
                state->trigger.reason_mask = CONTROL_STOP_UNKNOWN;
            }
            freeze = 1U;
        }
    }

    state->was_allowed = sample->output_allowed;
    if (freeze) {
        /* Publish only after the trigger, ring and metadata are complete. */
        state->frozen = 1U;
    }
}
