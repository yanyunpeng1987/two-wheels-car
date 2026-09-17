#include "gamepad_report.h"
#include <stddef.h>

/*
 * Editable mapping from USB Button Usages 1..15 to project button masks.
 * Physical labels have not been measured on this 20BC:5500 receiver.
 * Usages 5..12 use the provisional common DInput shoulder/system mapping.
 * Keep unverified face buttons unmapped: their old actions include PID edits.
 * Confirm each physical button before changing this table and its tests.
 */
static const uint16_t gamepad_20bc_5500_button_map[15] = {
    0U,                          /* Usage 1: face label unverified */
    0U,                          /* Usage 2: face label unverified */
    0U,                          /* Usage 3: face label unverified */
    0U,                          /* Usage 4: face label unverified */
    GAMEPAD_BUTTON_MASK_L1,       /* Usage 5 */
    GAMEPAD_BUTTON_MASK_R1,       /* Usage 6 */
    GAMEPAD_BUTTON_MASK_L2,       /* Usage 7 */
    GAMEPAD_BUTTON_MASK_R2,       /* Usage 8 */
    GAMEPAD_BUTTON_MASK_SELECT,   /* Usage 9 */
    GAMEPAD_BUTTON_MASK_START,    /* Usage 10 */
    GAMEPAD_BUTTON_MASK_L3,       /* Usage 11 */
    GAMEPAD_BUTTON_MASK_R3,       /* Usage 12 */
    0U,                          /* Usage 13: unassigned */
    0U,                          /* Usage 14: unassigned */
    0U                           /* Usage 15: unassigned */
};

static int8_t centered_axis(uint8_t value, int invert)
{
    int axis = invert ? 128 - (int)value : (int)value - 128;
    if (axis < -127) {
        axis = -127;
    } else if (axis > 127) {
        axis = 127;
    }
    return (int8_t)axis;
}

int Gamepad_DecodeReport(GamepadReportProfile profile, const uint8_t *data,
                         uint16_t length, HID_GAMEPAD_Info_TypeDef *out)
{
    static const uint8_t hat_directions[8] = {
        GAMEPAD_HAT_UP,
        GAMEPAD_HAT_UP | GAMEPAD_HAT_RIGHT,
        GAMEPAD_HAT_RIGHT,
        GAMEPAD_HAT_RIGHT | GAMEPAD_HAT_DOWN,
        GAMEPAD_HAT_DOWN,
        GAMEPAD_HAT_DOWN | GAMEPAD_HAT_LEFT,
        GAMEPAD_HAT_LEFT,
        GAMEPAD_HAT_LEFT | GAMEPAD_HAT_UP
    };
    HID_GAMEPAD_Info_TypeDef decoded = {0};
    uint16_t raw_buttons;
    uint8_t hat;
    unsigned int index;

    if (data == NULL || out == NULL) {
        return 0;
    }

    switch (profile) {
    case GAMEPAD_REPORT_LEGACY:
        if (length < 8U || length > 32U ||
            (length > 20U && data[20] == 0x02U)) {
            return 0;
        }
        decoded.buttons = (uint16_t)(((uint16_t)data[6] << 8) | data[7]);
        decoded.hat = (uint8_t)(0x0FU & (uint8_t)~data[5]);
        decoded.lx = (int8_t)((int)data[1] - 128);
        decoded.ly = (int8_t)(127 - (int)data[2]);
        decoded.rx = (int8_t)((int)data[3] - 128);
        decoded.ry = (int8_t)(127 - (int)data[4]);
        break;

    case GAMEPAD_REPORT_20BC_5500:
        if (length != 9U) {
            return 0;
        }
        hat = data[2] & 0x0FU;
        /* HasNull: every value outside the logical range 0..7 is neutral. */
        decoded.hat = (hat >= 8U) ? GAMEPAD_HAT_CENTERED : hat_directions[hat];
        raw_buttons = (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
        for (index = 0U; index < 15U; ++index) {
            if ((raw_buttons & (uint16_t)(1U << index)) != 0U) {
                decoded.buttons |= gamepad_20bc_5500_button_map[index];
            }
        }
        decoded.lx = centered_axis(data[3], 0);
        decoded.ly = centered_axis(data[4], 1);
        decoded.rx = centered_axis(data[5], 0);
        decoded.ry = centered_axis(data[6], 1);
        /* Bytes 7 and 8 are the captured C5/C4 fields, currently unused. */
        break;

    default:
        return 0;
    }

    *out = decoded;
    return 1;
}
