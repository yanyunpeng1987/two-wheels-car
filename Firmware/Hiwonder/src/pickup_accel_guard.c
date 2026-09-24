#include "pickup_accel_guard.h"

void pickup_accel_guard_reset(PickupAccelGuard *state)
{
    state->first_high_cycles = 0U;
    state->last_high_cycles = 0U;
    state->tracking = 0U;
}

uint8_t pickup_accel_guard_update(PickupAccelGuard *state,
                                 uint32_t now_cycles,
                                 uint32_t confirm_cycles,
                                 uint32_t max_gap_cycles,
                                 float accel_z,
                                 uint8_t sample_valid)
{
    if (!sample_valid || !(accel_z > 1.7f)) {
        pickup_accel_guard_reset(state);
        return 0U;
    }

    if (!state->tracking ||
        (uint32_t)(now_cycles - state->last_high_cycles) > max_gap_cycles) {
        state->first_high_cycles = now_cycles;
        state->tracking = 1U;
    }
    state->last_high_cycles = now_cycles;

    return (uint8_t)((uint32_t)(now_cycles - state->first_high_cycles) >=
                     confirm_cycles);
}
