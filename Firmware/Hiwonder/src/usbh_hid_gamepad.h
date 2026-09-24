#ifndef __USBH_HID_GAMEPAD_H
#define __USBH_HID_GAMEPAD_H

#include "usbh_hid.h"
#include "gamepad_report.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GAMEPAD_USB_RX_BYTES 64U

extern HID_GAMEPAD_Info_TypeDef *info;
/* Read-only debugger observations; no logging in the control interrupt. */
extern volatile uint16_t gamepad_raw_buttons;
extern volatile uint32_t gamepad_reports_accepted;
extern volatile uint32_t gamepad_reports_rejected;

uint8_t USBH_HID_GamepadIs20BC(const USBH_HandleTypeDef *phost);
USBH_StatusTypeDef USBH_HID_GamepadInit(USBH_HandleTypeDef *phost);
void USBH_HID_GamepadReset(void);
void USBH_HID_GamepadReceive(USBH_HandleTypeDef *phost, uint32_t length);
HID_GAMEPAD_Info_TypeDef *USBH_HID_GetGamepadInfo(USBH_HandleTypeDef *phost);

#ifdef __cplusplus
}
#endif
#endif /* __USBH_HID_GAMEPAD_H */
