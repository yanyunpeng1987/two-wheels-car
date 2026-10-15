#include "gamepad_arbiter.h"
#include <stddef.h>

typedef struct {
    uint32_t last_report_ms;
    uint8_t valid;
    uint8_t armed;
} GamepadArbiterChannel;

static GamepadArbiterChannel channels[GAMEPAD_ARBITER_CHANNELS];
static HID_GAMEPAD_Info_TypeDef output;
static uint8_t active_channel = GAMEPAD_ARBITER_NO_CHANNEL;
static uint8_t release_gate;
static uint32_t release_ms;

static void clear_output(void)
{
    output.buttons = 0U;
    output.hat = 0U;
    output.lx = 0;
    output.ly = 0;
    output.rx = 0;
    output.ry = 0;
}

static int axis_is_neutral(int8_t value)
{
    return value >= -GAMEPAD_ARBITER_NEUTRAL_DEADZONE &&
           value <= GAMEPAD_ARBITER_NEUTRAL_DEADZONE;
}

static int report_is_neutral(const HID_GAMEPAD_Info_TypeDef *decoded,
                             uint16_t rawButtons)
{
    return rawButtons == 0U && decoded->buttons == 0U && decoded->hat == 0U &&
           axis_is_neutral(decoded->lx) && axis_is_neutral(decoded->ly) &&
           axis_is_neutral(decoded->rx) && axis_is_neutral(decoded->ry);
}

static void release_owner(uint32_t nowMs)
{
    unsigned int channel;
    active_channel = GAMEPAD_ARBITER_NO_CHANNEL;
    clear_output();
    release_gate = 1U;
    release_ms = nowMs;
    for (channel = 0U; channel < GAMEPAD_ARBITER_CHANNELS; ++channel) {
        channels[channel].armed = 0U;
    }
}

void GamepadArbiter_Reset(void)
{
    unsigned int channel;
    for (channel = 0U; channel < GAMEPAD_ARBITER_CHANNELS; ++channel) {
        channels[channel].last_report_ms = 0U;
        channels[channel].valid = 0U;
        channels[channel].armed = 0U;
    }
    active_channel = GAMEPAD_ARBITER_NO_CHANNEL;
    release_gate = 0U;
    release_ms = 0U;
    clear_output();
}

void GamepadArbiter_Tick(uint32_t nowMs)
{
    unsigned int channel;
    if (release_gate && (uint32_t)(nowMs - release_ms) >=
        GAMEPAD_ARBITER_HANDOFF_MS) {
        release_gate = 0U;
    }
    for (channel = 0U; channel < GAMEPAD_ARBITER_CHANNELS; ++channel) {
        if (channels[channel].valid &&
            (uint32_t)(nowMs - channels[channel].last_report_ms) >=
            GAMEPAD_ARBITER_TIMEOUT_MS) {
            channels[channel].valid = 0U;
            channels[channel].armed = 0U;
            if (active_channel == channel) {
                release_owner(nowMs);
            }
        }
    }
}

void GamepadArbiter_Invalidate(uint8_t channel, uint32_t nowMs)
{
    if (channel >= GAMEPAD_ARBITER_CHANNELS) {
        return;
    }
    GamepadArbiter_Tick(nowMs);
    channels[channel].valid = 0U;
    channels[channel].armed = 0U;
    if (active_channel == channel) {
        release_owner(nowMs);
    }
}

void GamepadArbiter_Update(uint8_t channel,
                          const HID_GAMEPAD_Info_TypeDef *decoded,
                          uint16_t rawButtons, uint32_t nowMs)
{
    int neutral;
    if (channel >= GAMEPAD_ARBITER_CHANNELS) {
        return;
    }
    if (decoded == NULL) {
        GamepadArbiter_Invalidate(channel, nowMs);
        return;
    }
    GamepadArbiter_Tick(nowMs);
    neutral = report_is_neutral(decoded, rawButtons);
    channels[channel].last_report_ms = nowMs;
    channels[channel].valid = 1U;

    if (active_channel == channel) {
        if (neutral) {
            release_owner(nowMs);
        } else {
            output = *decoded;
        }
        return;
    }
    if (neutral) {
        channels[channel].armed = 1U;
        return;
    }
    if (active_channel == GAMEPAD_ARBITER_NO_CHANNEL &&
        channels[channel].armed && !release_gate) {
        active_channel = channel;
        output = *decoded;
    }
    /* Rejecting a held input consumes arming: no deferred takeover. */
    channels[channel].armed = 0U;
}

int GamepadArbiter_GetOutput(HID_GAMEPAD_Info_TypeDef *out)
{
    if (out == NULL) {
        return 0;
    }
    *out = output;
    return active_channel != GAMEPAD_ARBITER_NO_CHANNEL;
}

uint8_t GamepadArbiter_GetActiveChannel(void)
{
    return active_channel;
}
