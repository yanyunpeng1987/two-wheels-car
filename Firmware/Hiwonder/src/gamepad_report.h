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
    GAMEPAD_REPORT_20BC_5500
} GamepadReportProfile;

/*
 * Decode one complete USB input report, without a Windows HID API prefix.
 * LEGACY accepts 8..32 bytes; 20BC:5500 bcdDevice 1003 accepts exactly 9.
 * Returns 1 on success. On failure returns 0 and leaves *out unchanged.
 * No USB, HAL, allocation, or global runtime state is required.
 */
int Gamepad_DecodeReport(GamepadReportProfile profile, const uint8_t *data,
                         uint16_t length, HID_GAMEPAD_Info_TypeDef *out);

#ifdef __cplusplus
}
#endif

#endif /* GAMEPAD_REPORT_H */
