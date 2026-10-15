#ifndef GAMEPAD_REPORT_H
#define GAMEPAD_REPORT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t buttons;
    uint8_t hat;
    int8_t lx;
    int8_t ly;
    int8_t rx;
    int8_t ry;
} HID_GAMEPAD_Info_TypeDef;

#define GAMEPAD_BUTTON_MASK_L2        0x0001u
#define GAMEPAD_BUTTON_MASK_R2        0x0002u
#define GAMEPAD_BUTTON_MASK_SELECT    0x0004u
#define GAMEPAD_BUTTON_MASK_START     0x0008u
#define GAMEPAD_BUTTON_MASK_L3        0x0020u
#define GAMEPAD_BUTTON_MASK_R3        0x0040u
#define GAMEPAD_BUTTON_MASK_CROSS     0x0100u
#define GAMEPAD_BUTTON_MASK_CIRCLE    0x0200u
#define GAMEPAD_BUTTON_MASK_SQUARE    0x0800u
#define GAMEPAD_BUTTON_MASK_TRIANGLE  0x1000u
#define GAMEPAD_BUTTON_MASK_L1        0x4000u
#define GAMEPAD_BUTTON_MASK_R1        0x8000u
#define GAMEPAD_GET_BUTTON(gi, km) (((gi)->buttons & (km)) ? true : false)

/* Direction bits for the new profile; legacy hat values remain unchanged. */
#define GAMEPAD_HAT_CENTERED  0x00u
#define GAMEPAD_HAT_UP        0x01u
#define GAMEPAD_HAT_RIGHT     0x02u
#define GAMEPAD_HAT_DOWN      0x04u
#define GAMEPAD_HAT_LEFT      0x08u

typedef enum GamepadReportProfile {
    GAMEPAD_REPORT_LEGACY = 0,
    GAMEPAD_REPORT_20BC_5500,
    GAMEPAD_REPORT_0810_0001,
    GAMEPAD_REPORT_20BC_5500_8BYTE = 3,
    GAMEPAD_REPORT_20BC_5500_9BYTE = 4
} GamepadReportProfile;

/* Physical-axis offsets for the separately identified 64-byte-endpoint,
   123-byte-descriptor nine-byte profile, physically measured 2026-10-13. */
#define GAMEPAD_20BC_9BYTE_RIGHT_X_OFFSET 5U
#define GAMEPAD_20BC_9BYTE_RIGHT_Y_OFFSET 6U

/* Both physical controllers share interface 0 as Report IDs 1 and 2.
   Historical ID aliases below are labels, not a controller-selection policy;
   the decoder accepts both and the USB adapter arbitrates control ownership. */
#define GAMEPAD_0810_SELECTED_REPORT_ID 1U
#define GAMEPAD_0810_OTHER_REPORT_ID (3U - GAMEPAD_0810_SELECTED_REPORT_ID)
/* Digital right-stick directions share these face-button usages. */
#define GAMEPAD_0810_DIGITAL_RIGHT_USAGE 2U
#define GAMEPAD_0810_DIGITAL_LEFT_USAGE 4U
#define GAMEPAD_0810_DIGITAL_RIGHT_MASK (1U << (GAMEPAD_0810_DIGITAL_RIGHT_USAGE - 1U))
#define GAMEPAD_0810_DIGITAL_LEFT_MASK (1U << (GAMEPAD_0810_DIGITAL_LEFT_USAGE - 1U))
#define GAMEPAD_0810_ANALOG_TURN_DEADZONE 10
#if (GAMEPAD_0810_SELECTED_REPORT_ID != 1U) && (GAMEPAD_0810_SELECTED_REPORT_ID != 2U)
#error "0810:0001 selected Report ID must be 1 or 2"
#endif

/*
 * Decode one complete USB input report, without a Windows HID API prefix.
 * LEGACY accepts 8..32 bytes; 20BC:5500 bcdDevice 1003 accepts exactly 9.
 * 0810:0001 bcdDevice 0206 accepts exactly 8, including Report ID 1 or 2.
 * 20BC_5500_8BYTE accepts exactly 8 without Report ID; its C5/C4 are nibbles.
 * In that measured mode physical right X/Y are Rz/Z; no face-key turn fallback.
 * 20BC_5500_9BYTE accepts exactly 9 without Report ID; separate C5/C4 bytes
 * are unused. Its physical right axes use the explicit offsets above.
 * Only 0810 rx uses native Z outside the turn deadzone; otherwise its
 * digital right/left usages produce +127/-127 (opposing inputs cancel).
 * Returns 1 on success. On failure returns 0 and leaves *out unchanged.
 * No USB, HAL, allocation, or global runtime state is required.
 */
int Gamepad_DecodeReport(GamepadReportProfile profile, const uint8_t *data,
                         uint16_t length, HID_GAMEPAD_Info_TypeDef *out);

#ifdef __cplusplus
}
#endif

#endif /* GAMEPAD_REPORT_H */
