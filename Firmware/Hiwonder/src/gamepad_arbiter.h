#ifndef GAMEPAD_ARBITER_H
#define GAMEPAD_ARBITER_H

#include "gamepad_report.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GAMEPAD_ARBITER_CHANNELS 2U
#define GAMEPAD_ARBITER_NO_CHANNEL 0xFFU
#define GAMEPAD_ARBITER_TIMEOUT_MS 500U
#define GAMEPAD_ARBITER_HANDOFF_MS 10U
#define GAMEPAD_ARBITER_NEUTRAL_DEADZONE 10

/*
 * Single-context, allocation-free arbitration for physical channels 0 and 1.
 * A channel needs a new neutral report before its next nonneutral gesture.
 * Neutral means all four axes within +/-10, hat centered and every real raw
 * button released, including buttons omitted from the decoded action map.
 * Supply real button bits only: USB constant padding is not a button.
 *
 * The owner remains exclusive until neutral, invalidation or its own 500ms
 * report deadline. Other channels neither mix input nor renew that deadline.
 * Every release clears all arming and publishes zero; held rejected gestures
 * are never queued. No channel may acquire for 10ms after a release.
 *
 * GetOutput is pure: receiving or reading reports cannot acknowledge or
 * shorten the 10ms neutral handoff interval. Call Tick from the periodic
 * USB/control path so an owner expires even when no new reports arrive.
 */
void GamepadArbiter_Reset(void);
void GamepadArbiter_Update(uint8_t channel,
                          const HID_GAMEPAD_Info_TypeDef *decoded,
                          uint16_t rawButtons, uint32_t nowMs);
void GamepadArbiter_Invalidate(uint8_t channel, uint32_t nowMs);
void GamepadArbiter_Tick(uint32_t nowMs);

/* Always writes zero without an owner; returns 1 with an owner, else 0.
 * NULL output returns 0 without changing arbiter state. */
int GamepadArbiter_GetOutput(HID_GAMEPAD_Info_TypeDef *out);
uint8_t GamepadArbiter_GetActiveChannel(void);

#ifdef __cplusplus
}
#endif

#endif /* GAMEPAD_ARBITER_H */
