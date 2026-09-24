#ifndef PICKUP_ACCEL_GUARD_H
#define PICKUP_ACCEL_GUARD_H

#include <stdint.h>

typedef struct {
    uint32_t first_high_cycles;
    uint32_t last_high_cycles;
    uint8_t tracking;
} PickupAccelGuard;

/* Reset before each new balancing run. */
void pickup_accel_guard_reset(PickupAccelGuard *state);

/*
 * Confirm only uninterrupted valid samples above 1.7 g. A gap greater than
 * max_gap_cycles starts a new interval at this sample. Unsigned subtraction
 * handles one cycle-counter wrap; callers must update within one full wrap.
 * Return 1 once elapsed high-sample time reaches confirm_cycles, else 0.
 */
uint8_t pickup_accel_guard_update(PickupAccelGuard *state,
                                 uint32_t now_cycles,
                                 uint32_t confirm_cycles,
                                 uint32_t max_gap_cycles,
                                 float accel_z,
                                 uint8_t sample_valid);

#endif
